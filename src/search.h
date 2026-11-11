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

#define SEARCH_MAX_RESULTS 10

/* Search UI state */
typedef struct {
    char query[512];
    int query_len;
    search_type_e search_type;
    search_result_t *results;
    int result_count;
    int selected_result;
    bool active;
    bool editing;     /* true: keystrokes feed query; false: keys are global */

    /* Debounce: search executes when current_ms - last_keystroke_ms > 250
       AND dirty is true. */
    bool dirty;
    long long last_keystroke_ms;
    char last_executed_query[512];
    search_type_e last_executed_type;
} search_ui_t;

search_ui_t *search_ui_create(void);
void search_ui_free(search_ui_t *ui);
/* avail_w is the number of columns available before the queue sidebar
   (or the full screen width if no sidebar). Pass 0 to use the full width. */
void search_ui_render(search_ui_t *ui, int avail_w);

/* Type a single character. Marks dirty + bumps last_keystroke_ms. */
void search_ui_input_char(search_ui_t *ui, char ch, long long now_ms);
void search_ui_backspace(search_ui_t *ui, long long now_ms);

/* Execute the search synchronously against the current query/type. */
bool search_ui_execute(search_ui_t *ui, spotify_api_t *api);

/* Debounce tick: call from main loop. Fires search if it has been > 250ms
   since the last keystroke and the buffer is dirty. */
void search_ui_tick(search_ui_t *ui, spotify_api_t *api, long long now_ms);

void search_ui_select_next(search_ui_t *ui);
void search_ui_select_prev(search_ui_t *ui);
const search_result_t *search_ui_get_selected(search_ui_t *ui);

/* Cycle to next/prev type. Re-runs the search. */
void search_ui_change_type(search_ui_t *ui, search_type_e new_type, spotify_api_t *api);
void search_ui_cycle_type(search_ui_t *ui, spotify_api_t *api);

#endif
