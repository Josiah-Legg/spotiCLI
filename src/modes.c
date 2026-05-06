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

/* ============== Top Bar ============== */

static void render_top_bar(player_state_t *state, int width)
{
    /* Gradient-like header */
    tui_set_color_idx(C_BG, C_ACCENT_DIM);
    tui_set_attr(ATTR_BOLD);
    tui_cursor_move(0, 0);
    for (int i = 0; i < width; i++) tui_write(" ");

    /* Logo and title */
    tui_cursor_move(2, 0);
    tui_set_color_idx(C_TEXT, C_ACCENT_DIM);
    tui_write("♫ SpotiCLI");

    /* Mode indicator */
    tui_set_attr(ATTR_NORMAL);
    const char *mode = state->display_mode == MODE_ALBUM_ART ? "Album" :
                       state->display_mode == MODE_HYBRID    ? "Hybrid" : "Lyrics";
    tui_cursor_move(14, 0);
    tui_write(" • ");

    /* Playback status */
    const char *icon = state->playback_state == PLAYBACK_STATE_PLAYING ? "▶" :
                       state->playback_state == PLAYBACK_STATE_PAUSED  ? "⏸" : "⏹";
    tui_write(icon);

    /* Right side info */
    char status[64];
    snprintf(status, sizeof(status), "Vol %d%% %s%s",
             state->volume,
             state->shuffle_enabled ? "⇄" : "  ",
             state->repeat_mode == REPEAT_OFF ? "  " :
             state->repeat_mode == REPEAT_ALL ? "↻" : "↻¹");

    tui_cursor_move(width - (int)strlen(status) - 2, 0);
    tui_write(status);

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
    (void)width;
    tui_set_color_idx(C_TEXT_FAINT, C_BG);
    tui_set_attr(ATTR_NORMAL);
    tui_cursor_move(1, y);
    tui_write(" [Space] Play/Pause  [N/P] Next/Prev  [1/2/3] Mode  [Q] Queue  [+/-] Vol  [Esc] Quit ");
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

    char line[512];
    for (int row = 0; row < rows; row++) {
        int li = cur + (row - center);
        if (li < 0 || li >= state->lyrics_count) continue;

        int dist = abs(row - center);
        int fg = dist == 0 ? C_LYRICS_CUR : dist == 1 ? C_LYRICS_NEXT : C_LYRICS_PREV;
        uint8_t attr = dist == 0 ? ATTR_BOLD : dist == 1 ? ATTR_NORMAL : ATTR_DIM;

        truncate_to(line, sizeof(line), state->lyrics[li].text, inner_w);
        int pad = (inner_w - (int)strlen(line)) / 2;
        if (pad < 0) pad = 0;

        tui_cursor_move(inner_x + pad, inner_y + row);
        tui_set_color_idx(fg, C_BG);
        tui_set_attr(attr);
        tui_write(line);
    }
    tui_reset_style();
}

/* ============== Mode Renderers ============== */

void mode_render_album(mode_renderer_t *r, player_state_t *state)
{
    int W = r->screen_width, H = r->screen_height;
    render_top_bar(state, W);

    int q_w = state->queue_sidebar_visible ? 38 : 0;
    int main_w = W - q_w;

    /* One unified main panel containing album art (left) + track info (right).
       Progress bar lives outside any box near the bottom, with a gap above and
       below it before the help bar. */
    int panel_x = 1;
    int panel_y = 2;
    int panel_w = main_w - 2;
    /* Layout: top bar (row 1), panel, blank, progress (H-3), blank, help (H-1). */
    int panel_h = H - 6;
    if (panel_h < 34) panel_h = 34;

    double_box(panel_x, panel_y, panel_w, panel_h, C_BORDER_HI, "♪ SpotiCLI");

    /* Album art on the left, deeply indented. The helper applies an internal
       2-col indent; we add more here so the art sits well inside the panel. */
    const int ART_LEFT_PAD = 16;
    int art_x = panel_x + 1 + ART_LEFT_PAD;
    int art_box_w = 64;
    draw_album_art_string(art_x - 2, panel_y + 1, art_box_w + 4, panel_h - 2, state->album_ascii);

    /* Track info: centered horizontally between the art's right edge and the
       panel's right border, vertically centered. */
    int art_right = art_x + art_box_w;       /* first column past the art */
    int region_left = art_right + 2;          /* small breathing room */
    int region_right = panel_x + panel_w - 2; /* one in from the right border */
    int region_w = region_right - region_left;
    if (region_w > 4) {
        char buf[512];
        int info_y = panel_y + 1 + ((panel_h - 2) - 3) / 2;

        const char *name   = state->current_track.name[0]   ? state->current_track.name   : "— No Track —";
        const char *artist = state->current_track.artist[0] ? state->current_track.artist : "—";
        const char *album  = state->current_track.album[0]  ? state->current_track.album  : "";

        /* Render each line individually so each is centered against its own width. */
        truncate_to(buf, sizeof(buf), name, region_w);
        int lw = (int)strlen(buf);
        tui_cursor_move(region_left + (region_w - lw) / 2, info_y);
        tui_set_color_idx(C_TEXT, C_BG);
        tui_set_attr(ATTR_BOLD);
        tui_write(buf);

        truncate_to(buf, sizeof(buf), artist, region_w);
        lw = (int)strlen(buf);
        tui_cursor_move(region_left + (region_w - lw) / 2, info_y + 1);
        tui_set_color_idx(C_ARTIST, C_BG);
        tui_set_attr(ATTR_NORMAL);
        tui_write(buf);

        truncate_to(buf, sizeof(buf), album, region_w);
        lw = (int)strlen(buf);
        tui_cursor_move(region_left + (region_w - lw) / 2, info_y + 2);
        tui_set_color_idx(C_ALBUM, C_BG);
        tui_write(buf);

        tui_reset_style();
    }

    if (state->queue_sidebar_visible) {
        render_queue_panel(main_w, 2, q_w - 1, H - 6, state);
    }

    /* Full-width progress bar, no box, with empty rows above and below. */
    render_progress(2, H - 3, W - 4, state);

    render_help_bar(H - 1, W);
}

void mode_render_hybrid(mode_renderer_t *r, player_state_t *state)
{
    int W = r->screen_width, H = r->screen_height;
    render_top_bar(state, W);

    int q_w = state->queue_sidebar_visible ? 38 : 0;
    int main_w = W - q_w;

    /* Three side-by-side panels above the bottom progress bar:
       [ left: 32x16 art + track info ] [ middle: lyrics ] [ right: queue ]
       Progress bar lives outside the boxes (same as album mode). */
    int panel_y = 2;
    int panel_h = H - 6;        /* leaves: blank, progress (H-3), blank, help (H-1) */
    if (panel_h < 20) panel_h = 20;

    /* Left panel: just wide enough to comfortably hold the 32-col art with padding. */
    int left_w = 44;
    if (left_w > main_w - 30) left_w = main_w / 2;
    int left_x = 1;

    /* Middle (lyrics) fills the remaining main width. */
    int mid_x = left_x + left_w + 1;
    int mid_w = main_w - mid_x - 1;

    /* === Left panel: 32x16 art + track info, group vertically centered === */
    double_box(left_x, panel_y, left_w, panel_h, C_BORDER_HI, "♪ SpotiCLI");

    {
        const int ART_ROWS  = 16;
        const int ART_COLS  = 32;
        const int GAP       = 2;     /* blank rows between art and track text */
        const int TEXT_ROWS = 3;     /* name / artist / album */
        int total_rows = ART_ROWS + GAP + TEXT_ROWS;

        int inner_h = panel_h - 2;
        int inner_w = left_w - 2;
        int top_pad = (inner_h - total_rows) / 2;
        if (top_pad < 0) top_pad = 0;

        int group_y = panel_y + 1 + top_pad;

        /* Center the 32-col art horizontally within the panel. */
        int art_left = left_x + 1 + (inner_w - ART_COLS) / 2;
        if (art_left < left_x + 1) art_left = left_x + 1;

        const char *small = state->album_ascii_small[0]
            ? state->album_ascii_small : state->album_ascii;
        /* draw_album_art_string applies an internal 2-col indent; cancel it so
           the art lands exactly at art_left and the side gaps stay equal. */
        draw_album_art_string(art_left - 2, group_y, ART_COLS, ART_ROWS, small);

        /* Track text under the art, each line centered against panel inner width. */
        int text_y = group_y + ART_ROWS + GAP;
        int region_left = left_x + 1;
        int region_w = inner_w;

        char buf[512];
        const char *name   = state->current_track.name[0]   ? state->current_track.name   : "— No Track —";
        const char *artist = state->current_track.artist[0] ? state->current_track.artist : "—";
        const char *album  = state->current_track.album[0]  ? state->current_track.album  : "";

        truncate_to(buf, sizeof(buf), name, region_w);
        int lw = (int)strlen(buf);
        tui_cursor_move(region_left + (region_w - lw) / 2, text_y);
        tui_set_color_idx(C_TEXT, C_BG);
        tui_set_attr(ATTR_BOLD);
        tui_write(buf);

        truncate_to(buf, sizeof(buf), artist, region_w);
        lw = (int)strlen(buf);
        tui_cursor_move(region_left + (region_w - lw) / 2, text_y + 1);
        tui_set_color_idx(C_ARTIST, C_BG);
        tui_set_attr(ATTR_NORMAL);
        tui_write(buf);

        truncate_to(buf, sizeof(buf), album, region_w);
        lw = (int)strlen(buf);
        tui_cursor_move(region_left + (region_w - lw) / 2, text_y + 2);
        tui_set_color_idx(C_ALBUM, C_BG);
        tui_write(buf);

        tui_reset_style();
    }

    /* === Middle panel: lyrics === */
    if (mid_w > 10) {
        render_lyrics_panel(mid_x, panel_y, mid_w, panel_h, state);
    }

    /* === Right panel: queue (unchanged) === */
    if (state->queue_sidebar_visible) {
        render_queue_panel(main_w, 2, q_w - 1, panel_h, state);
    }

    /* Full-width progress bar, no box, with empty rows above and below. */
    render_progress(2, H - 3, W - 4, state);

    render_help_bar(H - 1, W);
}

void mode_render_lyrics(mode_renderer_t *r, player_state_t *state)
{
    int W = r->screen_width, H = r->screen_height;
    render_top_bar(state, W);

    int q_w = state->queue_sidebar_visible ? 34 : 0;
    int main_w = W - q_w;

    int lyr_h = H - 12;
    render_lyrics_panel(1, 2, main_w - 2, lyr_h, state);

    if (state->queue_sidebar_visible) {
        render_queue_panel(main_w, 2, q_w - 1, H - 5, state);
    }

    int np_y = H - 9;
    render_track_block(3, np_y + 1, main_w - 6, state);
    render_progress(3, np_y + 5, main_w - 6, state);

    render_help_bar(H - 1, W);
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

    lyrics_update_position(state, state->current_track.progress_ms);
    tui_get_size(&r->screen_width, &r->screen_height);

    switch (state->display_mode) {
    case MODE_ALBUM_ART: mode_render_album(r, state); break;
    case MODE_HYBRID:    mode_render_hybrid(r, state); break;
    case MODE_LYRICS:    mode_render_lyrics(r, state); break;
    }
}