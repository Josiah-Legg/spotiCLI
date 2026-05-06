#include "api.h"
#include "http.h"
#include "json_util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Helper: check if we're in demo mode (no real auth required) */
static bool can_make_request(spotify_api_t *api)
{
    if (!api) return false;
    /* In demo mode, allow requests without token */
    if (http_is_demo_mode()) return true;
    /* Otherwise, need valid token */
    if (!api->auth) return false;
    return api->auth->token.access_token[0] != '\0';
}

/* Wrap http_request so that a 401 transparently triggers a refresh and retry.
   This keeps the player alive across the 1-hour token TTL. */
static http_response_t *api_http(spotify_api_t *api, http_method_e method,
                                 const char *url, const char *body,
                                 const char *content_type)
{
    http_response_t *r = http_request(method, url,
                                      api->auth->token.access_token,
                                      body, content_type);
    if (!r || r->status_code != 401) return r;

    fprintf(stderr, "[api] 401 -> attempting token refresh\n");
    http_response_free(r);

    if (!auth_refresh_token(api->auth)) {
        fprintf(stderr, "[api] refresh failed; run with --login to re-auth\n");
        return http_request(method, url, api->auth->token.access_token,
                            body, content_type);
    }
    auth_save_token(api->auth, NULL);
    return http_request(method, url, api->auth->token.access_token,
                        body, content_type);
}

spotify_api_t *spotify_api_init(auth_state_t *auth)
{
    spotify_api_t *api = (spotify_api_t *)malloc(sizeof(spotify_api_t));
    if (!api) return NULL;

    api->auth = auth;
    strcpy(api->base_url, "https://api.spotify.com/v1");

    return api;
}

bool spotify_get_current_playback(spotify_api_t *api, player_state_t *state)
{
    if (!api || !state) return false;
    if (!can_make_request(api)) return false;

    /* Use /me/player rather than /me/player/currently-playing so we also get the
       active device's volume_percent — otherwise state->volume stays at its init
       default and the first +/- jumps the real volume to that stale value. */
    char url[1024];
    snprintf(url, sizeof(url), "%s/me/player", api->base_url);

    http_response_t *resp = api_http(api, HTTP_GET, url, NULL, NULL);
    if (!resp) {
        fprintf(stderr, "[curr] http_request returned NULL\n");
        return false;
    }

    fprintf(stderr, "[curr] status=%d body_len=%zu body[0..200]=%.200s\n",
            resp->status_code,
            resp->body ? strlen(resp->body) : 0,
            resp->body ? resp->body : "(null)");

    bool success = false;

    if (resp->status_code == 200 && resp->body && strlen(resp->body) > 0) {
        json_t *json = json_parse_string(resp->body);
        if (json) {
            /* json_get_string returns a pointer to an internal static buffer
               that is reused on each call — copy each result before issuing
               the next lookup. */
            char name[256] = {0}, artist[256] = {0}, album[256] = {0}, image_url[512] = {0};
            const char *s;

            char id[256] = {0};

            /* Try "item.name" first (wrapped), then just "name" (unwrapped) */
            s = json_get_string(json, "item.id", "");
            if (s[0] == '\0') s = json_get_string(json, "id", "");
            strncpy(id,        s, sizeof(id) - 1);

            s = json_get_string(json, "item.name", "");
            if (s[0] == '\0') s = json_get_string(json, "name", "");
            strncpy(name,      s, sizeof(name) - 1);

            s = json_get_string(json, "item.artists[0].name", "");
            if (s[0] == '\0') s = json_get_string(json, "artists[0].name", "");
            strncpy(artist,    s, sizeof(artist) - 1);

            s = json_get_string(json, "item.album.name", "");
            if (s[0] == '\0') s = json_get_string(json, "album.name", "");
            strncpy(album,     s, sizeof(album) - 1);

            s = json_get_string(json, "item.album.images[0].url", "");
            if (s[0] == '\0') s = json_get_string(json, "album.images[0].url", "");
            strncpy(image_url, s, sizeof(image_url) - 1);

            int duration = json_get_int(json, "item.duration_ms", 0);
            if (duration == 0) duration = json_get_int(json, "duration_ms", 0);

            int progress = json_get_int(json, "progress_ms", 0);

            bool is_playing = json_get_bool(json, "is_playing", false);

            /* Sync local volume from the active device so the displayed value
               and +/- adjustments start from the device's real volume. */
            int dev_volume = json_get_int(json, "device.volume_percent", -1);
            if (dev_volume >= 0 && dev_volume <= 100) {
                state->volume = dev_volume;
            }

            fprintf(stderr, "[curr] parsed name='%s' artist='%s' album='%s' dur=%d prog=%d playing=%d\n",
                    name, artist, album, duration, progress, (int)is_playing);

            if (name[0] != '\0') {
                if (id[0] != '\0')
                    strncpy(state->current_track.id, id, sizeof(state->current_track.id) - 1);
                strncpy(state->current_track.name, name, sizeof(state->current_track.name) - 1);
                if (artist[0] != '\0')
                    strncpy(state->current_track.artist, artist, sizeof(state->current_track.artist) - 1);
                if (album[0] != '\0')
                    strncpy(state->current_track.album, album, sizeof(state->current_track.album) - 1);
                if (image_url[0] != '\0')
                    strncpy(state->current_track.cover_url, image_url, sizeof(state->current_track.cover_url) - 1);

                state->current_track.duration_ms = duration;
                state->current_track.progress_ms = progress;
                state->playback_state = is_playing ? PLAYBACK_STATE_PLAYING : PLAYBACK_STATE_PAUSED;
                state->needs_redraw = true;
                success = true;
            }
            json_decref(json);
        } else {
            fprintf(stderr, "[curr] json_parse_string returned NULL\n");
        }
    } else if (resp->status_code == 204) {
        state->playback_state = PLAYBACK_STATE_STOPPED;
        success = true;
    }

    http_response_free(resp);
    return success;
}

bool spotify_play(spotify_api_t *api, player_state_t *state)
{
    if (!api || !state) return false;
    if (!can_make_request(api)) return false;

    char url[1024];
    snprintf(url, sizeof(url), "%s/me/player/play", api->base_url);

    http_response_t *resp = api_http(api, HTTP_PUT, url, "{}", "application/json");
    if (!resp) return false;

    bool success = (resp->status_code == 204 || resp->status_code == 200);
    if (success) {
        state->playback_state = PLAYBACK_STATE_PLAYING;
        fprintf(stderr, "[OK] Resume playback\n");
    }

    http_response_free(resp);
    return success;
}

bool spotify_pause(spotify_api_t *api, player_state_t *state)
{
    if (!api || !state) return false;
    if (!can_make_request(api)) return false;

    char url[1024];
    snprintf(url, sizeof(url), "%s/me/player/pause", api->base_url);

    http_response_t *resp = api_http(api, HTTP_PUT, url, "{}", "application/json");
    if (!resp) return false;

    bool success = (resp->status_code == 204 || resp->status_code == 200);
    if (success) {
        state->playback_state = PLAYBACK_STATE_PAUSED;
        fprintf(stderr, "[OK] Paused\n");
    }

    http_response_free(resp);
    return success;
}

bool spotify_next(spotify_api_t *api, player_state_t *state)
{
    if (!api || !state) return false;
    if (!can_make_request(api)) return false;

    char url[1024];
    snprintf(url, sizeof(url), "%s/me/player/next", api->base_url);

    http_response_t *resp = api_http(api, HTTP_POST, url, NULL, NULL);
    if (!resp) return false;

    bool success = (resp->status_code == 204 || resp->status_code == 200);
    if (success) {
        fprintf(stderr, "[OK] Skipped to next\n");
    }

    http_response_free(resp);
    return success;
}

bool spotify_previous(spotify_api_t *api, player_state_t *state)
{
    if (!api || !state) return false;
    if (!can_make_request(api)) return false;

    char url[1024];
    snprintf(url, sizeof(url), "%s/me/player/previous", api->base_url);

    http_response_t *resp = api_http(api, HTTP_POST, url, NULL, NULL);
    if (!resp) return false;

    bool success = (resp->status_code == 204 || resp->status_code == 200);
    if (success) {
        fprintf(stderr, "[OK] Skipped to previous\n");
    }

    http_response_free(resp);
    return success;
}

bool spotify_seek(spotify_api_t *api, player_state_t *state, int position_ms)
{
    if (!api || !state) return false;
    if (!can_make_request(api)) return false;

    char url[1024];
    snprintf(url, sizeof(url), "%s/me/player/seek?position_ms=%d", api->base_url, position_ms);

    http_response_t *resp = api_http(api, HTTP_PUT, url, NULL, NULL);
    if (!resp) return false;

    bool success = (resp->status_code == 204 || resp->status_code == 200);
    if (success) {
        state->current_track.progress_ms = position_ms;
    }

    http_response_free(resp);
    return success;
}

bool spotify_set_volume(spotify_api_t *api, player_state_t *state, int volume)
{
    if (!api || !state || volume < 0 || volume > 100) return false;
    if (!can_make_request(api)) return false;

    char url[1024];
    snprintf(url, sizeof(url), "%s/me/player/volume?volume_percent=%d", api->base_url, volume);

    http_response_t *resp = api_http(api, HTTP_PUT, url, NULL, NULL);
    if (!resp) return false;

    bool success = (resp->status_code == 204 || resp->status_code == 200);
    if (success) {
        state->volume = volume;
    }

    http_response_free(resp);
    return success;
}

bool spotify_set_shuffle(spotify_api_t *api, player_state_t *state, bool enabled)
{
    if (!api || !state) return false;
    if (!can_make_request(api)) return false;

    char url[1024];
    snprintf(url, sizeof(url), "%s/me/player/shuffle?state=%s", api->base_url, enabled ? "true" : "false");

    http_response_t *resp = api_http(api, HTTP_PUT, url, NULL, NULL);
    if (!resp) return false;

    bool success = (resp->status_code == 204 || resp->status_code == 200);
    if (success) {
        state->shuffle_enabled = enabled;
    }

    http_response_free(resp);
    return success;
}

bool spotify_set_repeat(spotify_api_t *api, player_state_t *state, repeat_mode_e mode)
{
    if (!api || !state) return false;
    if (!can_make_request(api)) return false;

    const char *mode_str;
    switch (mode) {
    case REPEAT_OFF:
        mode_str = "off";
        break;
    case REPEAT_ALL:
        mode_str = "context";
        break;
    case REPEAT_ONE:
        mode_str = "track";
        break;
    default:
        return false;
    }

    char url[1024];
    snprintf(url, sizeof(url), "%s/me/player/repeat?state=%s", api->base_url, mode_str);

    http_response_t *resp = api_http(api, HTTP_PUT, url, NULL, NULL);
    if (!resp) return false;

    bool success = (resp->status_code == 204 || resp->status_code == 200);
    if (success) {
        state->repeat_mode = mode;
    }

    http_response_free(resp);
    return success;
}

bool spotify_get_devices(spotify_api_t *api, player_state_t *state)
{
    if (!api || !state) return false;
    if (!can_make_request(api)) return false;

    char url[1024];
    snprintf(url, sizeof(url), "%s/me/player/devices", api->base_url);

    http_response_t *resp = api_http(api, HTTP_GET, url, NULL, NULL);
    if (!resp) return false;

    bool success = (resp->status_code == 200);
    http_response_free(resp);
    return success;
}

bool spotify_transfer_playback(spotify_api_t *api, player_state_t *state, const char *device_id)
{
    if (!api || !state || !device_id) return false;
    if (!can_make_request(api)) return false;

    char url[1024];
    char body[256];

    snprintf(url, sizeof(url), "%s/me/player", api->base_url);
    snprintf(body, sizeof(body), "{\"device_ids\":[\"%s\"]}", device_id);

    http_response_t *resp = api_http(api, HTTP_PUT, url, body, "application/json");
    if (!resp) return false;

    bool success = (resp->status_code == 204 || resp->status_code == 200);
    http_response_free(resp);
    return success;
}

bool spotify_add_to_queue(spotify_api_t *api, const char *uri)
{
    if (!api || !uri) return false;
    if (!can_make_request(api)) return false;

    char url[1024];
    snprintf(url, sizeof(url), "%s/me/player/queue?uri=%s", api->base_url, uri);

    http_response_t *resp = api_http(api, HTTP_POST, url, NULL, NULL);
    if (!resp) return false;

    bool success = (resp->status_code == 204 || resp->status_code == 200);
    http_response_free(resp);
    return success;
}

bool spotify_get_queue(spotify_api_t *api, player_state_t *state)
{
    if (!api || !state) return false;
    if (!can_make_request(api)) return false;

    char url[1024];
    snprintf(url, sizeof(url), "%s/me/player/queue", api->base_url);

    http_response_t *resp = api_http(api, HTTP_GET, url, NULL, NULL);
    if (!resp) return false;

    bool success = false;
    if (resp->status_code == 200 && resp->body && strlen(resp->body) > 0) {
        json_t *json = json_parse_string(resp->body);
        if (json) {
            /* Find the queue array in the response */
            const char *queue_start = strstr(json->raw, "\"queue\"");
            if (!queue_start) {
                /* Response might be just the array directly */
                queue_start = json->raw;
            } else {
                /* Find the [ after "queue": */
                queue_start = strchr(queue_start, '[');
                if (!queue_start) {
                    json_decref(json);
                    http_response_free(resp);
                    return false;
                }
            }

            queue_clear(state);

            const char *p = queue_start;
            const char *end = json->raw + json->size;

            /* Skip past the opening bracket */
            if (*p == '[') p++;

            while (p < end) {
                /* Find next { */
                p = strchr(p, '{');
                if (!p || p >= end) break;

                /* Find matching } */
                const char *obj_start = p;
                int depth = 0;
                const char *obj_end = NULL;
                for (const char *curr = p; curr < end; curr++) {
                    if (*curr == '{') depth++;
                    else if (*curr == '}') {
                        depth--;
                        if (depth == 0) {
                            obj_end = curr + 1;
                            break;
                        }
                    }
                }

                if (!obj_end) break;

                /* Extract this object as a string and parse it */
                size_t len = (size_t)(obj_end - obj_start);
                char *buf = (char *)malloc(len + 1);
                if (!buf) break;
                memcpy(buf, obj_start, len);
                buf[len] = 0;

                json_t *item = json_parse_string(buf);
                free(buf);

                if (item) {
                    const char *id = json_get_string(item, "id", "");
                    /* Try track.name first (for nested structure), then name directly */
                    const char *name = json_get_string(item, "name", NULL);
                    if (!name || name[0] == '\0') {
                        name = json_get_string(item, "track.name", NULL);
                    }
                    if (!name || name[0] == '\0') {
                        name = "Unknown";
                    }

                    /* Artist: try artists[0].name, then track.artists[0].name */
                    const char *artist = json_get_string(item, "artists[0].name", NULL);
                    if (!artist || artist[0] == '\0') {
                        artist = json_get_string(item, "track.artists[0].name", NULL);
                    }
                    if (!artist || artist[0] == '\0') {
                        artist = "Unknown Artist";
                    }

                    /* Only add if we have a name (valid track) */
                    queue_add(state, id && id[0] ? id : "", name, artist);
                    json_decref(item);
                }

                p = obj_end;
            }
            success = true;
            json_decref(json);
        }
    }

    http_response_free(resp);
    return success;
}

bool spotify_search(spotify_api_t *api, const char *query, int search_type,
                    search_result_t *results, int *result_count, int max_results)
{
    if (!api || !query || !results || !result_count) return false;
    if (!can_make_request(api)) return false;

    char url[1024];
    char *encoded_query = http_urlencode(query);
    if (!encoded_query) return false;

    const char *type_str = (search_type == 0) ? "track" : (search_type == 1) ? "artist" : "album";

    snprintf(url, sizeof(url), "%s/search?q=%s&type=%s&limit=%d", api->base_url, encoded_query, type_str, max_results);

    http_response_t *resp = api_http(api, HTTP_GET, url, NULL, NULL);
    http_free_encoded(encoded_query);

    if (!resp) return false;

    bool success = (resp->status_code == 200);
    if (success) *result_count = 0;

    http_response_free(resp);
    return success;
}

bool spotify_get_user_playlists(spotify_api_t *api, playlist_t *playlists, int *count, int max_count)
{
    if (!api || !playlists || !count) return false;
    if (!can_make_request(api)) return false;

    char url[1024];
    snprintf(url, sizeof(url), "%s/me/playlists?limit=%d", api->base_url, max_count);

    http_response_t *resp = api_http(api, HTTP_GET, url, NULL, NULL);
    if (!resp) return false;

    bool success = (resp->status_code == 200);
    if (success) *count = 0;

    http_response_free(resp);
    return success;
}

bool spotify_get_playlist_tracks(spotify_api_t *api, const char *playlist_id,
                                  queue_item_t *tracks, int *count, int max_count)
{
    if (!api || !playlist_id || !tracks || !count) return false;
    if (!can_make_request(api)) return false;

    char url[1024];
    snprintf(url, sizeof(url), "%s/playlists/%s/tracks?limit=%d", api->base_url, playlist_id, max_count);

    http_response_t *resp = api_http(api, HTTP_GET, url, NULL, NULL);
    if (!resp) return false;

    bool success = (resp->status_code == 200);
    if (success) *count = 0;

    http_response_free(resp);
    return success;
}

void spotify_api_free(spotify_api_t *api)
{
    if (!api) return;
    free(api);
}
