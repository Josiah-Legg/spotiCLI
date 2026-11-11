#include "modal.h"
#include "tui.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

void modal_input_open(modal_input_t *m, const char *title, const char *initial)
{
    if (!m) return;
    memset(m, 0, sizeof(*m));
    if (title) {
        strncpy(m->title, title, sizeof(m->title) - 1);
    }
    if (initial) {
        strncpy(m->buf, initial, sizeof(m->buf) - 1);
        m->len = (int)strlen(m->buf);
    }
    m->active = true;
    m->confirmed = false;
}

bool modal_input_handle_event(modal_input_t *m, const input_event_t *evt)
{
    if (!m || !m->active || !evt) return false;

    if (evt->type == INPUT_KEY) {
        switch (evt->special_key) {
        case INPUT_KEY_ENTER:
            m->confirmed = true;
            m->active = false;
            return true;
        case INPUT_KEY_ESCAPE:
            m->confirmed = false;
            m->active = false;
            return true;
        case INPUT_KEY_BACKSPACE:
            if (m->len > 0) {
                m->len--;
                m->buf[m->len] = 0;
            }
            return false;
        default:
            return false;
        }
    }

    if (evt->type == INPUT_CHAR) {
        unsigned char c = (unsigned char)evt->ch;
        if (c >= 32 && c < 127 && m->len < (int)sizeof(m->buf) - 1) {
            m->buf[m->len++] = (char)c;
            m->buf[m->len] = 0;
        }
        return false;
    }

    return false;
}

void modal_input_render(const modal_input_t *m)
{
    if (!m || !m->active) return;

    int W, H;
    tui_get_size(&W, &H);

    int box_w = 60;
    if (box_w > W - 4) box_w = W - 4;
    if (box_w < 24) box_w = 24;
    int box_h = 5;
    int x = (W - box_w) / 2;
    int y = (H - box_h) / 2;

    /* Clear/fill backdrop. */
    tui_set_color(COLOR_WHITE, COLOR_BLACK);
    for (int j = 0; j < box_h; j++) {
        tui_cursor_move(x, y + j);
        for (int i = 0; i < box_w; i++) tui_write(" ");
    }

    /* Border */
    tui_cursor_move(x, y);
    tui_write("+");
    for (int i = 1; i < box_w - 1; i++) tui_write("-");
    tui_write("+");
    for (int j = 1; j < box_h - 1; j++) {
        tui_cursor_move(x, y + j);          tui_write("|");
        tui_cursor_move(x + box_w - 1, y + j); tui_write("|");
    }
    tui_cursor_move(x, y + box_h - 1);
    tui_write("+");
    for (int i = 1; i < box_w - 1; i++) tui_write("-");
    tui_write("+");

    /* Title */
    tui_cursor_move(x + 2, y);
    tui_set_color(COLOR_CYAN, COLOR_BLACK);
    tui_set_attr(ATTR_BOLD);
    tui_printf(" %s ", m->title);
    tui_reset_style();

    /* Input line */
    tui_cursor_move(x + 2, y + 2);
    tui_set_color(COLOR_WHITE, COLOR_BLACK);
    int max_in = box_w - 4;
    int show_from = 0;
    if (m->len > max_in) show_from = m->len - max_in;
    tui_write(m->buf + show_from);
    tui_write("_");

    /* Hint */
    tui_cursor_move(x + 2, y + 3);
    tui_set_color(COLOR_WHITE, COLOR_BLACK);
    tui_write("[Enter] confirm  [Esc] cancel");
    tui_reset_style();
}
