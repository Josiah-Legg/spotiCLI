#include "tui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#endif

/*
 * Double-buffered TUI renderer.
 *
 * All public draw calls (tui_write, tui_printf, tui_set_color, tui_cursor_move,
 * tui_clear, tui_draw_box, ...) mutate an in-memory back buffer instead of
 * writing to stdout. tui_flush()/tui_present() diffs the back buffer against
 * the previously-rendered front buffer, emits ANSI escapes for only the
 * changed cells into a single staging buffer, and writes that staging buffer
 * to stdout in one syscall. This is what eliminates the flicker.
 *
 * Each cell stores:
 *   - up to 4 bytes of UTF-8 for one printable column
 *   - a 16-bit foreground color (-1 default, 0..255 palette index)
 *   - a 16-bit background color (same encoding)
 *   - an attribute bitmask (bold, dim, reverse, underline)
 */

#define TUI_MAX_W 240
#define TUI_MAX_H 80

typedef struct {
    char ch[5];      /* utf-8 bytes, null terminated; "" == space */
    int16_t fg;      /* -1 default */
    int16_t bg;
    uint8_t attr;
} tui_cell_t;

static struct {
    bool initialized;
    int width;
    int height;

    int cursor_x;
    int cursor_y;

    int16_t cur_fg;
    int16_t cur_bg;
    uint8_t cur_attr;

    tui_cell_t front[TUI_MAX_H][TUI_MAX_W];
    tui_cell_t back[TUI_MAX_H][TUI_MAX_W];

    /* staging output buffer for the present pass */
    char *out;
    size_t out_len;
    size_t out_cap;

    bool cursor_hidden;

#ifdef _WIN32
    UINT original_output_cp;
    DWORD original_out_mode;
    HANDLE hout;
#endif
} g = {0};

/* ------------------------------------------------------------------ helpers */

static void out_reserve(size_t need)
{
    if (g.out_len + need + 64 < g.out_cap) return;
    size_t newcap = g.out_cap ? g.out_cap * 2 : 16384;
    while (newcap < g.out_len + need + 64) newcap *= 2;
    char *p = (char *)realloc(g.out, newcap);
    if (!p) return;
    g.out = p;
    g.out_cap = newcap;
}

static void out_write(const char *s, size_t n)
{
    out_reserve(n);
    if (!g.out) return;
    memcpy(g.out + g.out_len, s, n);
    g.out_len += n;
}

static void out_str(const char *s)
{
    out_write(s, strlen(s));
}

static void out_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) out_write(buf, (size_t)n);
}

static int utf8_byte_len(unsigned char c)
{
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static void cell_set_char(tui_cell_t *c, const char *bytes, int n)
{
    if (n > 4) n = 4;
    memcpy(c->ch, bytes, n);
    c->ch[n] = '\0';
}

static bool cell_eq(const tui_cell_t *a, const tui_cell_t *b)
{
    return a->fg == b->fg && a->bg == b->bg && a->attr == b->attr
        && strcmp(a->ch, b->ch) == 0;
}

/* ------------------------------------------------------------------ init    */

static void detect_size(void)
{
    int w = 120, h = 40;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
#else
    /* TIOCGWINSZ would be ideal; default fallback ok */
#endif
    /* Keep a small floor so the render code never divides by zero, but let
       the actual terminal size flow through — modes.c handles small sizes
       gracefully and shows a "too small" message below its own minimums. */
    if (w < 20) w = 20;
    if (h < 6)  h = 6;
    if (w > TUI_MAX_W) w = TUI_MAX_W;
    if (h > TUI_MAX_H) h = TUI_MAX_H;
    g.width = w;
    g.height = h;
}

static void clear_buffer(tui_cell_t buf[TUI_MAX_H][TUI_MAX_W])
{
    for (int y = 0; y < TUI_MAX_H; y++) {
        for (int x = 0; x < TUI_MAX_W; x++) {
            buf[y][x].ch[0] = ' ';
            buf[y][x].ch[1] = '\0';
            buf[y][x].fg = -1;
            buf[y][x].bg = -1;
            buf[y][x].attr = 0;
        }
    }
}

bool tui_init(void)
{
    if (g.initialized) return true;

#ifdef _WIN32
    g.hout = GetStdHandle(STD_OUTPUT_HANDLE);
    g.original_output_cp = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);

    DWORD mode = 0;
    if (GetConsoleMode(g.hout, &mode)) {
        g.original_out_mode = mode;
        SetConsoleMode(g.hout, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING
                                    | ENABLE_PROCESSED_OUTPUT);
    }
#endif

    detect_size();
    clear_buffer(g.front);
    clear_buffer(g.back);
    /* poison front so first present forces full redraw */
    for (int y = 0; y < TUI_MAX_H; y++) {
        for (int x = 0; x < TUI_MAX_W; x++) {
            g.front[y][x].ch[0] = '\1';
            g.front[y][x].ch[1] = '\0';
        }
    }

    g.cursor_x = 0;
    g.cursor_y = 0;
    g.cur_fg = -1;
    g.cur_bg = -1;
    g.cur_attr = 0;
    g.cursor_hidden = true;

    /* enter alt screen, hide cursor, disable line-wrap, reset */
    fputs("\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[2J\x1b[H\x1b[0m", stdout);
    fflush(stdout);

    g.initialized = true;
    return true;
}

void tui_cleanup(void)
{
    if (!g.initialized) return;

    /* leave alt screen, show cursor, restore line-wrap */
    fputs("\x1b[0m\x1b[?25h\x1b[?7h\x1b[?1049l", stdout);
    fflush(stdout);

#ifdef _WIN32
    if (g.original_output_cp) SetConsoleOutputCP(g.original_output_cp);
    if (g.original_out_mode) SetConsoleMode(g.hout, g.original_out_mode);
#endif

    free(g.out);
    g.out = NULL;
    g.out_cap = g.out_len = 0;
    g.initialized = false;
}

bool tui_is_initialized(void)
{
    return g.initialized;
}

void tui_get_size(int *w, int *h)
{
    if (w) *w = g.width;
    if (h) *h = g.height;
}

/* ------------------------------------------------------------------ state   */

void tui_clear(void)
{
    /* Don't clear front buffer — the diff will repaint changed cells.
       Just reset back buffer to spaces with default attrs. */
    clear_buffer(g.back);
    g.cursor_x = 0;
    g.cursor_y = 0;
    /* keep current style; modes.c sets it before each region */
}

void tui_cursor_move(int x, int y)
{
    g.cursor_x = x;
    g.cursor_y = y;
}

void tui_cursor_home(void)
{
    g.cursor_x = 0;
    g.cursor_y = 0;
}

void tui_cursor_show(bool show)
{
    /* Cursor is always hidden in our renderer; track state for cleanup. */
    g.cursor_hidden = !show;
}

void tui_set_color(tui_color_e fg, tui_color_e bg)
{
    g.cur_fg = (int16_t)fg;
    g.cur_bg = (int16_t)bg;
}

void tui_set_color_idx(int fg, int bg)
{
    g.cur_fg = (int16_t)fg;
    g.cur_bg = (int16_t)bg;
}

void tui_set_attr(tui_attr_e attr)
{
    g.cur_attr = (uint8_t)attr;
}

void tui_reset_style(void)
{
    g.cur_fg = -1;
    g.cur_bg = -1;
    g.cur_attr = 0;
}

/* ------------------------------------------------------------------ writes  */

static void put_char_at(int x, int y, const char *bytes, int nbytes)
{
    if (x < 0 || x >= g.width || y < 0 || y >= g.height) return;
    tui_cell_t *c = &g.back[y][x];
    cell_set_char(c, bytes, nbytes);
    c->fg = g.cur_fg;
    c->bg = g.cur_bg;
    c->attr = g.cur_attr;
}

void tui_write(const char *text)
{
    if (!text) return;
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        if (*p == '\n') {
            g.cursor_y++;
            /* don't reset x — keep absolute positioning style */
            p++;
            continue;
        }
        if (*p == '\r') { p++; continue; }
        if (*p == '\x1b') {
            /* swallow ANSI escapes embedded in input — they would corrupt
               the cell grid. We honor SGR codes by parsing simple cases. */
            p++;
            if (*p == '[') {
                p++;
                /* parse parameters and final byte */
                int params[16] = {0};
                int npar = 0;
                while (*p && (*p < 0x40 || *p > 0x7E)) {
                    if (*p >= '0' && *p <= '9') {
                        params[npar] = params[npar] * 10 + (*p - '0');
                    } else if (*p == ';') {
                        if (npar < 15) npar++;
                    }
                    p++;
                }
                if (*p == 'm') {
                    /* SGR */
                    int total = npar + 1;
                    for (int i = 0; i < total; i++) {
                        int v = params[i];
                        if (v == 0) { g.cur_fg = -1; g.cur_bg = -1; g.cur_attr = 0; }
                        else if (v == 1) g.cur_attr |= ATTR_BOLD;
                        else if (v == 2) g.cur_attr |= ATTR_DIM;
                        else if (v == 4) g.cur_attr |= ATTR_UNDERLINE;
                        else if (v == 7) g.cur_attr |= ATTR_REVERSE;
                        else if (v >= 30 && v <= 37) g.cur_fg = v - 30;
                        else if (v >= 40 && v <= 47) g.cur_bg = v - 40;
                        else if (v >= 90 && v <= 97) g.cur_fg = v - 90 + 8;
                        else if (v == 38 && i + 4 < total && params[i + 1] == 2) {
                            /* True-color foreground: 38;2;R;G;B */
                            /* Map to nearest 256-color palette for simplicity */
                            int r = params[i + 2];
                            int g_val = params[i + 3];
                            int b = params[i + 4];
                            /* Convert to 256-color: 16 + 36*r + 6*g + b (r,g,b in 0-5) */
                            int ri = (r * 6) / 256;
                            int gi = (g_val * 6) / 256;
                            int bi = (b * 6) / 256;
                            g.cur_fg = (int16_t)(16 + 36 * ri + 6 * gi + bi);
                            i += 4;
                        } else if (v == 38 && i + 2 < total && params[i + 1] == 5) {
                            g.cur_fg = params[i + 2]; i += 2;
                        } else if (v == 48 && i + 4 < total && params[i + 1] == 2) {
                            /* True-color background: 48;2;R;G;B */
                            int r = params[i + 2];
                            int g_val = params[i + 3];
                            int b = params[i + 4];
                            int ri = (r * 6) / 256;
                            int gi = (g_val * 6) / 256;
                            int bi = (b * 6) / 256;
                            g.cur_bg = (int16_t)(16 + 36 * ri + 6 * gi + bi);
                            i += 4;
                        } else if (v == 48 && i + 2 < total && params[i + 1] == 5) {
                            g.cur_bg = params[i + 2]; i += 2;
                        }
                    }
                }
                if (*p) p++;
                continue;
            }
            continue;
        }
        int n = utf8_byte_len(*p);
        put_char_at(g.cursor_x, g.cursor_y, (const char *)p, n);
        g.cursor_x++;
        p += n;
    }
}

void tui_printf(const char *fmt, ...)
{
    if (!fmt) return;
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    tui_write(buf);
}

/* ------------------------------------------------------------------ present */

static void emit_sgr(int16_t fg, int16_t bg, uint8_t attr,
                     int16_t *last_fg, int16_t *last_bg, uint8_t *last_attr)
{
    if (fg == *last_fg && bg == *last_bg && attr == *last_attr) return;

    out_str("\x1b[0");
    if (attr & ATTR_BOLD) out_str(";1");
    if (attr & ATTR_DIM) out_str(";2");
    if (attr & ATTR_UNDERLINE) out_str(";4");
    if (attr & ATTR_REVERSE) out_str(";7");
    if (fg >= 0) {
        if (fg < 16) out_printf(";%d", (fg < 8 ? 30 + fg : 90 + (fg - 8)));
        else out_printf(";38;5;%d", fg);
    }
    if (bg >= 0) {
        if (bg < 16) out_printf(";%d", (bg < 8 ? 40 + bg : 100 + (bg - 8)));
        else out_printf(";48;5;%d", bg);
    }
    out_str("m");

    *last_fg = fg;
    *last_bg = bg;
    *last_attr = attr;
}

void tui_flush(void)
{
    if (!g.initialized) return;

    g.out_len = 0;
    out_reserve(8192);

    int16_t last_fg = -2, last_bg = -2;
    uint8_t last_attr = 0xFF;
    int last_x = -2, last_y = -2;

    for (int y = 0; y < g.height; y++) {
        for (int x = 0; x < g.width; x++) {
            tui_cell_t *b = &g.back[y][x];
            tui_cell_t *f = &g.front[y][x];
            if (cell_eq(b, f)) continue;

            if (last_y != y || last_x != x) {
                out_printf("\x1b[%d;%dH", y + 1, x + 1);
                last_x = x;
                last_y = y;
            }

            emit_sgr(b->fg, b->bg, b->attr, &last_fg, &last_bg, &last_attr);

            const char *ch = b->ch;
            if (ch[0] == '\0') {
                out_str(" ");
            } else {
                out_str(ch);
            }
            *f = *b;
            last_x++;
        }
    }

    /* park cursor at home (avoid touching last column to dodge auto-wrap) */
    out_str("\x1b[H\x1b[0m");

    if (g.out && g.out_len) {
        fwrite(g.out, 1, g.out_len, stdout);
        fflush(stdout);
    }
}

void tui_refresh(void)
{
    tui_flush();
}

bool tui_get_event(tui_event_t *event)   { (void)event; return false; }
bool tui_wait_event(tui_event_t *event, int t) { (void)event; (void)t; return false; }

/* ------------------------------------------------------------------ shapes  */

void tui_draw_box(int x, int y, int width, int height, bool double_line)
{
    if (width < 2 || height < 2) return;

    const char *tl = double_line ? "╔" : "┌";
    const char *tr = double_line ? "╗" : "┐";
    const char *bl = double_line ? "╚" : "└";
    const char *br = double_line ? "╝" : "┘";
    const char *h  = double_line ? "═" : "─";
    const char *v  = double_line ? "║" : "│";

    tui_cursor_move(x, y);
    tui_write(tl);
    for (int i = 1; i < width - 1; i++) tui_write(h);
    tui_write(tr);

    for (int row = 1; row < height - 1; row++) {
        tui_cursor_move(x, y + row);
        tui_write(v);
        tui_cursor_move(x + width - 1, y + row);
        tui_write(v);
    }

    tui_cursor_move(x, y + height - 1);
    tui_write(bl);
    for (int i = 1; i < width - 1; i++) tui_write(h);
    tui_write(br);
}

void tui_draw_hline(int x, int y, int width)
{
    if (width < 1) return;
    tui_cursor_move(x, y);
    for (int i = 0; i < width; i++) tui_write("─");
}

void tui_draw_vline(int x, int y, int height)
{
    if (height < 1) return;
    for (int i = 0; i < height; i++) {
        tui_cursor_move(x, y + i);
        tui_write("│");
    }
}
