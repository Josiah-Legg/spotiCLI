#include "modes.h"
#include "album_art.h"
#include "lyrics.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

/* Enhanced Spotify-like color palette */
#define C_BG          -1
#define C_BG_DARK     16    /* Deep black */
#define C_BG_PANEL    234  /* Panel background */

#define C_ACCENT      46    /* Spotify green */
#define C_ACCENT_DIM  28    /* Dimmed green */
#define C_ACCENT_HI   82    /* Bright green */

#define C_TEXT        255   /* Pure white */
#define C_TEXT_BRIGHT 253   /* Off-white */
#define C_TEXT_DIM    248   /* Gray text */
#define C_TEXT_FAINT  240   /* Faint text */

#define C_BORDER      238   /* Border color */
#define C_BORDER_HI   242   /* Highlighted border */

#define C_TITLE       157   /* Title cyan */
#define C_ARTIST      180   /* Artist gold */
#define C_ALBUM       244   /* Album gray */

#define C_PROGRESS_BG 236   /* Progress bar background */
#define C_PROGRESS_FG 46    /* Progress bar foreground */

#define C_LYRICS_CUR  255   /* Current lyric */
#define C_LYRICS_NEXT 248   /* Next lyric */
#define C_LYRICS_PREV 240   /* Previous lyric */

/* ============== Life Cycle ============== */

mode_renderer_t *mode_renderer_create(int width, int height)
{
    mode_renderer_t *r = (mode_renderer_t *)malloc(sizeof(mode_renderer_t));
    if (!r) return NULL;
    r->screen_width = width > 0 ? width : 120;
    r->screen_height = height > 0 ? height : 40;
    r->mode = MODE_ALBUM_ART;
    return r;
}

void mode_renderer_free(mode_renderer_t *r) { if (r) free(r); }

/* ============== Utilities ============== */

static void truncate_to(char *out, size_t out_sz, const char *in, int max_cols)
{
    if (!in) { out[0] = 0; return; }
    int len = (int)strlen(in);
    if (len <= max_cols) {
        snprintf(out, out_sz, "%.*s", (int)(out_sz - 1), in);
        return;
    }
    if (max_cols < 4) { snprintf(out, out_sz, "%.*s", max_cols, in); return; }
    snprintf(out, out_sz, "%.*s...", max_cols - 3, in);
}

/* Draw a filled rectangle */
static void fill_rect(int x, int y, int w, int h, int bg)
{
    tui_set_color_idx(C_TEXT, bg);
    tui_set_attr(ATTR_NORMAL);
    for (int j = 0; j < h; j++) {
        tui_cursor_move(x, y + j);
        for (int i = 0; i < w; i++) tui_write(" ");
    }
    tui_reset_style();
}

/* Draw a rounded box with optional title */
static void rounded_box(int x, int y, int w, int h, int fg, const char *title)
{
    if (w < 4 || h < 3) return;

    tui_set_color_idx(fg, C_BG);
    tui_set_attr(ATTR_NORMAL);

    /* Top */
    tui_cursor_move(x, y);
    tui_write("╭");
    for (int i = 1; i < w - 1; i++) tui_write("─");
    tui_write("╮");

    /* Sides */
    for (int j = 1; j < h - 1; j++) {
        tui_cursor_move(x, y + j);
        tui_write("│");
        tui_cursor_move(x + w - 1, y + j);
        tui_write("│");
    }

    /* Bottom */
    tui_cursor_move(x, y + h - 1);
    tui_write("╰");
    for (int i = 1; i < w - 1; i++) tui_write("─");
    tui_write("╯");

    /* Title */
    if (title && *title) {
        tui_cursor_move(x + 2, y);
        tui_set_color_idx(C_ACCENT, C_BG);
        tui_set_attr(ATTR_BOLD);
        tui_printf(" %s ", title);
        tui_reset_style();
    }
}

/* Draw a double-line box for important panels */
static void double_box(int x, int y, int w, int h, int fg, const char *title)
{
    if (w < 4 || h < 3) return;

    tui_set_color_idx(fg, C_BG);
    tui_set_attr(ATTR_BOLD);

    /* Top */
    tui_cursor_move(x, y);
    tui_write("╔");
    for (int i = 1; i < w - 1; i++) tui_write("═");
    tui_write("╗");

    /* Sides */
    for (int j = 1; j < h - 1; j++) {
        tui_cursor_move(x, y + j);
        tui_write("║");
        tui_cursor_move(x + w - 1, y + j);
        tui_write("║");
    }

    /* Bottom */
    tui_cursor_move(x, y + h - 1);
    tui_write("╚");
    for (int i = 1; i < w - 1; i++) tui_write("═");
    tui_write("╝");

    /* Title */
    if (title && *title) {
        tui_cursor_move(x + 2, y);
        tui_set_color_idx(C_ACCENT_HI, C_BG);
        tui_set_attr(ATTR_BOLD);
        tui_printf(" %s ", title);
        tui_reset_style();
    }
}

/* ============== Responsive Layout Helpers ==============

   Reference layout was designed at ~1440p / 100% scaling (terminal roughly
   200x60+ cells). Smaller terminals (e.g. 1080p / 150% scaling, often
   ~120-150x35-45) need everything to scale.

   Strategy: keep the reference numbers when there's room, shrink/drop pieces
   in priority order when space is tight, refuse to render below a hard floor. */

#define LAYOUT_MIN_W 60
#define LAYOUT_MIN_H 14

/* Reference values (the "looks identical at 1440p" baseline). */
#define REF_QUEUE_W       38
#define REF_ART_W         64
#define REF_ART_H         32
#define REF_ART_W_SMALL   32
#define REF_ART_H_SMALL   16
#define REF_ART_LEFT_PAD  16
#define REF_HYBRID_LEFT_W 44

/* Pick a queue sidebar width that fits W, or 0 if there isn't room.
   Returns 0 when sidebar is hidden or W is too narrow to host one. */
static int responsive_queue_w(int W, bool visible)
{
    if (!visible) return 0;
    int q = REF_QUEUE_W;
    /* Cap to a third of the screen so the main area always dominates. */
    if (q > W / 3) q = W / 3;
    /* If we can't even fit a usable 22-col queue, drop the sidebar. */
    if (q < 22) return 0;
    return q;
}

/* Pick album-art dimensions that fit the given inner panel.
   Falls back to the 32x16 small art, then to no art (returns 0,0).
   *use_small is set true when the small variant should be used. */
static void responsive_art_size(int avail_w, int avail_h, int min_text_w,
                                int *art_w, int *art_h, bool *use_small)
{
    *use_small = false;
    /* Try full 64x32: needs 64 cols + min_text_w + a couple of gaps + indent. */
    if (avail_w >= REF_ART_W + min_text_w + 6 && avail_h >= REF_ART_H) {
        *art_w = REF_ART_W; *art_h = REF_ART_H; return;
    }
    /* Fall back to 32x16. */
    if (avail_w >= REF_ART_W_SMALL + min_text_w + 4 && avail_h >= REF_ART_H_SMALL) {
        *art_w = REF_ART_W_SMALL; *art_h = REF_ART_H_SMALL;
        *use_small = true; return;
    }
    /* No room for art at all. */
    *art_w = 0; *art_h = 0;
}

/* Render a centered "terminal too small" message and exit early. */
static void render_too_small(int W, int H)
{
    tui_set_color_idx(C_TEXT, C_BG);
    tui_set_attr(ATTR_BOLD);
    const char *msg = "Terminal too small — please resize";
    int len = (int)strlen(msg);
    int x = (W - len) / 2;
    int y = H / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    tui_cursor_move(x, y);
    tui_write(msg);
    tui_reset_style();
}

/* Render three centered lines (name, artist, album) inside [x, x+w). */
static void render_centered_track_info(int x, int y, int w, player_state_t *state)
{
    if (w < 4) return;
    char buf[512];
    const char *name   = state->current_track.name[0]   ? state->current_track.name   : "— No Track —";
    const char *artist = state->current_track.artist[0] ? state->current_track.artist : "—";
    const char *album  = state->current_track.album[0]  ? state->current_track.album  : "";

    truncate_to(buf, sizeof(buf), name, w);
    int lw = (int)strlen(buf);
    tui_cursor_move(x + (w - lw) / 2, y);
    tui_set_color_idx(C_TEXT, C_BG);
    tui_set_attr(ATTR_BOLD);
    tui_write(buf);

    truncate_to(buf, sizeof(buf), artist, w);
    lw = (int)strlen(buf);
    tui_cursor_move(x + (w - lw) / 2, y + 1);
    tui_set_color_idx(C_ARTIST, C_BG);
    tui_set_attr(ATTR_NORMAL);
    tui_write(buf);

    truncate_to(buf, sizeof(buf), album, w);
    lw = (int)strlen(buf);
    tui_cursor_move(x + (w - lw) / 2, y + 2);
    tui_set_color_idx(C_ALBUM, C_BG);
    tui_write(buf);

    tui_reset_style();
}

/* ============== Top Bar ============== */

static void render_top_bar(player_state_t *state, int width)
{
    /* Gradient-like header */
    tui_set_color_idx(C_BG, C_ACCENT_DIM);
    tui_set_attr(ATTR_BOLD);
    tui_cursor_move(0, 0);
    for (int i = 0; i < width; i++) tui_write(" ");

    /* Logo: full on wide bars, shortened to "♫" only when very narrow. */
    tui_cursor_move(2, 0);
    tui_set_color_idx(C_TEXT, C_ACCENT_DIM);
    const char *logo = (width >= 30) ? "♫ SpotiCLI" : "♫";
    tui_write(logo);
    int logo_end = 2 + (int)strlen(logo);

    /* Playback icon (always shown, just past the logo). */
    tui_set_attr(ATTR_NORMAL);
    const char *icon = state->playback_state == PLAYBACK_STATE_PLAYING ? "▶" :
                       state->playback_state == PLAYBACK_STATE_PAUSED  ? "⏸" : "⏹";
    if (width >= 22) {
        tui_cursor_move(logo_end + 2, 0);
        tui_write("• ");
        tui_write(icon);
    }

    /* Right-side status. Drop pieces as width shrinks so it never collides
       with the logo/icon on the left. */
    char status[64];
    if (width >= 50) {
        snprintf(status, sizeof(status), "Vol %d%% %s%s",
                 state->volume,
                 state->shuffle_enabled ? "⇄" : "  ",
                 state->repeat_mode == REPEAT_OFF ? "  " :
                 state->repeat_mode == REPEAT_ALL ? "↻" : "↻¹");
    } else if (width >= 30) {
        snprintf(status, sizeof(status), "Vol %d%%", state->volume);
    } else {
        status[0] = 0;
    }

    if (status[0]) {
        int sx = width - (int)strlen(status) - 2;
        if (sx > logo_end + 6) {
            tui_cursor_move(sx, 0);
            tui_write(status);
        }
    }

    tui_reset_style();
}

/* ============== Progress Bar ============== */

static void render_progress(int x, int y, int width, player_state_t *state)
{
    if (width < 20) return;

    int dur = state->current_track.duration_ms;
    int pos = state->current_track.progress_ms;
    if (dur < 1) dur = 1;
    if (pos < 0) pos = 0;
    if (pos > dur) pos = dur;

    /* Format time */
    char left[16], right[16];
    snprintf(left, sizeof(left), "%d:%02d", pos / 60000, (pos / 1000) % 60);
    snprintf(right, sizeof(right), "%d:%02d", dur / 60000, (dur / 1000) % 60);

    int bar_w = width - (int)strlen(left) - (int)strlen(right) - 4;
    if (bar_w < 8) return;

    tui_cursor_move(x, y);

    /* Time left */
    tui_set_color_idx(C_TEXT_DIM, C_BG);
    tui_set_attr(ATTR_NORMAL);
    tui_printf("%s ", left);

    /* Progress bar background — keep it on the regular background so the track
       row doesn't get a lighter gray strip behind it. */
    tui_set_color_idx(C_TEXT_FAINT, C_BG);
    for (int i = 0; i < bar_w; i++) tui_write("─");

    /* Progress bar fill */
    int filled = (int)((long)pos * bar_w / dur);
    tui_cursor_move(x + (int)strlen(left) + 1, y);
    tui_set_color_idx(C_ACCENT, C_BG);
    tui_set_attr(ATTR_BOLD);
    for (int i = 0; i < filled; i++) tui_write("━");

    /* Playback position indicator */
    tui_cursor_move(x + (int)strlen(left) + 1 + filled, y);
    tui_set_color_idx(C_ACCENT_HI, C_BG);
    tui_write("╸");

    /* Time right */
    tui_cursor_move(x + (int)strlen(left) + bar_w + 2, y);
    tui_set_color_idx(C_TEXT_DIM, C_BG);
    tui_set_attr(ATTR_NORMAL);
    tui_printf(" %s", right);

    tui_reset_style();
}

/* ============== Track Info ============== */

static void render_track_block(int x, int y, int width, player_state_t *state)
{
    char buf[512];

    /* Track name - large and bright */
    tui_set_color_idx(C_TEXT, C_BG);
    tui_set_attr(ATTR_BOLD);
    tui_cursor_move(x, y);
    truncate_to(buf, sizeof(buf),
                state->current_track.name[0] ? state->current_track.name : "— No Track —",
                width);
    tui_write(buf);

    /* Artist - secondary color */
    tui_set_attr(ATTR_NORMAL);
    tui_set_color_idx(C_ARTIST, C_BG);
    tui_cursor_move(x, y + 1);
    truncate_to(buf, sizeof(buf),
                state->current_track.artist[0] ? state->current_track.artist : "—",
                width);
    tui_write(buf);

    /* Album - tertiary color */
    tui_set_color_idx(C_ALBUM, C_BG);
    tui_cursor_move(x, y + 2);
    truncate_to(buf, sizeof(buf),
                state->current_track.album[0] ? state->current_track.album : "",
                width);
    tui_write(buf);

    tui_reset_style();
}

/* ============== Help Bar ============== */

static void render_help_bar(int y, int width)
{
    /* Pick the longest help string that fits, shortest as fallback. */
    const char *full   = " [Space] Play/Pause  [N/P] Next/Prev  [1/2/3] Mode  [Q] Queue  [+/-] Vol  [Esc] Quit ";
    const char *medium = " [Space] Play  [N/P] Skip  [1/2/3] Mode  [Q] Queue  [+/-] Vol  [Esc] Quit ";
    const char *small  = " [Spc] Play  [N/P] Skip  [1/2/3] Mode  [Q] Queue  [Esc] Quit ";
    const char *tiny   = " [Spc] [N/P] [1/2/3] [Q] [Esc] ";

    const char *pick = full;
    if ((int)strlen(full) + 2 > width)   pick = medium;
    if ((int)strlen(medium) + 2 > width) pick = small;
    if ((int)strlen(small) + 2 > width)  pick = tiny;
    if ((int)strlen(tiny) + 2 > width)   return;

    tui_set_color_idx(C_TEXT_FAINT, C_BG);
    tui_set_attr(ATTR_NORMAL);
    tui_cursor_move(1, y);
    tui_write(pick);
    tui_reset_style();
}

/* ============== Album Art Panel ============== */

/* Draw an ASCII art string into a rectangle, vertically centered with a small
   horizontal indent. Caller sets cursor/colors as needed; returns silently on
   empty input. */
static void draw_album_art_string(int x, int y, int max_w, int max_h, const char *art)
{
    if (!art || !art[0]) return;

    int art_rows = 0;
    for (const char *q = art; *q; q++) if (*q == '\n') art_rows++;
    if (art_rows == 0) art_rows = 1;

    int v_pad = (max_h - art_rows) / 2;
    if (v_pad < 0) v_pad = 0;

    const int H_INDENT = 2;
    int inner_x = x + H_INDENT;
    int inner_y = y + v_pad;
    int inner_h = max_h - v_pad;
    (void)max_w;

    int row = 0;
    const char *p = art;
    /* Each colored pixel can emit ~19 bytes of SGR ("\x1b[38;2;R;G;Bm") plus the glyph.
       For a 64-wide line that's worst-case ~1300 bytes; keep generous headroom so the
       buffer never truncates mid-escape (which would swallow following glyphs and
       produce blank gaps unrelated to image density). */
    char line[8192];
    int li = 0;

    while (*p && row < inner_h) {
        if (*p == '\n') {
            line[li] = 0;
            tui_cursor_move(inner_x, inner_y + row);
            tui_write(line);
            row++;
            li = 0;
            p++;
            continue;
        }
        if (li < (int)sizeof(line) - 4) {
            line[li++] = *p;
        }
        p++;
    }
    if (li > 0 && row < inner_h) {
        line[li] = 0;
        tui_cursor_move(inner_x, inner_y + row);
        tui_write(line);
    }
    tui_reset_style();
}

/* Boxed album-art panel — used by hybrid/lyrics modes. Album mode renders its own
   combined panel (art + track info) instead of using this. */
static void render_album_art_panel(int x, int y, int w, int h, player_state_t *state)
{
    double_box(x, y, w, h, C_BORDER_HI, "♪ Album Art");

    const char *art = state->album_ascii;
    if (!art || !art[0]) {
        tui_cursor_move(x + 2, y + h / 2);
        tui_set_color_idx(C_TEXT_FAINT, C_BG);
        tui_set_attr(ATTR_DIM);
        tui_write("(no album art)");
        tui_reset_style();
        return;
    }

    draw_album_art_string(x + 2, y + 1, w - 4, h - 2, state->album_ascii);
}

/* ============== Queue Panel ============== */

static void render_queue_panel(int x, int y, int w, int h, player_state_t *state)
{
    rounded_box(x, y, w, h, C_BORDER, "Up Next");

    int inner_x = x + 2;
    int inner_y = y + 1;
    int inner_w = w - 4;
    int items = h - 2;

    if (state->queue_size == 0) {
        tui_cursor_move(inner_x, inner_y + 2);
        tui_set_color_idx(C_TEXT_FAINT, C_BG);
        tui_set_attr(ATTR_DIM);
        tui_write("Queue is empty");
        tui_cursor_move(inner_x, inner_y + 3);
        tui_write("Add tracks to play next");
        tui_reset_style();
        return;
    }

    int start = state->current_queue_index - 2;
    if (start < 0) start = 0;
    if (start + items > state->queue_size) {
        start = state->queue_size - items;
        if (start < 0) start = 0;
    }

    char line[256];
    for (int i = 0; i < items && start + i < state->queue_size; i++) {
        int idx = start + i;
        bool sel = idx == state->current_queue_index;
        tui_cursor_move(inner_x, inner_y + i);

        if (sel) {
            tui_set_color_idx(C_ACCENT, C_BG);
            tui_set_attr(ATTR_BOLD);
            tui_write("▶ ");
        } else {
            tui_set_color_idx(C_TEXT_FAINT, C_BG);
            tui_set_attr(ATTR_NORMAL);
            tui_printf("%2d ", idx + 1);
        }

        tui_set_color_idx(sel ? C_TEXT_BRIGHT : C_TEXT_DIM, C_BG);
        tui_set_attr(sel ? ATTR_BOLD : ATTR_NORMAL);

        /* Show track name - artist format */
        char track_info[512];
        snprintf(track_info, sizeof(track_info), "%s - %s",
                 state->queue[idx].name[0] ? state->queue[idx].name : "Unknown",
                 state->queue[idx].artist[0] ? state->queue[idx].artist : "");
        truncate_to(line, sizeof(line), track_info, inner_w - 4);
        tui_write(line);
    }
    tui_reset_style();
}

/* ============== Lyrics Panel ============== */

/* Word-wrap one lyric line into segments. UTF-8 safe (won't split a
   multibyte sequence). Prefers breaking on spaces; falls back to a hard
   break if no space fits. Returns the segment count written. */
static int wrap_lyric_into(const char *src, int max_cols,
                           char segs[][384], int seg_capacity)
{
    int n = 0;
    if (!src || !*src) {
        if (n < seg_capacity) { segs[n][0] = 0; n++; }
        return n;
    }
    if (max_cols < 1) max_cols = 1;

    int len = (int)strlen(src);
    int i = 0;
    while (i < len && n < seg_capacity) {
        int remain = len - i;
        if (remain <= max_cols) {
            int copy = remain;
            if (copy > 383) copy = 383;
            memcpy(segs[n], src + i, copy);
            segs[n][copy] = 0;
            n++;
            break;
        }
        int seg_end = i + max_cols;
        /* Back up out of any UTF-8 continuation bytes. */
        while (seg_end > i &&
               (unsigned char)src[seg_end] >= 0x80 &&
               (unsigned char)src[seg_end] < 0xC0) {
            seg_end--;
        }
        /* Prefer the last space inside [i+1, seg_end]. */
        int brk = -1;
        for (int j = seg_end; j > i; j--) {
            if (src[j] == ' ') { brk = j; break; }
        }
        int end = brk > 0 ? brk : seg_end;
        int copy = end - i;
        if (copy > 383) copy = 383;
        memcpy(segs[n], src + i, copy);
        segs[n][copy] = 0;
        n++;
        i = end;
        if (brk > 0) i++;  /* consume the space */
    }
    return n;
}

static void render_lyrics_panel(int x, int y, int w, int h, player_state_t *state)
{
    rounded_box(x, y, w, h, C_BORDER, "Lyrics");

    int inner_x = x + 2;
    int inner_y = y + 1;
    int inner_w = w - 4;
    int rows = h - 2;

    if (state->lyrics_count == 0) {
        tui_cursor_move(inner_x + inner_w / 2 - 10, inner_y + rows / 2);
        tui_set_color_idx(C_TEXT_FAINT, C_BG);
        tui_set_attr(ATTR_DIM | ATTR_BOLD);
        tui_write("♪ No lyrics available ♪");
        tui_reset_style();
        return;
    }

    int center = rows / 2;
    int cur = state->current_lyric_index;

    /* Build a flat list of wrapped rows around the current lyric, then
       align so the first wrapped row of `cur` lands on `center`. */
    enum { ROW_CAP = 96, WIN = 12, SEG_CAP = 24 };
    static char rows_text[ROW_CAP][384];
    static int  rows_li[ROW_CAP];
    int nrows = 0;
    int cur_row_idx = -1;

    int start_li = cur - WIN; if (start_li < 0) start_li = 0;
    int end_li   = cur + WIN + 1;
    if (end_li > state->lyrics_count) end_li = state->lyrics_count;

    char segs[SEG_CAP][384];
    for (int li = start_li; li < end_li && nrows < ROW_CAP; li++) {
        int nseg = wrap_lyric_into(state->lyrics[li].text, inner_w, segs, SEG_CAP);
        for (int s = 0; s < nseg && nrows < ROW_CAP; s++) {
            if (li == cur && cur_row_idx < 0) cur_row_idx = nrows;
            rows_li[nrows] = li;
            size_t sl = strlen(segs[s]);
            if (sl > sizeof(rows_text[nrows]) - 1) sl = sizeof(rows_text[nrows]) - 1;
            memcpy(rows_text[nrows], segs[s], sl);
            rows_text[nrows][sl] = 0;
            nrows++;
        }
    }
    if (cur_row_idx < 0) cur_row_idx = 0;

    for (int row = 0; row < rows; row++) {
        int idx = cur_row_idx + (row - center);
        if (idx < 0 || idx >= nrows) continue;

        int li = rows_li[idx];
        int dist = li - cur; if (dist < 0) dist = -dist;
        int fg = dist == 0 ? C_LYRICS_CUR : dist == 1 ? C_LYRICS_NEXT : C_LYRICS_PREV;
        uint8_t attr = dist == 0 ? ATTR_BOLD : dist == 1 ? ATTR_NORMAL : ATTR_DIM;

        const char *text = rows_text[idx];
        int pad = (inner_w - (int)strlen(text)) / 2;
        if (pad < 0) pad = 0;

        tui_cursor_move(inner_x + pad, inner_y + row);
        tui_set_color_idx(fg, C_BG);
        tui_set_attr(attr);
        tui_write(text);
    }
    tui_reset_style();
}

/* ============== Mode Renderers ============== */

void mode_render_album(mode_renderer_t *r, player_state_t *state)
{
    int W = r->screen_width, H = r->screen_height;

    if (W < LAYOUT_MIN_W || H < LAYOUT_MIN_H) {
        render_too_small(W, H);
        return;
    }

    render_top_bar(state, W);

    int q_w = responsive_queue_w(W, state->queue_sidebar_visible);
    int main_w = W - q_w;

    /* Reserve rows: top bar (0), blank (1), panel, blank, progress (H-3),
       blank, help (H-1). Below that, drop the help bar then the progress
       gaps to keep the panel usable. */
    int panel_x = 1;
    int panel_y = 2;
    int panel_w = main_w - 2;
    int panel_h = H - 6;
    if (panel_h < 8) panel_h = H - 4;   /* drop help bar gap */
    if (panel_h < 6) panel_h = H - 3;
    if (panel_h < 4) panel_h = 4;

    double_box(panel_x, panel_y, panel_w, panel_h, C_BORDER_HI, "♪ SpotiCLI");

    /* Decide art size based on what fits. min_text_w reserves room for the
       track-info column to the art's right. */
    int inner_w = panel_w - 2;
    int inner_h = panel_h - 2;
    int min_text_w = 18;
    int art_w = 0, art_h = 0;
    bool use_small = false;
    responsive_art_size(inner_w, inner_h, min_text_w, &art_w, &art_h, &use_small);

    /* Indent the art from the panel's left edge. The reference look (1440p,
       64-col art, 16-col indent) puts the indent at exactly art_w/4, so we
       scale the same way — that keeps the same visual proportion when the
       art falls back to 32×16 (indent 8) or smaller. Then clamp so the
       indent never grows past the reference, never collapses below 2, and
       always leaves at least min_text_w for the track info column. */
    int art_left_pad = (art_w > 0) ? art_w / 4 : REF_ART_LEFT_PAD;
    if (art_left_pad > REF_ART_LEFT_PAD) art_left_pad = REF_ART_LEFT_PAD;
    int max_pad = (inner_w - art_w - min_text_w) / 2;
    if (max_pad < 0) max_pad = 0;
    if (art_left_pad > max_pad) art_left_pad = max_pad;
    if (art_left_pad < 2) art_left_pad = 2;

    int art_x = panel_x + 1 + art_left_pad;

    if (art_w > 0) {
        const char *art = use_small && state->album_ascii_small[0]
            ? state->album_ascii_small
            : state->album_ascii;
        int art_y = panel_y + 1 + (inner_h - art_h) / 2;
        if (art_y < panel_y + 1) art_y = panel_y + 1;
        draw_album_art_string(art_x - 2, art_y, art_w + 4, art_h, art);
    }

    /* Track info region: to the right of the art, or full-width if no art. */
    int region_left = (art_w > 0) ? art_x + art_w + 2 : panel_x + 2;
    int region_right = panel_x + panel_w - 2;
    int region_w = region_right - region_left;
    if (region_w > 4) {
        int info_y = panel_y + 1 + (inner_h - 3) / 2;
        render_centered_track_info(region_left, info_y, region_w, state);
    }

    if (q_w > 0) {
        render_queue_panel(main_w, 2, q_w - 1, panel_h, state);
    }

    /* Full-width progress bar, no box, with empty rows above and below.
       Skip if there isn't enough vertical room. */
    if (H >= 6) render_progress(2, H - 3, W - 4, state);
    if (H >= 4) render_help_bar(H - 1, W);
}

void mode_render_hybrid(mode_renderer_t *r, player_state_t *state)
{
    int W = r->screen_width, H = r->screen_height;

    if (W < LAYOUT_MIN_W || H < LAYOUT_MIN_H) {
        render_too_small(W, H);
        return;
    }

    render_top_bar(state, W);

    int q_w = responsive_queue_w(W, state->queue_sidebar_visible);
    int main_w = W - q_w;

    int panel_y = 2;
    int panel_h = H - 6;
    if (panel_h < 8) panel_h = H - 4;
    if (panel_h < 6) panel_h = H - 3;
    if (panel_h < 4) panel_h = 4;

    /* Left panel: 44 cols (reference) when there's room, otherwise scale down.
       Need enough room for a meaningful lyrics column to the right (>= 24). */
    int left_w = REF_HYBRID_LEFT_W;
    int min_lyrics_w = 24;
    if (left_w > main_w - min_lyrics_w - 2) left_w = main_w - min_lyrics_w - 2;
    if (left_w < 20) left_w = (main_w >= 40) ? main_w / 2 : main_w - 2;
    if (left_w < 12) left_w = main_w - 2;  /* lyrics drops out below */
    int left_x = 1;

    int mid_x = left_x + left_w + 1;
    int mid_w = main_w - mid_x - 1;

    /* === Left panel: art + track info, group vertically centered === */
    double_box(left_x, panel_y, left_w, panel_h, C_BORDER_HI, "♪ SpotiCLI");

    {
        const int GAP       = 2;
        const int TEXT_ROWS = 3;

        int inner_h = panel_h - 2;
        int inner_w = left_w - 2;

        /* Pick art size that fits the left panel inner width and leaves room
           for the 3-line track text plus a gap. */
        int avail_h_for_art = inner_h - TEXT_ROWS - GAP;
        if (avail_h_for_art < 0) avail_h_for_art = 0;

        int art_w = 0, art_h = 0;
        if (inner_w >= REF_ART_W_SMALL && avail_h_for_art >= REF_ART_H_SMALL) {
            art_w = REF_ART_W_SMALL; art_h = REF_ART_H_SMALL;
        } else if (inner_w >= 16 && avail_h_for_art >= 8) {
            /* Half-size fallback for very tight panels. */
            art_w = 16; art_h = 8;
        }

        int total_rows = art_h + (art_h > 0 ? GAP : 0) + TEXT_ROWS;
        int top_pad = (inner_h - total_rows) / 2;
        if (top_pad < 0) top_pad = 0;

        int group_y = panel_y + 1 + top_pad;
        int text_y = group_y + (art_h > 0 ? art_h + GAP : 0);

        if (art_w > 0) {
            int art_left = left_x + 1 + (inner_w - art_w) / 2;
            if (art_left < left_x + 1) art_left = left_x + 1;

            const char *art = state->album_ascii_small[0]
                ? state->album_ascii_small : state->album_ascii;
            draw_album_art_string(art_left - 2, group_y, art_w, art_h, art);
        }

        if (text_y + TEXT_ROWS <= panel_y + panel_h - 1) {
            render_centered_track_info(left_x + 1, text_y, inner_w, state);
        }
    }

    /* === Middle panel: lyrics — only if there's actual room === */
    if (mid_w > 10) {
        render_lyrics_panel(mid_x, panel_y, mid_w, panel_h, state);
    }

    /* === Right panel: queue === */
    if (q_w > 0) {
        render_queue_panel(main_w, 2, q_w - 1, panel_h, state);
    }

    if (H >= 6) render_progress(2, H - 3, W - 4, state);
    if (H >= 4) render_help_bar(H - 1, W);
}

/* Render the bottom strip used by lyrics mode: a 10×5 album-art thumbnail
   immediately followed by a centered "name - artist - album" line. The art
   and the text together are centered as one block within [x, x+width). */
static void render_lyrics_bottom_strip(int x, int y, int width, player_state_t *state)
{
    const int ART_W = 10;
    const int ART_H = 5;
    const int GAP   = 2;

    /* Build the combined "name - artist - album" string. */
    char info[1024];
    const char *name   = state->current_track.name[0]   ? state->current_track.name   : "— No Track —";
    const char *artist = state->current_track.artist[0] ? state->current_track.artist : "—";
    const char *album  = state->current_track.album[0]  ? state->current_track.album  : "";
    if (album[0]) {
        snprintf(info, sizeof(info), "%s - %s - %s", name, artist, album);
    } else {
        snprintf(info, sizeof(info), "%s - %s", name, artist);
    }

    int max_text_w = width - ART_W - GAP - 2;
    if (max_text_w < 8) max_text_w = 8;

    char info_trunc[1024];
    truncate_to(info_trunc, sizeof(info_trunc), info, max_text_w);
    int text_len = (int)strlen(info_trunc);

    int block_w = ART_W + GAP + text_len;
    int block_x = x + (width - block_w) / 2;
    if (block_x < x) block_x = x;

    /* Draw the tiny art (already includes ANSI color escapes per pixel). */
    const char *art = state->album_ascii_tiny[0]
        ? state->album_ascii_tiny
        : state->album_ascii_small;
    if (art && art[0]) {
        draw_album_art_string(block_x - 2, y, ART_W + 4, ART_H, art);
    }

    /* Center the text vertically against the 5-row art (row index 2). */
    int text_y = y + ART_H / 2;
    tui_cursor_move(block_x + ART_W + GAP, text_y);
    tui_set_color_idx(C_TEXT, C_BG);
    tui_set_attr(ATTR_BOLD);
    tui_write(info_trunc);
    tui_reset_style();
}

void mode_render_lyrics(mode_renderer_t *r, player_state_t *state)
{
    int W = r->screen_width, H = r->screen_height;

    if (W < LAYOUT_MIN_W || H < LAYOUT_MIN_H) {
        render_too_small(W, H);
        return;
    }

    render_top_bar(state, W);

    int q_w = responsive_queue_w(W, state->queue_sidebar_visible);
    int main_w = W - q_w;

    /* Layout (top to bottom):
         row 0       : top bar
         row 1       : blank
         row 2       : lyrics panel start
         ...         : lyrics panel (lyr_h rows)
         row 2+lyr_h : blank
         strip_y     : tiny art (5 rows) + centered "name - artist - album"
         row H-3     : progress bar
         row H-1     : help bar
       Bottom reserve = 5 art rows + 1 blank above + 1 progress + 1 blank
       above progress + 1 help + 1 gap = ~10. */
    int bottom_reserve = 10;
    int min_lyrics_h = 6;
    while (bottom_reserve > 8 && H - bottom_reserve < min_lyrics_h + 2) bottom_reserve--;
    if (bottom_reserve < 8) bottom_reserve = 8;

    int lyr_h = H - bottom_reserve;
    if (lyr_h < 4) lyr_h = 4;

    render_lyrics_panel(1, 2, main_w - 2, lyr_h, state);

    /* Queue panel matches the lyrics panel's height. */
    if (q_w > 0) {
        render_queue_panel(main_w, 2, q_w - 1, lyr_h, state);
    }

    /* Bottom strip: art + centered track info. Sits between the lyrics panel
       and the progress bar. */
    int progress_y = H - 3;
    int strip_y = 2 + lyr_h + 1;
    if (strip_y + 5 > progress_y - 1) strip_y = progress_y - 6;
    if (strip_y < 2 + lyr_h) strip_y = 2 + lyr_h;

    if (strip_y + 5 <= progress_y) {
        render_lyrics_bottom_strip(2, strip_y, W - 4, state);
    }
    if (H >= 6) render_progress(3, progress_y, W - 6, state);
    if (H >= 4) render_help_bar(H - 1, W);
}

/* Legacy API */
void mode_render_header(player_state_t *state) {
    int w, h;
    tui_get_size(&w, &h);
    render_top_bar(state, w);
}

void mode_render_controls(int y, player_state_t *state) {
    (void)state;
    int w, h;
    tui_get_size(&w, &h);
    render_help_bar(y, w);
}

void mode_render_progress(int y, player_state_t *state, int width) {
    render_progress(2, y, width, state);
}

void mode_render(mode_renderer_t *r, player_state_t *state)
{
    if (!r || !state) return;

    /* progress_ms trails real playback by roughly the API round-trip,
       so look ahead a bit when selecting the active lyric line. */
    lyrics_update_position(state, state->current_track.progress_ms + 1000);
    tui_get_size(&r->screen_width, &r->screen_height);

    switch (state->display_mode) {
    case MODE_ALBUM_ART: mode_render_album(r, state); break;
    case MODE_HYBRID:    mode_render_hybrid(r, state); break;
    case MODE_LYRICS:    mode_render_lyrics(r, state); break;
    }
}