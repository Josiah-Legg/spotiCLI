#ifndef SPOTICLI_SEARCH_H
#define SPOTICLI_SEARCH_H

#include "state.h"
#include "api.h"
#include <stdbool.h>

/* Search mode */
typedef enum {
    SEARCH_TRACKS,
    SEARCH_ARTISTS,
    SEARCH_ALBUMS,
    SEARCH_PLAYLISTS
} search_type_e;

/* Search UI state */
typedef struct {
    char query[512];
    int query_len;
    search_type_e search_type;
    search_result_t *results;
    int result_count;
    int selected_result;
    bool active;
} search_ui_t;

/* Initialize search UI */
search_ui_t *search_ui_create(void);

/* Free search UI */
void search_ui_free(search_ui_t *ui);

/* Render search UI */
void search_ui_render(search_ui_t *ui);

/* Handle text input */
void search_ui_input_char(search_ui_t *ui, char ch);

/* Remove last character */
void search_ui_backspace(search_ui_t *ui);

/* Execute search */
bool search_ui_execute(search_ui_t *ui, spotify_api_t *api);

/* Navigate results */
void search_ui_select_next(search_ui_t *ui);
void search_ui_select_prev(search_ui_t *ui);

/* Get selected result */
const search_result_t *search_ui_get_selected(search_ui_t *ui);

/* Change search type */
void search_ui_change_type(search_ui_t *ui, search_type_e new_type);

#endif
