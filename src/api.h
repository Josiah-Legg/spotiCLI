#ifndef SPOTICLI_API_H
#define SPOTICLI_API_H

#include "state.h"
#include "auth.h"
#include <stdbool.h>

/* Spotify API client */
typedef struct {
    auth_state_t *auth;
    char base_url[512];
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

/* Search */
typedef struct {
    char id[256];
    char name[512];
    char artist[512];
    char album[512];
    char uri[256];
} search_result_t;

bool spotify_search(spotify_api_t *api, const char *query, int search_type,
                    search_result_t *results, int *result_count, int max_results);

/* Playlists */
typedef struct {
    char id[256];
    char name[512];
    int track_count;
} playlist_t;

bool spotify_get_user_playlists(spotify_api_t *api, playlist_t *playlists, int *count, int max_count);
bool spotify_get_playlist_tracks(spotify_api_t *api, const char *playlist_id,
                                  queue_item_t *tracks, int *count, int max_count);

/* Free API client */
void spotify_api_free(spotify_api_t *api);

#endif
