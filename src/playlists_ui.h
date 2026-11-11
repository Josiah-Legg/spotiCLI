#ifndef SPOTICLI_PLAYLISTS_UI_H
#define SPOTICLI_PLAYLISTS_UI_H

#include "api.h"
#include "modal.h"
#include "input.h"
#include "state.h"
#include <stdbool.h>

#define PLU_MAX_PLAYLISTS 100
#define PLU_MAX_TRACKS    200

typedef enum {
    PLU_PANE_PLAYLISTS,
    PLU_PANE_TRACKS
} plu_pane_e;

typedef enum {
    PLU_MODAL_NONE,
    PLU_MODAL_CREATE,
    PLU_MODAL_RENAME
} plu_modal_kind_e;

typedef struct {
    playlist_t   playlists[PLU_MAX_PLAYLISTS];
    int          playlist_count;
    bool         playlists_loaded;
    int          sel_playlist;

    queue_item_t tracks[PLU_MAX_TRACKS];
    int          track_count;
    int          sel_track;
    char         loaded_for_id[256]; /* playlist id whose tracks are in `tracks` */

    plu_pane_e   pane;

    modal_input_t modal;
    plu_modal_kind_e modal_kind;
} playlists_ui_t;

playlists_ui_t *playlists_ui_create(void);
void playlists_ui_free(playlists_ui_t *ui);

void playlists_ui_render(playlists_ui_t *ui);

/* Lazy-load playlists list. */
void playlists_ui_ensure_loaded(playlists_ui_t *ui, spotify_api_t *api);

/* When the selected playlist changes, load (or re-load) its tracks. */
void playlists_ui_load_selected_tracks(playlists_ui_t *ui, spotify_api_t *api);

void playlists_ui_select_next(playlists_ui_t *ui, spotify_api_t *api);
void playlists_ui_select_prev(playlists_ui_t *ui, spotify_api_t *api);
void playlists_ui_pane_left(playlists_ui_t *ui);
void playlists_ui_pane_right(playlists_ui_t *ui);

bool playlists_ui_play_selected(playlists_ui_t *ui, spotify_api_t *api);

/* N: open create-playlist modal. */
void playlists_ui_open_create(playlists_ui_t *ui);
/* R: open rename-playlist modal. */
void playlists_ui_open_rename(playlists_ui_t *ui);
/* D: delete (unfollow) selected playlist. */
bool playlists_ui_delete_selected(playlists_ui_t *ui, spotify_api_t *api);
/* A: add the currently-playing track to the selected playlist. */
bool playlists_ui_add_current_track(playlists_ui_t *ui, spotify_api_t *api,
                                    const player_state_t *state);
/* X: remove selected track from selected playlist. */
bool playlists_ui_remove_track(playlists_ui_t *ui, spotify_api_t *api);
/* Shift+Up / Shift+Down: move selected track within playlist. */
bool playlists_ui_move_track_up(playlists_ui_t *ui, spotify_api_t *api);
bool playlists_ui_move_track_down(playlists_ui_t *ui, spotify_api_t *api);

/* Feed a key event to the active modal (if any). Returns true if the event
   was consumed by the modal. When the modal closes (confirmed or canceled),
   this function applies the create/rename action. */
bool playlists_ui_handle_modal_event(playlists_ui_t *ui, spotify_api_t *api,
                                     const input_event_t *evt);

#endif
