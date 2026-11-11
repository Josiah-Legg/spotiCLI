#include "api.h"
#include "http.h"
#include "json_util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static bool can_make_request(spotify_api_t *api)
{
    if (!api || !api->auth) return false;
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
    api->user_id[0] = '\0';

    return api;
}

/* Copy a json_get_string result safely (it shares one static buffer).
   Always reads via the indexed path then snapshots into `dst`. */
static void copy_json_str(char *dst, size_t dst_sz, json_t *j, const char *path)
{
    const char *s = json_get_string(j, path, "");
    if (!s) s = "";
    strncpy(dst, s, dst_sz - 1);
    dst[dst_sz - 1] = 0;
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
            /* Pull currently_playing.* off the top so we can prepend it as
               queue[0]. With it in slot 0, jump-to math
               (steps = sel - current_queue_index) is always correct. */
            char cp_id[256] = {0}, cp_name[512] = {0}, cp_artist[512] = {0};
            copy_json_str(cp_id,     sizeof(cp_id),     json, "currently_playing.id");
            copy_json_str(cp_name,   sizeof(cp_name),   json, "currently_playing.name");
            copy_json_str(cp_artist, sizeof(cp_artist), json, "currently_playing.artists[0].name");

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

            if (cp_name[0]) {
                queue_add(state, cp_id, cp_name, cp_artist);
            }
            state->current_queue_index = 0;

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
                    /* json_get_string returns a pointer into a single static
                       buffer, so each call invalidates the previous result.
                       Copy each field locally before the next lookup. */
                    char id_buf[256] = {0};
                    char name_buf[512] = {0};
                    char artist_buf[512] = {0};

                    const char *s = json_get_string(item, "id", "");
                    if (s) strncpy(id_buf, s, sizeof(id_buf) - 1);

                    s = json_get_string(item, "name", NULL);
                    if (!s || !s[0]) s = json_get_string(item, "track.name", NULL);
                    if (s && s[0]) strncpy(name_buf, s, sizeof(name_buf) - 1);
                    else           strncpy(name_buf, "Unknown", sizeof(name_buf) - 1);

                    s = json_get_string(item, "artists[0].name", NULL);
                    if (!s || !s[0]) s = json_get_string(item, "track.artists[0].name", NULL);
                    if (s && s[0]) strncpy(artist_buf, s, sizeof(artist_buf) - 1);
                    else           strncpy(artist_buf, "Unknown Artist", sizeof(artist_buf) - 1);

                    queue_add(state, id_buf, name_buf, artist_buf);
                    json_decref(item);
                }

                p = obj_end;
            }
            success = true;
            json_decref(json);
        }
    }

    if (state->queue_sidebar_sel >= state->queue_size)
        state->queue_sidebar_sel = state->queue_size > 0 ? state->queue_size - 1 : 0;
    if (state->queue_sidebar_sel < 0) state->queue_sidebar_sel = 0;

    http_response_free(resp);
    return success;
}

bool spotify_search(spotify_api_t *api, const char *query, int search_type,
                    search_result_t *results, int *result_count, int max_results)
{
    if (!api || !query || !results || !result_count) return false;
    if (!can_make_request(api)) return false;

    *result_count = 0;

    char url[1024];
    char *encoded_query = http_urlencode(query);
    if (!encoded_query) return false;

    const char *type_str;
    const char *items_path;
    switch (search_type) {
    case 0: type_str = "track";    items_path = "tracks.items";    break;
    case 1: type_str = "artist";   items_path = "artists.items";   break;
    case 2: type_str = "album";    items_path = "albums.items";    break;
    case 3: type_str = "playlist"; items_path = "playlists.items"; break;
    default: http_free_encoded(encoded_query); return false;
    }

    snprintf(url, sizeof(url), "%s/search?q=%s&type=%s&limit=%d",
             api->base_url, encoded_query, type_str, max_results);

    http_response_t *resp = api_http(api, HTTP_GET, url, NULL, NULL);
    http_free_encoded(encoded_query);

    if (!resp) return false;

    bool success = false;
    if (resp->status_code == 200 && resp->body && resp->body[0]) {
        json_t *j = json_parse_string(resp->body);
        if (j) {
            success = true;
            for (int i = 0; i < max_results; i++) {
                char path[160];
                snprintf(path, sizeof(path), "%s[%d].id", items_path, i);
                char id[256];
                copy_json_str(id, sizeof(id), j, path);
                if (!id[0]) break;

                search_result_t *r = &results[*result_count];
                memset(r, 0, sizeof(*r));
                strncpy(r->id, id, sizeof(r->id) - 1);

                snprintf(path, sizeof(path), "%s[%d].name", items_path, i);
                copy_json_str(r->name, sizeof(r->name), j, path);

                snprintf(path, sizeof(path), "%s[%d].uri", items_path, i);
                copy_json_str(r->uri, sizeof(r->uri), j, path);

                if (search_type == 0) {
                    /* track */
                    r->type = SR_TRACK;
                    snprintf(path, sizeof(path), "%s[%d].artists[0].name", items_path, i);
                    copy_json_str(r->artist, sizeof(r->artist), j, path);
                    snprintf(path, sizeof(path), "%s[%d].album.name", items_path, i);
                    copy_json_str(r->album, sizeof(r->album), j, path);
                } else if (search_type == 1) {
                    r->type = SR_ARTIST;
                } else if (search_type == 2) {
                    r->type = SR_ALBUM;
                    snprintf(path, sizeof(path), "%s[%d].artists[0].name", items_path, i);
                    copy_json_str(r->artist, sizeof(r->artist), j, path);
                } else {
                    r->type = SR_PLAYLIST;
                    snprintf(path, sizeof(path), "%s[%d].owner.display_name", items_path, i);
                    copy_json_str(r->artist, sizeof(r->artist), j, path);
                }

                (*result_count)++;
            }
            json_decref(j);
        }
    }

    http_response_free(resp);
    return success;
}

bool spotify_get_user_playlists(spotify_api_t *api, playlist_t *playlists, int *count, int max_count)
{
    if (!api || !playlists || !count) return false;
    if (!can_make_request(api)) return false;

    *count = 0;

    /* Make sure we know the current user's id so we can mark ownership. */
    spotify_get_current_user_id(api);

    /* Spotify caps the page size at 50 — anything higher returns 400. */
    int page_limit = max_count > 50 ? 50 : max_count;
    char url[1024];
    snprintf(url, sizeof(url), "%s/me/playlists?limit=%d", api->base_url, page_limit);

    http_response_t *resp = api_http(api, HTTP_GET, url, NULL, NULL);
    if (!resp) return false;

    bool success = false;
    if (resp->status_code == 200 && resp->body && resp->body[0]) {
        json_t *j = json_parse_string(resp->body);
        if (j) {
            success = true;
            for (int i = 0; i < max_count; i++) {
                char path[160];
                snprintf(path, sizeof(path), "items[%d].id", i);
                char id[256];
                copy_json_str(id, sizeof(id), j, path);
                if (!id[0]) break;

                playlist_t *pl = &playlists[*count];
                memset(pl, 0, sizeof(*pl));
                strncpy(pl->id, id, sizeof(pl->id) - 1);

                snprintf(path, sizeof(path), "items[%d].name", i);
                copy_json_str(pl->name, sizeof(pl->name), j, path);

                snprintf(path, sizeof(path), "items[%d].tracks.total", i);
                pl->track_count = json_get_int(j, path, 0);

                snprintf(path, sizeof(path), "items[%d].owner.id", i);
                char owner_id[256];
                copy_json_str(owner_id, sizeof(owner_id), j, path);
                pl->owned = (api->user_id[0] && strcmp(owner_id, api->user_id) == 0);

                (*count)++;
            }
            json_decref(j);
        }
    }

    http_response_free(resp);
    return success;
}

bool spotify_get_playlist_tracks(spotify_api_t *api, const char *playlist_id,
                                  queue_item_t *tracks, int *count, int max_count)
{
    if (!api || !playlist_id || !tracks || !count) return false;
    if (!can_make_request(api)) return false;

    *count = 0;

    int page_limit = max_count > 50 ? 50 : max_count;
    char url[1024];
    snprintf(url, sizeof(url), "%s/playlists/%s/tracks?limit=%d",
             api->base_url, playlist_id, page_limit);

    http_response_t *resp = api_http(api, HTTP_GET, url, NULL, NULL);
    if (!resp) return false;

    bool success = false;
    if (resp->status_code == 200 && resp->body && resp->body[0]) {
        json_t *j = json_parse_string(resp->body);
        if (j) {
            success = true;
            for (int i = 0; i < max_count; i++) {
                char path[160];
                snprintf(path, sizeof(path), "items[%d].track.id", i);
                char id[256];
                copy_json_str(id, sizeof(id), j, path);
                if (!id[0]) continue;

                queue_item_t *t = &tracks[*count];
                memset(t, 0, sizeof(*t));
                strncpy(t->id, id, sizeof(t->id) - 1);

                snprintf(path, sizeof(path), "items[%d].track.name", i);
                copy_json_str(t->name, sizeof(t->name), j, path);

                snprintf(path, sizeof(path), "items[%d].track.artists[0].name", i);
                copy_json_str(t->artist, sizeof(t->artist), j, path);

                (*count)++;
            }
            json_decref(j);
        }
    }

    http_response_free(resp);
    return success;
}

bool spotify_play_uri(spotify_api_t *api, const char *track_uri)
{
    if (!api || !track_uri) return false;
    if (!can_make_request(api)) return false;

    char url[1024];
    snprintf(url, sizeof(url), "%s/me/player/play", api->base_url);

    char body[512];
    snprintf(body, sizeof(body), "{\"uris\":[\"%s\"]}", track_uri);

    http_response_t *resp = api_http(api, HTTP_PUT, url, body, "application/json");
    if (!resp) return false;
    bool ok = (resp->status_code == 204 || resp->status_code == 200 || resp->status_code == 202);
    http_response_free(resp);
    return ok;
}

bool spotify_play_context(spotify_api_t *api, const char *context_uri, const char *offset_uri)
{
    if (!api || !context_uri) return false;
    if (!can_make_request(api)) return false;

    char url[1024];
    snprintf(url, sizeof(url), "%s/me/player/play", api->base_url);

    char body[1024];
    if (offset_uri && offset_uri[0]) {
        snprintf(body, sizeof(body),
                 "{\"context_uri\":\"%s\",\"offset\":{\"uri\":\"%s\"}}",
                 context_uri, offset_uri);
    } else {
        snprintf(body, sizeof(body), "{\"context_uri\":\"%s\"}", context_uri);
    }

    http_response_t *resp = api_http(api, HTTP_PUT, url, body, "application/json");
    if (!resp) return false;
    bool ok = (resp->status_code == 204 || resp->status_code == 200 || resp->status_code == 202);
    http_response_free(resp);
    return ok;
}

/* ---------- Library: saved tracks / albums / followed artists ---------- */

bool spotify_get_saved_tracks(spotify_api_t *api, queue_item_t *tracks, int *count, int max_count)
{
    if (!api || !tracks || !count) return false;
    if (!can_make_request(api)) return false;

    *count = 0;
    int page_limit = max_count > 50 ? 50 : max_count;
    char url[1024];
    snprintf(url, sizeof(url), "%s/me/tracks?limit=%d", api->base_url, page_limit);

    http_response_t *resp = api_http(api, HTTP_GET, url, NULL, NULL);
    if (!resp) return false;

    bool success = false;
    if (resp->status_code == 200 && resp->body && resp->body[0]) {
        json_t *j = json_parse_string(resp->body);
        if (j) {
            success = true;
            for (int i = 0; i < max_count; i++) {
                char path[160];
                snprintf(path, sizeof(path), "items[%d].track.id", i);
                char id[256];
                copy_json_str(id, sizeof(id), j, path);
                if (!id[0]) continue;

                queue_item_t *t = &tracks[*count];
                memset(t, 0, sizeof(*t));
                strncpy(t->id, id, sizeof(t->id) - 1);
                snprintf(path, sizeof(path), "items[%d].track.name", i);
                copy_json_str(t->name, sizeof(t->name), j, path);
                snprintf(path, sizeof(path), "items[%d].track.artists[0].name", i);
                copy_json_str(t->artist, sizeof(t->artist), j, path);
                (*count)++;
            }
            json_decref(j);
        }
    }
    http_response_free(resp);
    return success;
}

bool spotify_get_saved_albums(spotify_api_t *api, saved_album_t *albums, int *count, int max_count)
{
    if (!api || !albums || !count) return false;
    if (!can_make_request(api)) return false;

    *count = 0;
    int page_limit = max_count > 50 ? 50 : max_count;
    char url[1024];
    snprintf(url, sizeof(url), "%s/me/albums?limit=%d", api->base_url, page_limit);

    http_response_t *resp = api_http(api, HTTP_GET, url, NULL, NULL);
    if (!resp) return false;

    bool success = false;
    if (resp->status_code == 200 && resp->body && resp->body[0]) {
        json_t *j = json_parse_string(resp->body);
        if (j) {
            success = true;
            for (int i = 0; i < max_count; i++) {
                char path[160];
                snprintf(path, sizeof(path), "items[%d].album.id", i);
                char id[256];
                copy_json_str(id, sizeof(id), j, path);
                if (!id[0]) break;

                saved_album_t *a = &albums[*count];
                memset(a, 0, sizeof(*a));
                strncpy(a->id, id, sizeof(a->id) - 1);
                snprintf(path, sizeof(path), "items[%d].album.name", i);
                copy_json_str(a->name, sizeof(a->name), j, path);
                snprintf(path, sizeof(path), "items[%d].album.artists[0].name", i);
                copy_json_str(a->artist, sizeof(a->artist), j, path);
                (*count)++;
            }
            json_decref(j);
        }
    }
    http_response_free(resp);
    return success;
}

bool spotify_get_followed_artists(spotify_api_t *api, artist_t *artists, int *count, int max_count)
{
    if (!api || !artists || !count) return false;
    if (!can_make_request(api)) return false;

    *count = 0;
    /* /me/following caps at 50 per page. */
    int page_limit = max_count > 50 ? 50 : max_count;
    char url[1024];
    snprintf(url, sizeof(url), "%s/me/following?type=artist&limit=%d", api->base_url, page_limit);

    http_response_t *resp = api_http(api, HTTP_GET, url, NULL, NULL);
    if (!resp) return false;

    bool success = false;
    if (resp->status_code == 200 && resp->body && resp->body[0]) {
        json_t *j = json_parse_string(resp->body);
        if (j) {
            success = true;
            for (int i = 0; i < max_count; i++) {
                char path[160];
                snprintf(path, sizeof(path), "artists.items[%d].id", i);
                char id[256];
                copy_json_str(id, sizeof(id), j, path);
                if (!id[0]) break;

                artist_t *a = &artists[*count];
                memset(a, 0, sizeof(*a));
                strncpy(a->id, id, sizeof(a->id) - 1);
                snprintf(path, sizeof(path), "artists.items[%d].name", i);
                copy_json_str(a->name, sizeof(a->name), j, path);
                (*count)++;
            }
            json_decref(j);
        }
    }
    http_response_free(resp);
    return success;
}

bool spotify_check_track_saved(spotify_api_t *api, const char *track_id, bool *out_saved)
{
    if (!api || !track_id || !out_saved) return false;
    if (!can_make_request(api)) return false;

    *out_saved = false;
    char url[1024];
    snprintf(url, sizeof(url), "%s/me/tracks/contains?ids=%s", api->base_url, track_id);

    http_response_t *resp = api_http(api, HTTP_GET, url, NULL, NULL);
    if (!resp) return false;

    bool success = false;
    if (resp->status_code == 200 && resp->body && resp->body[0]) {
        /* Body is e.g. "[true]" or "[false]". */
        if (strstr(resp->body, "true")) *out_saved = true;
        success = true;
    }
    http_response_free(resp);
    return success;
}

static bool simple_put_delete(spotify_api_t *api, http_method_e method, const char *url)
{
    http_response_t *resp = api_http(api, method, url, NULL, NULL);
    if (!resp) return false;
    bool ok = (resp->status_code >= 200 && resp->status_code < 300);
    http_response_free(resp);
    return ok;
}

bool spotify_save_track(spotify_api_t *api, const char *track_id)
{
    if (!api || !track_id || !can_make_request(api)) return false;
    char url[1024];
    snprintf(url, sizeof(url), "%s/me/tracks?ids=%s", api->base_url, track_id);
    return simple_put_delete(api, HTTP_PUT, url);
}

bool spotify_unsave_track(spotify_api_t *api, const char *track_id)
{
    if (!api || !track_id || !can_make_request(api)) return false;
    char url[1024];
    snprintf(url, sizeof(url), "%s/me/tracks?ids=%s", api->base_url, track_id);
    return simple_put_delete(api, HTTP_DELETE, url);
}

bool spotify_save_album(spotify_api_t *api, const char *album_id)
{
    if (!api || !album_id || !can_make_request(api)) return false;
    char url[1024];
    snprintf(url, sizeof(url), "%s/me/albums?ids=%s", api->base_url, album_id);
    return simple_put_delete(api, HTTP_PUT, url);
}

bool spotify_unsave_album(spotify_api_t *api, const char *album_id)
{
    if (!api || !album_id || !can_make_request(api)) return false;
    char url[1024];
    snprintf(url, sizeof(url), "%s/me/albums?ids=%s", api->base_url, album_id);
    return simple_put_delete(api, HTTP_DELETE, url);
}

bool spotify_follow_artist(spotify_api_t *api, const char *artist_id)
{
    if (!api || !artist_id || !can_make_request(api)) return false;
    char url[1024];
    snprintf(url, sizeof(url), "%s/me/following?type=artist&ids=%s", api->base_url, artist_id);
    return simple_put_delete(api, HTTP_PUT, url);
}

bool spotify_unfollow_artist(spotify_api_t *api, const char *artist_id)
{
    if (!api || !artist_id || !can_make_request(api)) return false;
    char url[1024];
    snprintf(url, sizeof(url), "%s/me/following?type=artist&ids=%s", api->base_url, artist_id);
    return simple_put_delete(api, HTTP_DELETE, url);
}

/* ---------- Album / artist drill-in ---------- */

bool spotify_get_album_tracks(spotify_api_t *api, const char *album_id,
                              queue_item_t *tracks, int *count, int max_count)
{
    if (!api || !album_id || !tracks || !count) return false;
    if (!can_make_request(api)) return false;

    *count = 0;
    int page_limit = max_count > 50 ? 50 : max_count;
    char url[1024];
    snprintf(url, sizeof(url), "%s/albums/%s/tracks?limit=%d",
             api->base_url, album_id, page_limit);

    http_response_t *resp = api_http(api, HTTP_GET, url, NULL, NULL);
    if (!resp) return false;

    bool success = false;
    if (resp->status_code == 200 && resp->body && resp->body[0]) {
        json_t *j = json_parse_string(resp->body);
        if (j) {
            success = true;
            for (int i = 0; i < max_count; i++) {
                char path[160];
                snprintf(path, sizeof(path), "items[%d].id", i);
                char id[256];
                copy_json_str(id, sizeof(id), j, path);
                if (!id[0]) break;

                queue_item_t *t = &tracks[*count];
                memset(t, 0, sizeof(*t));
                strncpy(t->id, id, sizeof(t->id) - 1);
                snprintf(path, sizeof(path), "items[%d].name", i);
                copy_json_str(t->name, sizeof(t->name), j, path);
                snprintf(path, sizeof(path), "items[%d].artists[0].name", i);
                copy_json_str(t->artist, sizeof(t->artist), j, path);
                (*count)++;
            }
            json_decref(j);
        }
    }
    http_response_free(resp);
    return success;
}

bool spotify_get_artist_top_tracks(spotify_api_t *api, const char *artist_id,
                                    queue_item_t *tracks, int *count, int max_count)
{
    if (!api || !artist_id || !tracks || !count) return false;
    if (!can_make_request(api)) return false;

    *count = 0;
    char url[1024];
    snprintf(url, sizeof(url), "%s/artists/%s/top-tracks?market=from_token",
             api->base_url, artist_id);

    http_response_t *resp = api_http(api, HTTP_GET, url, NULL, NULL);
    if (!resp) return false;

    bool success = false;
    if (resp->status_code == 200 && resp->body && resp->body[0]) {
        json_t *j = json_parse_string(resp->body);
        if (j) {
            success = true;
            for (int i = 0; i < max_count; i++) {
                char path[160];
                snprintf(path, sizeof(path), "tracks[%d].id", i);
                char id[256];
                copy_json_str(id, sizeof(id), j, path);
                if (!id[0]) break;

                queue_item_t *t = &tracks[*count];
                memset(t, 0, sizeof(*t));
                strncpy(t->id, id, sizeof(t->id) - 1);
                snprintf(path, sizeof(path), "tracks[%d].name", i);
                copy_json_str(t->name, sizeof(t->name), j, path);
                snprintf(path, sizeof(path), "tracks[%d].artists[0].name", i);
                copy_json_str(t->artist, sizeof(t->artist), j, path);
                (*count)++;
            }
            json_decref(j);
        }
    }
    http_response_free(resp);
    return success;
}

/* ---------- Playlist editing ---------- */

bool spotify_get_current_user_id(spotify_api_t *api)
{
    if (!api || !can_make_request(api)) return false;
    if (api->user_id[0]) return true;

    char url[1024];
    snprintf(url, sizeof(url), "%s/me", api->base_url);
    http_response_t *resp = api_http(api, HTTP_GET, url, NULL, NULL);
    if (!resp) return false;

    bool success = false;
    if (resp->status_code == 200 && resp->body && resp->body[0]) {
        json_t *j = json_parse_string(resp->body);
        if (j) {
            copy_json_str(api->user_id, sizeof(api->user_id), j, "id");
            success = api->user_id[0] != '\0';
            json_decref(j);
        }
    }
    http_response_free(resp);
    return success;
}

bool spotify_create_playlist(spotify_api_t *api, const char *name, char *out_id, int out_id_sz)
{
    if (!api || !name || !out_id || out_id_sz < 1) return false;
    if (!can_make_request(api)) return false;
    if (!spotify_get_current_user_id(api)) return false;

    char url[1024];
    snprintf(url, sizeof(url), "%s/users/%s/playlists", api->base_url, api->user_id);

    char body[1024];
    snprintf(body, sizeof(body),
             "{\"name\":\"%s\",\"public\":false,\"description\":\"\"}", name);

    http_response_t *resp = api_http(api, HTTP_POST, url, body, "application/json");
    if (!resp) return false;

    bool success = false;
    out_id[0] = 0;
    if ((resp->status_code == 200 || resp->status_code == 201) && resp->body && resp->body[0]) {
        json_t *j = json_parse_string(resp->body);
        if (j) {
            const char *s = json_get_string(j, "id", "");
            if (s && s[0]) {
                strncpy(out_id, s, out_id_sz - 1);
                out_id[out_id_sz - 1] = 0;
                success = true;
            }
            json_decref(j);
        }
    }
    http_response_free(resp);
    return success;
}

bool spotify_rename_playlist(spotify_api_t *api, const char *playlist_id, const char *new_name)
{
    if (!api || !playlist_id || !new_name) return false;
    if (!can_make_request(api)) return false;

    char url[1024];
    snprintf(url, sizeof(url), "%s/playlists/%s", api->base_url, playlist_id);
    char body[1024];
    snprintf(body, sizeof(body), "{\"name\":\"%s\"}", new_name);

    http_response_t *resp = api_http(api, HTTP_PUT, url, body, "application/json");
    if (!resp) return false;
    bool ok = (resp->status_code >= 200 && resp->status_code < 300);
    http_response_free(resp);
    return ok;
}

bool spotify_unfollow_playlist(spotify_api_t *api, const char *playlist_id)
{
    if (!api || !playlist_id) return false;
    if (!can_make_request(api)) return false;

    char url[1024];
    snprintf(url, sizeof(url), "%s/playlists/%s/followers", api->base_url, playlist_id);
    return simple_put_delete(api, HTTP_DELETE, url);
}

bool spotify_add_track_to_playlist(spotify_api_t *api, const char *playlist_id, const char *track_uri)
{
    if (!api || !playlist_id || !track_uri) return false;
    if (!can_make_request(api)) return false;

    char url[1024];
    snprintf(url, sizeof(url), "%s/playlists/%s/tracks", api->base_url, playlist_id);
    char body[512];
    snprintf(body, sizeof(body), "{\"uris\":[\"%s\"]}", track_uri);

    http_response_t *resp = api_http(api, HTTP_POST, url, body, "application/json");
    if (!resp) return false;
    bool ok = (resp->status_code >= 200 && resp->status_code < 300);
    http_response_free(resp);
    return ok;
}

bool spotify_remove_track_from_playlist(spotify_api_t *api, const char *playlist_id, const char *track_uri)
{
    if (!api || !playlist_id || !track_uri) return false;
    if (!can_make_request(api)) return false;

    char url[1024];
    snprintf(url, sizeof(url), "%s/playlists/%s/tracks", api->base_url, playlist_id);
    char body[512];
    snprintf(body, sizeof(body), "{\"tracks\":[{\"uri\":\"%s\"}]}", track_uri);

    http_response_t *resp = api_http(api, HTTP_DELETE, url, body, "application/json");
    if (!resp) return false;
    bool ok = (resp->status_code >= 200 && resp->status_code < 300);
    http_response_free(resp);
    return ok;
}

bool spotify_reorder_playlist_track(spotify_api_t *api, const char *playlist_id,
                                     int range_start, int insert_before)
{
    if (!api || !playlist_id) return false;
    if (!can_make_request(api)) return false;

    char url[1024];
    snprintf(url, sizeof(url), "%s/playlists/%s/tracks", api->base_url, playlist_id);
    char body[256];
    snprintf(body, sizeof(body),
             "{\"range_start\":%d,\"insert_before\":%d,\"range_length\":1}",
             range_start, insert_before);

    http_response_t *resp = api_http(api, HTTP_PUT, url, body, "application/json");
    if (!resp) return false;
    bool ok = (resp->status_code >= 200 && resp->status_code < 300);
    http_response_free(resp);
    return ok;
}

void spotify_api_free(spotify_api_t *api)
{
    if (!api) return;
    free(api);
}
