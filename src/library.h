#ifndef SPOTICLI_LIBRARY_H
#define SPOTICLI_LIBRARY_H

#include "api.h"
#include <stdbool.h>

#define LIB_MAX_TRACKS  50
#define LIB_MAX_ALBUMS  50
#define LIB_MAX_ARTISTS 50

typedef enum {
    LIB_VIEW_TRACKS,
    LIB_VIEW_ALBUMS,
    LIB_VIEW_ARTISTS
} library_view_e;

typedef enum {
    LIB_SUB_NONE,         /* showing the main sub-view list */
    LIB_SUB_ALBUM_TRACKS, /* drilled into a saved album's tracks */
    LIB_SUB_ARTIST_TOP    /* drilled into an artist's top tracks */
} library_sub_e;

typedef struct {
    library_view_e view;

    queue_item_t   tracks[LIB_MAX_TRACKS];     int track_count;   bool tracks_loaded;
    saved_album_t  albums[LIB_MAX_ALBUMS];     int album_count;   bool albums_loaded;
    artist_t       artists[LIB_MAX_ARTISTS];   int artist_count;  bool artists_loaded;

    int sel_track;
    int sel_album;
    int sel_artist;

    library_sub_e sub;
    queue_item_t  sub_tracks[LIB_MAX_TRACKS];
    int sub_track_count;
    int sub_sel;
    char sub_title[512];
    char sub_context_uri[256];   /* spotify:album:... or spotify:artist:... for play */
} library_ui_t;

library_ui_t *library_create(void);
void library_free(library_ui_t *ui);

/* Render. Player.c draws the tab strip; library renders the content area.
   avail_w restricts the rendered width to leave room for a sidebar (0 = full). */
void library_render(library_ui_t *ui, int avail_w);

/* Lazy-load the current view if not already loaded. */
void library_ensure_loaded(library_ui_t *ui, spotify_api_t *api);

/* Cycle Tracks → Albums → Artists. Lazy-loads the new view. */
void library_cycle_view(library_ui_t *ui, spotify_api_t *api);

void library_select_next(library_ui_t *ui);
void library_select_prev(library_ui_t *ui);

/* Enter on selected: play track / play album / play artist top tracks. */
bool library_play_selected(library_ui_t *ui, spotify_api_t *api);

/* 'Q': add selected track (or all sub-tracks) to queue. */
bool library_queue_selected(library_ui_t *ui, spotify_api_t *api);

/* 'D': unsave/unfollow selected. */
bool library_remove_selected(library_ui_t *ui, spotify_api_t *api);

/* '→': drill into album/artist. Returns true if drilled. */
bool library_drill_in(library_ui_t *ui, spotify_api_t *api);

/* 'Esc' inside a drilled-in view: go back to the main list.
   Returns true if it consumed the event (was drilled in). */
bool library_drill_back(library_ui_t *ui);

#endif
