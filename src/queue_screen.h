#ifndef SPOTICLI_QUEUE_SCREEN_H
#define SPOTICLI_QUEUE_SCREEN_H

#include "api.h"
#include "modal.h"
#include "input.h"
#include "state.h"
#include <stdbool.h>

#define QS_MAX_RESULTS 10

typedef enum {
    QS_MODAL_NONE,
    QS_MODAL_SEARCH_QUERY,    /* typing a query */
    QS_MODAL_SEARCH_RESULTS   /* picking a result to queue */
} qs_modal_kind_e;

typedef struct {
    int sel;          /* selected row in the queue list */

    qs_modal_kind_e modal_kind;
    modal_input_t   modal;

    /* Search-results overlay (after query confirmed). */
    search_result_t results[QS_MAX_RESULTS];
    int   result_count;
    int   result_sel;
} queue_screen_t;

queue_screen_t *queue_screen_create(void);
void queue_screen_free(queue_screen_t *ui);

void queue_screen_render(queue_screen_t *ui, const player_state_t *state);

void queue_screen_select_next(queue_screen_t *ui, const player_state_t *state);
void queue_screen_select_prev(queue_screen_t *ui, const player_state_t *state);

/* Enter: jump to the selected upcoming track by calling spotify_next() N times. */
bool queue_screen_jump_to_selected(queue_screen_t *ui, spotify_api_t *api,
                                   player_state_t *state);

/* A: open a search-query modal. */
void queue_screen_open_add(queue_screen_t *ui);

/* Feed events to the active modal/result-picker. Returns true if consumed. */
bool queue_screen_handle_modal_event(queue_screen_t *ui, spotify_api_t *api,
                                     const input_event_t *evt);

#endif
