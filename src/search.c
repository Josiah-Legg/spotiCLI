#include "search.h"
#include "tui.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define DEBOUNCE_MS 250

search_ui_t *search_ui_create(void)
{
    search_ui_t *ui = (search_ui_t *)malloc(sizeof(search_ui_t));
    if (!ui) return NULL;

    memset(ui, 0, sizeof(search_ui_t));
    ui->search_type = SEARCH_TRACKS;
    ui->results = (search_result_t *)malloc(SEARCH_MAX_RESULTS * sizeof(search_result_t));
    if (!ui->results) {
        free(ui);
        return NULL;
    }
    ui->result_count = 0;

    return ui;
}

void search_ui_free(search_ui_t *ui)
{
    if (!ui) return;
    if (ui->results) free(ui->results);
    free(ui);
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

void search_ui_render(search_ui_t *ui, int avail_w)
{
    if (!ui) return;

    int W, H;
    tui_get_size(&W, &H);
    if (avail_w > 0 && avail_w < W) W = avail_w;

    /* Tab strip already drawn by player.c; we render below it. */
    int top = 3;

    tui_cursor_move(2, top);
    tui_set_color(COLOR_CYAN, COLOR_BLACK);
    tui_set_attr(ATTR_BOLD);
    tui_write("SEARCH");
    tui_reset_style();

    tui_cursor_move(2, top + 2);
    tui_write("Query: ");
    tui_set_color(COLOR_WHITE, COLOR_BLACK);
    tui_write(ui->query);
    tui_write(ui->editing ? "_" : " ");
    tui_reset_style();
    if (ui->editing) {
        tui_write("  ");
        tui_set_color(COLOR_BLACK, COLOR_GREEN);
        tui_set_attr(ATTR_BOLD);
        tui_write(" EDITING ");
        tui_reset_style();
    }

    /* Type tabs */
    tui_cursor_move(2, top + 4);
    const char *labels[] = { "Tracks", "Artists", "Albums", "Playlists" };
    for (int i = 0; i < 4; i++) {
        if (i == (int)ui->search_type) {
            tui_set_color(COLOR_BLACK, COLOR_GREEN);
            tui_set_attr(ATTR_BOLD);
        } else {
            tui_set_color(COLOR_WHITE, COLOR_BLACK);
        }
        tui_printf(" %s ", labels[i]);
        tui_reset_style();
        tui_write(" ");
    }

    /* Results */
    int list_y = top + 6;
    int rows = H - list_y - 2;
    if (rows < 4) rows = 4;

    if (ui->result_count == 0) {
        tui_cursor_move(2, list_y);
        tui_set_color(COLOR_WHITE, COLOR_BLACK);
        tui_write(ui->query_len == 0 ? "Type to search..." : "No results");
        tui_reset_style();
    } else {
        char line[512];
        for (int i = 0; i < ui->result_count && i < rows; i++) {
            tui_cursor_move(2, list_y + i);

            const search_result_t *r = &ui->results[i];

            /* Format per-type */
            char detail[1024];
            if (r->type == SR_TRACK) {
                snprintf(detail, sizeof(detail), "%s — %s — %s", r->name, r->artist, r->album);
            } else if (r->type == SR_ALBUM) {
                snprintf(detail, sizeof(detail), "%s — %s", r->name, r->artist);
            } else if (r->type == SR_PLAYLIST) {
                snprintf(detail, sizeof(detail), "%s — by %s", r->name, r->artist);
            } else {
                snprintf(detail, sizeof(detail), "%s", r->name);
            }
            truncate_to(line, sizeof(line), detail, W - 6);

            if (i == ui->selected_result) {
                tui_set_color(COLOR_BLACK, COLOR_GREEN);
                tui_printf("> %s", line);
                tui_reset_style();
            } else {
                tui_set_color(COLOR_WHITE, COLOR_BLACK);
                tui_printf("  %s", line);
                tui_reset_style();
            }
        }
    }

    /* Footer hint */
    tui_cursor_move(2, H - 1);
    tui_set_color_idx(240, -1);
    tui_set_attr(ATTR_NORMAL);
    if (ui->editing)
        tui_write(" [Esc] Stop typing  [Backspace] Delete ");
    else
        tui_write(" [/] Type  [Tab] Category  [Up/Down] Select  [Enter] Play  [Q] Add to queue  [Esc] Back ");
    tui_reset_style();
}

void search_ui_input_char(search_ui_t *ui, char ch, long long now_ms)
{
    if (!ui || ui->query_len >= (int)sizeof(ui->query) - 1) return;
    if (isprint((unsigned char)ch)) {
        ui->query[ui->query_len++] = ch;
        ui->query[ui->query_len] = '\0';
        ui->dirty = true;
        ui->last_keystroke_ms = now_ms;
    }
}

void search_ui_backspace(search_ui_t *ui, long long now_ms)
{
    if (!ui || ui->query_len <= 0) return;
    ui->query_len--;
    ui->query[ui->query_len] = '\0';
    ui->dirty = true;
    ui->last_keystroke_ms = now_ms;
}

bool search_ui_execute(search_ui_t *ui, spotify_api_t *api)
{
    if (!ui || !api) return false;

    if (ui->query_len == 0) {
        ui->result_count = 0;
        ui->selected_result = 0;
        return true;
    }

    int n = 0;
    bool ok = spotify_search(api, ui->query, (int)ui->search_type,
                             ui->results, &n, SEARCH_MAX_RESULTS);
    ui->result_count = n;
    if (ui->selected_result >= n) ui->selected_result = n > 0 ? n - 1 : 0;

    strncpy(ui->last_executed_query, ui->query, sizeof(ui->last_executed_query) - 1);
    ui->last_executed_query[sizeof(ui->last_executed_query) - 1] = 0;
    ui->last_executed_type = ui->search_type;
    ui->dirty = false;
    return ok;
}

void search_ui_tick(search_ui_t *ui, spotify_api_t *api, long long now_ms)
{
    if (!ui || !api || !ui->dirty) return;
    if (now_ms - ui->last_keystroke_ms < DEBOUNCE_MS) return;
    search_ui_execute(ui, api);
}

void search_ui_select_next(search_ui_t *ui)
{
    if (!ui) return;
    if (ui->selected_result < ui->result_count - 1) {
        ui->selected_result++;
    }
}

void search_ui_select_prev(search_ui_t *ui)
{
    if (!ui) return;
    if (ui->selected_result > 0) {
        ui->selected_result--;
    }
}

const search_result_t *search_ui_get_selected(search_ui_t *ui)
{
    if (!ui || ui->selected_result < 0 || ui->selected_result >= ui->result_count) {
        return NULL;
    }
    return &ui->results[ui->selected_result];
}

void search_ui_change_type(search_ui_t *ui, search_type_e new_type, spotify_api_t *api)
{
    if (!ui) return;
    ui->search_type = new_type;
    ui->result_count = 0;
    ui->selected_result = 0;
    if (api && ui->query_len > 0) search_ui_execute(ui, api);
}

void search_ui_cycle_type(search_ui_t *ui, spotify_api_t *api)
{
    if (!ui) return;
    search_ui_change_type(ui, (search_type_e)(((int)ui->search_type + 1) % 4), api);
}
