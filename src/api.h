#ifndef SPOTICLI_API_H
#define SPOTICLI_API_H

#include "state.h"
#include "auth.h"
#include <stdbool.h>

/* Spotify API client */
typedef struct {
    auth_state_t *auth;
    char base_url[512];
    /* Cached current-user id (filled lazily by spotify_get_current_user_id). */
    char user_id[256];
} spotify_api_t;

/* Initialize Spotify API client */
spotify_api_t *spotify_api_init(auth_state_t *auth);

/* Get current playback state */
bool spotify_get_current_playback(spotify_api_t *api, player_state_t *state);

/* Playback control functions */
bool spotify_play(spotify_api_t *api, player_state_t *state);
bool spotify_pause(spotify_api_t *api, player_state_t *state);
bool spotify_next(spotify_api_t *api, player_state_t *state);
bool spotify_previous(spotify_api_t *api, player_state_t *state);
bool spotify_seek(spotify_api_t *api, player_state_t *state, int position_ms);
bool spotify_set_volume(spotify_api_t *api, player_state_t *state, int volume);

/* Shuffle and repeat */
bool spotify_set_shuffle(spotify_api_t *api, player_state_t *state, bool enabled);
bool spotify_set_repeat(spotify_api_t *api, player_state_t *state, repeat_mode_e mode);

/* Device management */
bool spotify_get_devices(spotify_api_t *api, player_state_t *state);
bool spotify_transfer_playback(spotify_api_t *api, player_state_t *state, const char *device_id);

/* Queue operations */
bool spotify_add_to_queue(spotify_api_t *api, const char *uri);
bool spotify_get_queue(spotify_api_t *api, player_state_t *state);

/* Play a single track / context (album/playlist/artist).
   For tracks pass the full URI ("spotify:track:..."). For context pass
   context_uri ("spotify:album:..."). offset_uri may be NULL. */
bool spotify_play_uri(spotify_api_t *api, const char *track_uri);
bool spotify_play_context(spotify_api_t *api, const char *context_uri, const char *offset_uri);

/* Search */
typedef enum {
    SR_TRACK,
    SR_ARTIST,
    SR_ALBUM,
    SR_PLAYLIST
} search_result_type_e;

typedef struct {
    char id[256];
    char name[512];
    char artist[512];   /* For tracks/albums */
    char album[512];    /* For tracks */
    char uri[256];
    search_result_type_e type;
} search_result_t;

/* search_type: 0 = track, 1 = artist, 2 = album, 3 = playlist */
bool spotify_search(spotify_api_t *api, const char *query, int search_type,
                    search_result_t *results, int *result_count, int max_results);

/* Playlists */
typedef struct {
    char id[256];
    char name[512];
    int track_count;
    bool owned;        /* true if owned by current user (so writes are allowed) */
} playlist_t;

bool spotify_get_user_playlists(spotify_api_t *api, playlist_t *playlists, int *count, int max_count);
bool spotify_get_playlist_tracks(spotify_api_t *api, const char *playlist_id,
                                  queue_item_t *tracks, int *count, int max_count);

/* Library: saved tracks / albums / followed artists */
typedef struct {
    char id[256];
    char name[512];
    char artist[512];
} saved_album_t;

typedef struct {
    char id[256];
    char name[512];
} artist_t;

bool spotify_get_saved_tracks(spotify_api_t *api, queue_item_t *tracks, int *count, int max_count);
bool spotify_get_saved_albums(spotify_api_t *api, saved_album_t *albums, int *count, int max_count);
bool spotify_get_followed_artists(spotify_api_t *api, artist_t *artists, int *count, int max_count);

bool spotify_check_track_saved(spotify_api_t *api, const char *track_id, bool *out_saved);
bool spotify_save_track(spotify_api_t *api, const char *track_id);
bool spotify_unsave_track(spotify_api_t *api, const char *track_id);
bool spotify_save_album(spotify_api_t *api, const char *album_id);
bool spotify_unsave_album(spotify_api_t *api, const char *album_id);
bool spotify_follow_artist(spotify_api_t *api, const char *artist_id);
bool spotify_unfollow_artist(spotify_api_t *api, const char *artist_id);

/* Album / artist drill-in */
bool spotify_get_album_tracks(spotify_api_t *api, const char *album_id,
                              queue_item_t *tracks, int *count, int max_count);
bool spotify_get_artist_top_tracks(spotify_api_t *api, const char *artist_id,
                                    queue_item_t *tracks, int *count, int max_count);

/* Playlist editing */
bool spotify_get_current_user_id(spotify_api_t *api);
bool spotify_create_playlist(spotify_api_t *api, const char *name, char *out_id, int out_id_sz);
bool spotify_rename_playlist(spotify_api_t *api, const char *playlist_id, const char *new_name);
bool spotify_unfollow_playlist(spotify_api_t *api, const char *playlist_id);
bool spotify_add_track_to_playlist(spotify_api_t *api, const char *playlist_id, const char *track_uri);
bool spotify_remove_track_from_playlist(spotify_api_t *api, const char *playlist_id, const char *track_uri);
bool spotify_reorder_playlist_track(spotify_api_t *api, const char *playlist_id,
                                     int range_start, int insert_before);

/* Free API client */
void spotify_api_free(spotify_api_t *api);

#endif
