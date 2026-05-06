#include "search.h"
#include "tui.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

search_ui_t *search_ui_create(void)
{
    search_ui_t *ui = (search_ui_t *)malloc(sizeof(search_ui_t));
    if (!ui) return NULL;

    memset(ui, 0, sizeof(search_ui_t));
    ui->search_type = SEARCH_TRACKS;
    ui->results = (search_result_t *)malloc(50 * sizeof(search_result_t));
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

void search_ui_render(search_ui_t *ui)
{
    if (!ui) return;

    /* Draw search box */
    tui_cursor_move(10, 5);
    tui_set_color(COLOR_CYAN, COLOR_BLACK);
    tui_set_attr(ATTR_BOLD);
    tui_write("SEARCH");
    tui_reset_style();

    tui_cursor_move(10, 7);
    tui_write("Query: ");
    tui_set_color(COLOR_WHITE, COLOR_BLACK);
    tui_write(ui->query);
    tui_write("_");
    tui_reset_style();

    tui_cursor_move(10, 9);
    tui_printf("Type: %s", ui->search_type == SEARCH_TRACKS ? "Tracks" :
                         ui->search_type == SEARCH_ARTISTS ? "Artists" :
                         ui->search_type == SEARCH_ALBUMS ? "Albums" : "Playlists");

    /* Results */
    tui_cursor_move(10, 11);
    tui_write("Results:");

    for (int i = 0; i < ui->result_count && i < 10; i++) {
        tui_cursor_move(12, 12 + i);

        if (i == ui->selected_result) {
            tui_set_color(COLOR_BLACK, COLOR_GREEN);
            tui_printf("► %s", ui->results[i].name);
            tui_reset_style();
        } else {
            tui_printf("  %s", ui->results[i].name);
        }
    }
}

void search_ui_input_char(search_ui_t *ui, char ch)
{
    if (!ui || ui->query_len >= (int)sizeof(ui->query) - 1) return;

    if (isprint(ch)) {
        ui->query[ui->query_len++] = ch;
        ui->query[ui->query_len] = '\0';
    }
}

void search_ui_backspace(search_ui_t *ui)
{
    if (!ui || ui->query_len <= 0) return;

    ui->query_len--;
    ui->query[ui->query_len] = '\0';
}

bool search_ui_execute(search_ui_t *ui, spotify_api_t *api)
{
    if (!ui || !api || ui->query_len == 0) return false;

    fprintf(stderr, "[TODO] search_ui_execute: Search for '%s' via Spotify API\n", ui->query);

    /* Stub - Phase 8+ will integrate actual search via API */
    ui->result_count = 0;
    ui->selected_result = 0;

    return true;
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

void search_ui_change_type(search_ui_t *ui, search_type_e new_type)
{
    if (!ui) return;
    ui->search_type = new_type;
    ui->result_count = 0;
    ui->selected_result = 0;
}
