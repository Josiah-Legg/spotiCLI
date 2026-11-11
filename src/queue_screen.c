#include "queue_screen.h"
#include "tui.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

queue_screen_t *queue_screen_create(void)
{
    queue_screen_t *ui = (queue_screen_t *)calloc(1, sizeof(*ui));
    if (!ui) return NULL;
    ui->modal_kind = QS_MODAL_NONE;
    return ui;
}

void queue_screen_free(queue_screen_t *ui) { if (ui) free(ui); }

static void clamp_sel(queue_screen_t *ui, const player_state_t *state)
{
    if (!state || state->queue_size <= 0) { ui->sel = 0; return; }
    if (ui->sel < 0) ui->sel = 0;
    if (ui->sel >= state->queue_size) ui->sel = state->queue_size - 1;
}

void queue_screen_select_next(queue_screen_t *ui, const player_state_t *state)
{
    if (!ui || !state) return;
    if (ui->sel < state->queue_size - 1) ui->sel++;
}

void queue_screen_select_prev(queue_screen_t *ui, const player_state_t *state)
{
    if (!ui || !state) return;
    if (ui->sel > 0) ui->sel--;
}

bool queue_screen_jump_to_selected(queue_screen_t *ui, spotify_api_t *api,
                                   player_state_t *state)
{
    if (!ui || !api || !state) return false;
    if (state->queue_size <= 0) return false;
    if (ui->sel <= state->current_queue_index) return false;

    int steps = ui->sel - state->current_queue_index;
    bool ok = true;
    for (int i = 0; i < steps; i++) {
        if (!spotify_next(api, state)) { ok = false; break; }
    }
    return ok;
}

void queue_screen_open_add(queue_screen_t *ui)
{
    if (!ui) return;
    ui->modal_kind = QS_MODAL_SEARCH_QUERY;
    ui->result_count = 0;
    ui->result_sel = 0;
    modal_input_open(&ui->modal, "Search to queue", "");
}

static void run_search(queue_screen_t *ui, spotify_api_t *api, const char *query)
{
    ui->result_count = 0;
    ui->result_sel = 0;
    if (!api || !query || !query[0]) return;
    int n = 0;
    spotify_search(api, query, 0 /* tracks */, ui->results, &n, QS_MAX_RESULTS);
    ui->result_count = n;
}

bool queue_screen_handle_modal_event(queue_screen_t *ui, spotify_api_t *api,
                                     const input_event_t *evt)
{
    if (!ui || !evt) return false;
    if (ui->modal_kind == QS_MODAL_NONE) return false;

    if (ui->modal_kind == QS_MODAL_SEARCH_QUERY) {
        bool closed = modal_input_handle_event(&ui->modal, evt);
        if (!closed) return true;
        if (!ui->modal.confirmed || ui->modal.len == 0) {
            ui->modal_kind = QS_MODAL_NONE;
            return true;
        }
        run_search(ui, api, ui->modal.buf);
        if (ui->result_count == 0) {
            ui->modal_kind = QS_MODAL_NONE;
        } else {
            ui->modal_kind = QS_MODAL_SEARCH_RESULTS;
        }
        return true;
    }

    /* QS_MODAL_SEARCH_RESULTS */
    if (evt->type == INPUT_KEY) {
        switch (evt->special_key) {
        case INPUT_KEY_UP:
            if (ui->result_sel > 0) ui->result_sel--;
            return true;
        case INPUT_KEY_DOWN:
            if (ui->result_sel < ui->result_count - 1) ui->result_sel++;
            return true;
        case INPUT_KEY_ESCAPE:
            ui->modal_kind = QS_MODAL_NONE;
            return true;
        case INPUT_KEY_ENTER:
            if (ui->result_sel >= 0 && ui->result_sel < ui->result_count && api) {
                const search_result_t *r = &ui->results[ui->result_sel];
                if (r->uri[0]) spotify_add_to_queue(api, r->uri);
            }
            ui->modal_kind = QS_MODAL_NONE;
            return true;
        default:
            return true; /* swallow other keys while overlay is up */
        }
    }
    return true;
}

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

static void render_results_overlay(const queue_screen_t *ui)
{
    int W, H;
    tui_get_size(&W, &H);

    int box_w = W - 8;
    if (box_w > 80) box_w = 80;
    int box_h = ui->result_count + 4;
    if (box_h > H - 4) box_h = H - 4;
    int x = (W - box_w) / 2;
    int y = (H - box_h) / 2;

    tui_set_color(COLOR_WHITE, COLOR_BLACK);
    for (int j = 0; j < box_h; j++) {
        tui_cursor_move(x, y + j);
        for (int i = 0; i < box_w; i++) tui_write(" ");
    }
    tui_cursor_move(x, y); tui_write("+");
    for (int i = 1; i < box_w - 1; i++) tui_write("-");
    tui_write("+");
    for (int j = 1; j < box_h - 1; j++) {
        tui_cursor_move(x, y + j);                tui_write("|");
        tui_cursor_move(x + box_w - 1, y + j);    tui_write("|");
    }
    tui_cursor_move(x, y + box_h - 1); tui_write("+");
    for (int i = 1; i < box_w - 1; i++) tui_write("-");
    tui_write("+");

    tui_cursor_move(x + 2, y);
    tui_set_color(COLOR_CYAN, COLOR_BLACK); tui_set_attr(ATTR_BOLD);
    tui_write(" Pick a track to queue ");
    tui_reset_style();

    int rows = box_h - 3;
    for (int i = 0; i < ui->result_count && i < rows; i++) {
        tui_cursor_move(x + 2, y + 1 + i);
        const search_result_t *r = &ui->results[i];
        char detail[1024];
        snprintf(detail, sizeof(detail), "%s — %s", r->name, r->artist);
        char line[1024];
        truncate_to(line, sizeof(line), detail, box_w - 6);
        if (i == ui->result_sel) {
            tui_set_color(COLOR_BLACK, COLOR_GREEN); tui_set_attr(ATTR_BOLD);
            tui_printf("> %s", line);
        } else {
            tui_set_color(COLOR_WHITE, COLOR_BLACK);
            tui_printf("  %s", line);
        }
        tui_reset_style();
    }

    tui_cursor_move(x + 2, y + box_h - 2);
    tui_set_color(COLOR_WHITE, COLOR_BLACK);
    tui_write("[Up/Dn] Select  [Enter] Queue  [Esc] Cancel");
    tui_reset_style();
}

void queue_screen_render(queue_screen_t *ui, const player_state_t *state)
{
    if (!ui || !state) return;

    int W, H;
    tui_get_size(&W, &H);
    int top = 3;

    tui_cursor_move(2, top);
    tui_set_color(COLOR_CYAN, COLOR_BLACK);
    tui_set_attr(ATTR_BOLD);
    tui_write("QUEUE");
    tui_reset_style();

    /* Constrain selection to current queue contents. */
    ((queue_screen_t *)ui)->sel = ui->sel; /* no-op */
    {
        queue_screen_t *mu = (queue_screen_t *)ui;
        clamp_sel(mu, state);
    }

    int list_y = top + 2;
    int rows = H - list_y - 2;
    if (rows < 4) rows = 4;

    int n = state->queue_size;
    int sel = ui->sel;
    int start = sel - rows / 2;
    if (start < 0) start = 0;
    if (start + rows > n) start = n - rows;
    if (start < 0) start = 0;

    if (n == 0) {
        tui_cursor_move(2, list_y);
        tui_set_color(COLOR_WHITE, COLOR_BLACK);
        tui_write("(queue is empty)");
        tui_reset_style();
    } else {
        for (int i = 0; i < rows && start + i < n; i++) {
            int idx = start + i;
            tui_cursor_move(2, list_y + i);
            const queue_item_t *t = &state->queue[idx];

            const char *prefix = "  ";
            if (idx == state->current_queue_index) prefix = "> ";
            else if (idx < state->current_queue_index) prefix = "  ";
            else prefix = "  ";

            char buf[1024];
            snprintf(buf, sizeof(buf), "%3d. %s%s — %s",
                     idx + 1, prefix, t->name, t->artist);
            char line[1024];
            truncate_to(line, sizeof(line), buf, W - 6);

            bool is_current = (idx == state->current_queue_index);
            bool is_sel = (idx == sel);

            if (is_sel) {
                tui_set_color(COLOR_BLACK, COLOR_GREEN); tui_set_attr(ATTR_BOLD);
                tui_printf("> %s", line);
            } else if (is_current) {
                tui_set_color(COLOR_CYAN, COLOR_BLACK); tui_set_attr(ATTR_BOLD);
                tui_printf("  %s", line);
            } else if (idx < state->current_queue_index) {
                tui_set_color(COLOR_WHITE, COLOR_BLACK);
                tui_printf("  %s", line);
            } else {
                tui_set_color(COLOR_WHITE, COLOR_BLACK);
                tui_printf("  %s", line);
            }
            tui_reset_style();
        }
    }

    tui_cursor_move(2, H - 1);
    tui_set_color_idx(240, -1);
    tui_set_attr(ATTR_NORMAL);
    tui_write(" [Up/Down] Select  [Enter] Jump to  [A] Add track  [Esc] Back ");
    tui_reset_style();

    if (ui->modal_kind == QS_MODAL_SEARCH_QUERY) modal_input_render(&ui->modal);
    else if (ui->modal_kind == QS_MODAL_SEARCH_RESULTS) render_results_overlay(ui);
}
