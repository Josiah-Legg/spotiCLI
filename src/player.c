#include "player.h"
#include "input.h"
#include "tui.h"
#include "modes.h"
#include "album_art.h"
#include "http.h"
#include "lyrics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(int ms) { Sleep(ms); }
#else
#include <unistd.h>
#include <pthread.h>
static void sleep_ms(int ms) { usleep(ms * 1000); }
#endif

/* Thread arguments for async album art download */
typedef struct {
    char cover_url[1024];
    char access_token[2048];
    int art_w;
    int art_h;
    player_state_t *state;
} art_download_args_t;

/* Thread function to download album art asynchronously */
#ifdef _WIN32
static DWORD WINAPI art_download_thread(LPVOID arg)
#else
static void *art_download_thread(void *arg)
#endif
{
    art_download_args_t *args = (art_download_args_t *)arg;
    if (!args || !args->state) {
#ifdef _WIN32
        return 1;
#else
        return NULL;
#endif
    }

    fprintf(stderr, "[art] Starting async download for: %s\n", args->cover_url);

    /* Download once, render at both the full (64x32) and small (32x16) sizes
       so the hybrid mode has its own lower-res variant without a second fetch. */
    http_binary_response_t *bin = http_download_binary(args->cover_url, args->access_token);
    char *art_full = NULL;
    char *art_small = NULL;
    char *art_tiny = NULL;

    if (bin && bin->data && bin->size > 0) {
        album_art_config_t *cfg_full  = album_art_config_create(args->art_w, true);
        album_art_config_t *cfg_small = album_art_config_create(32, true);
        album_art_config_t *cfg_tiny  = album_art_config_create(10, true);

        uint8_t *img_full  = album_art_decode_resize(
            (const uint8_t *)bin->data, bin->size, args->art_w, args->art_h);
        uint8_t *img_small = album_art_decode_resize(
            (const uint8_t *)bin->data, bin->size, 32, 16);
        uint8_t *img_tiny  = album_art_decode_resize(
            (const uint8_t *)bin->data, bin->size, 10, 5);

        if (cfg_full && img_full) {
            art_full = album_art_convert(img_full, args->art_w, args->art_h, cfg_full);
        }
        if (cfg_small && img_small) {
            art_small = album_art_convert(img_small, 32, 16, cfg_small);
        }
        if (cfg_tiny && img_tiny) {
            art_tiny = album_art_convert(img_tiny, 10, 5, cfg_tiny);
        }

        free(img_full);
        free(img_small);
        free(img_tiny);
        album_art_config_free(cfg_full);
        album_art_config_free(cfg_small);
        album_art_config_free(cfg_tiny);
    }
    if (bin) http_binary_response_free(bin);

    if (art_full) {
        fprintf(stderr, "[art] Download complete, updating state\n");

#ifdef _WIN32
        WaitForSingleObject(args->state->art_mutex, INFINITE);
#else
        pthread_mutex_lock(&args->state->art_mutex);
#endif

        strncpy(args->state->pending_album_ascii, art_full,
                sizeof(args->state->pending_album_ascii) - 1);
        args->state->pending_album_ascii[sizeof(args->state->pending_album_ascii) - 1] = '\0';

        if (art_small) {
            strncpy(args->state->pending_album_ascii_small, art_small,
                    sizeof(args->state->pending_album_ascii_small) - 1);
            args->state->pending_album_ascii_small[sizeof(args->state->pending_album_ascii_small) - 1] = '\0';
        } else {
            args->state->pending_album_ascii_small[0] = '\0';
        }

        if (art_tiny) {
            strncpy(args->state->pending_album_ascii_tiny, art_tiny,
                    sizeof(args->state->pending_album_ascii_tiny) - 1);
            args->state->pending_album_ascii_tiny[sizeof(args->state->pending_album_ascii_tiny) - 1] = '\0';
        } else {
            args->state->pending_album_ascii_tiny[0] = '\0';
        }

        args->state->art_download_complete = true;
        args->state->art_download_pending = false;

#ifdef _WIN32
        ReleaseMutex(args->state->art_mutex);
#else
        pthread_mutex_unlock(&args->state->art_mutex);
#endif
    } else {
        fprintf(stderr, "[art] Download failed\n");
        args->state->art_download_pending = false;
    }

    free(art_full);
    free(art_small);
    free(art_tiny);
    free(args);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

player_t *player_create(player_state_t *state, spotify_api_t *api)
{
    player_t *p = (player_t *)malloc(sizeof(player_t));
    if (!p) return NULL;
    p->state = state;
    p->api = api;
    p->running = true;
    return p;
}

void player_free(player_t *p) { if (p) free(p); }

void player_toggle_play(player_t *player)
{
    if (!player) return;
    if (player->state->playback_state == PLAYBACK_STATE_PLAYING)
        player->state->playback_state = PLAYBACK_STATE_PAUSED;
    else
        player->state->playback_state = PLAYBACK_STATE_PLAYING;
}

void player_next_track(player_t *p)
{
    if (!p || p->state->queue_size == 0) return;
    if (p->state->current_queue_index < p->state->queue_size - 1) {
        p->state->current_queue_index++;
        p->state->playback_state = PLAYBACK_STATE_PLAYING;
    }
}

void player_prev_track(player_t *p)
{
    if (!p || p->state->queue_size == 0) return;
    if (p->state->current_queue_index > 0) {
        p->state->current_queue_index--;
        p->state->playback_state = PLAYBACK_STATE_PLAYING;
    }
}

void player_change_mode(player_t *p, display_mode_e mode)
{ if (p) p->state->display_mode = mode; }

void player_toggle_queue(player_t *p)
{ if (p) p->state->queue_sidebar_visible = !p->state->queue_sidebar_visible; }

void player_queue_up(player_t *p)
{
    if (!p) return;
    if (p->state->current_queue_index > 0) p->state->current_queue_index--;
}

void player_queue_down(player_t *p)
{
    if (!p) return;
    if (p->state->current_queue_index < p->state->queue_size - 1)
        p->state->current_queue_index++;
}

void player_volume_up(player_t *p)
{ if (p && p->state->volume < 100) p->state->volume += 5; }

void player_volume_down(player_t *p)
{ if (p && p->state->volume > 0) p->state->volume -= 5; }

/* Refresh the procedural album-art placeholder when the track changes.
   Sized for the album panel in modes.c (worst case: full width minus
   the queue sidebar). */
static void refresh_art_for_track(player_state_t *state, int width, int height)
{
    char key[1024];
    snprintf(key, sizeof(key), "%s|%s",
             state->current_track.album, state->current_track.name);

    static char last_key[1024] = {0};
    static int last_w = -1, last_h = -1;
    if (strcmp(last_key, key) == 0 && last_w == width && last_h == height) return;
    snprintf(last_key, sizeof(last_key), "%s", key);
    last_w = width;
    last_h = height;

    album_art_generate_placeholder(state->album_ascii, sizeof(state->album_ascii),
                                   width, height, key);
    album_art_generate_placeholder(state->album_ascii_small, sizeof(state->album_ascii_small),
                                   32, 16, key);
    album_art_generate_placeholder(state->album_ascii_tiny, sizeof(state->album_ascii_tiny),
                                   10, 5, key);
    state->album_width = width;
    state->album_height = height;
}

/* Start async download of album art (non-blocking) */
static void start_art_download(player_state_t *state, const char *url, const char *token, int w, int h)
{
    if (state->art_download_pending) return;  /* Already downloading */
    if (!url || !url[0]) return;  /* No URL */

    fprintf(stderr, "[art] Spawning download thread for: %s\n", url);

    art_download_args_t *args = (art_download_args_t *)malloc(sizeof(art_download_args_t));
    if (!args) return;

    strncpy(args->cover_url, url, sizeof(args->cover_url) - 1);
    args->cover_url[sizeof(args->cover_url) - 1] = '\0';
    strncpy(args->access_token, token, sizeof(args->access_token) - 1);
    args->access_token[sizeof(args->access_token) - 1] = '\0';
    args->art_w = w;
    args->art_h = h;
    args->state = state;

    state->art_download_pending = true;
    state->art_download_complete = false;

#ifdef _WIN32
    HANDLE thread = CreateThread(NULL, 0, art_download_thread, args, 0, NULL);
    if (thread) {
        state->art_thread = thread;
    } else {
        state->art_download_pending = false;
        free(args);
    }
#else
    if (pthread_create(&state->art_thread, NULL, art_download_thread, args) != 0) {
        state->art_download_pending = false;
        free(args);
    }
#endif
}

/* Check if async download completed and apply it */
static void check_art_download_complete(player_state_t *state)
{
    if (!state->art_download_complete) return;

#ifdef _WIN32
    WaitForSingleObject(state->art_mutex, INFINITE);
#else
    pthread_mutex_lock(&state->art_mutex);
#endif

    if (state->art_download_complete && state->pending_album_ascii[0]) {
        fprintf(stderr, "[art] Applying downloaded art\n");
        strncpy(state->album_ascii, state->pending_album_ascii, sizeof(state->album_ascii) - 1);
        state->album_ascii[sizeof(state->album_ascii) - 1] = '\0';

        if (state->pending_album_ascii_small[0]) {
            strncpy(state->album_ascii_small, state->pending_album_ascii_small,
                    sizeof(state->album_ascii_small) - 1);
            state->album_ascii_small[sizeof(state->album_ascii_small) - 1] = '\0';
            state->pending_album_ascii_small[0] = '\0';
        }
        if (state->pending_album_ascii_tiny[0]) {
            strncpy(state->album_ascii_tiny, state->pending_album_ascii_tiny,
                    sizeof(state->album_ascii_tiny) - 1);
            state->album_ascii_tiny[sizeof(state->album_ascii_tiny) - 1] = '\0';
            state->pending_album_ascii_tiny[0] = '\0';
        }

        state->art_download_complete = false;
        state->pending_album_ascii[0] = '\0';
    }

#ifdef _WIN32
    ReleaseMutex(state->art_mutex);
#else
    pthread_mutex_unlock(&state->art_mutex);
#endif
}

void player_run(player_t *player)
{
    if (!player) return;

    input_init();

    /* Redirect stderr to a log file so api.c's chatter doesn't corrupt
       the live TUI. Useful for postmortem debugging too. */
    {
        char log_path[1024];
#ifdef _WIN32
        const char *tmp = getenv("TEMP");
        snprintf(log_path, sizeof(log_path), "%s\\spoticli.log",
                 tmp ? tmp : ".");
#else
        snprintf(log_path, sizeof(log_path), "/tmp/spoticli.log");
#endif
        freopen(log_path, "w", stderr);
        setvbuf(stderr, NULL, _IOLBF, 0);
    }

    tui_init();

    int W = 120, H = 40;
    tui_get_size(&W, &H);

    /* Album art dimensions: 64 chars wide x 32 lines tall (appears square) */
    int art_w = 64;
    int art_h = 32;

    /* Fetch initial state and generate placeholder art */
    spotify_get_current_playback(player->api, player->state);

    /* Generate initial art placeholder */
    {
        char key[1024];
        snprintf(key, sizeof(key), "%s|%s",
                 player->state->current_track.album[0] ? player->state->current_track.album : "Demo Album",
                 player->state->current_track.name[0] ? player->state->current_track.name : "Demo Track");
        album_art_generate_placeholder(player->state->album_ascii, sizeof(player->state->album_ascii),
                                       art_w, art_h, key);
        album_art_generate_placeholder(player->state->album_ascii_small, sizeof(player->state->album_ascii_small),
                                       32, 16, key);
        album_art_generate_placeholder(player->state->album_ascii_tiny, sizeof(player->state->album_ascii_tiny),
                                       10, 5, key);
    }

    int update_counter = 0;
    const int UPDATE_INTERVAL = 20; /* ~1s at 50ms/frame - faster response */

    mode_renderer_t *renderer = mode_renderer_create(W, H);

    while (player->running) {
        tui_get_size(&W, &H);
        renderer->screen_width = W;
        renderer->screen_height = H;

        /* Fixed album art size: 64x32 for square appearance */
        int art_w = 64;
        int art_h = 32;
        refresh_art_for_track(player->state, art_w, art_h);

        tui_clear();
        mode_render(renderer, player->state);
        tui_flush();

        update_counter++;
        if (update_counter >= UPDATE_INTERVAL) {
            /* Check if async album art download completed */
            check_art_download_complete(player->state);

            if (spotify_get_current_playback(player->api, player->state)) {
                /* Trigger a lyrics fetch if the track changed */
                static char last_track_id[256] = {0};
                if (strcmp(last_track_id, player->state->current_track.id) != 0) {
                    /* Track changed - immediately show placeholder */
                    char key[1024];
                    snprintf(key, sizeof(key), "%s|%s",
                             player->state->current_track.album[0] ? player->state->current_track.album : "Album",
                             player->state->current_track.name[0] ? player->state->current_track.name : "Track");
                    album_art_generate_placeholder(player->state->album_ascii,
                                                   sizeof(player->state->album_ascii),
                                                   art_w, art_h, key);
                    album_art_generate_placeholder(player->state->album_ascii_small,
                                                   sizeof(player->state->album_ascii_small),
                                                   32, 16, key);
                    album_art_generate_placeholder(player->state->album_ascii_tiny,
                                                   sizeof(player->state->album_ascii_tiny),
                                                   10, 5, key);

                    lyrics_fetch(player->state->current_track.id,
                                 player->state->current_track.name,
                                 player->state->current_track.artist,
                                 player->state);
                    strncpy(last_track_id, player->state->current_track.id, sizeof(last_track_id)-1);

                    /* Start async album art download (non-blocking) */
                    if (player->state->current_track.cover_url[0]) {
                        start_art_download(player->state,
                                          player->state->current_track.cover_url,
                                          player->api->auth->token.access_token,
                                          art_w, art_h);
                    }
                }
            }
            /* Fetch queue periodically */
            spotify_get_queue(player->api, player->state);
            update_counter = 0;
        }

        if (player->state->playback_state == PLAYBACK_STATE_PLAYING) {
            if (player->state->current_track.progress_ms < player->state->current_track.duration_ms) {
                player->state->current_track.progress_ms += 50;
            }
        }

        input_event_t *evt = input_poll();
        if (evt) {
            if (evt->type == INPUT_QUIT) {
                break;
            } else if (evt->type == INPUT_KEY) {
                switch (evt->special_key) {
                case INPUT_KEY_ESCAPE:
                    player->running = false;
                    break;
                case INPUT_KEY_UP:
                    player_queue_up(player);
                    break;
                case INPUT_KEY_DOWN:
                    player_queue_down(player);
                    break;
                case INPUT_KEY_LEFT: {
                    int p = player->state->current_track.progress_ms - 5000;
                    if (p < 0) p = 0;
                    spotify_seek(player->api, player->state, p);
                    player->state->current_track.progress_ms = p;
                    break;
                }
                case INPUT_KEY_RIGHT: {
                    int p = player->state->current_track.progress_ms + 5000;
                    if (p > player->state->current_track.duration_ms)
                        p = player->state->current_track.duration_ms;
                    spotify_seek(player->api, player->state, p);
                    player->state->current_track.progress_ms = p;
                    break;
                }
                default: break;
                }
            } else if (evt->type == INPUT_CHAR) {
                switch (evt->ch) {
                case ' ':
                    if (player->state->playback_state == PLAYBACK_STATE_PLAYING) {
                        spotify_pause(player->api, player->state);
                        player->state->playback_state = PLAYBACK_STATE_PAUSED;
                    } else {
                        spotify_play(player->api, player->state);
                        player->state->playback_state = PLAYBACK_STATE_PLAYING;
                    }
                    break;
                case 'n': case 'N':
                    spotify_next(player->api, player->state);
                    player->state->needs_redraw = true;
                    break;
                case 'p': case 'P':
                    spotify_previous(player->api, player->state);
                    player->state->needs_redraw = true;
                    break;
                case 'q': case 'Q':
                    player_toggle_queue(player);
                    break;
                case 's': case 'S':
                    spotify_set_shuffle(player->api, player->state,
                                       !player->state->shuffle_enabled);
                    break;
                case 'r': case 'R': {
                    repeat_mode_e m = (repeat_mode_e)((player->state->repeat_mode + 1) % 3);
                    spotify_set_repeat(player->api, player->state, m);
                    break;
                }
                case '1': player_change_mode(player, MODE_ALBUM_ART); break;
                case '2': player_change_mode(player, MODE_HYBRID);    break;
                case '3': player_change_mode(player, MODE_LYRICS);    break;
                case '+': case '=':
                    if (player->state->volume < 100) {
                        player->state->volume += 5;
                        spotify_set_volume(player->api, player->state, player->state->volume);
                    }
                    break;
                case '-': case '_':
                    if (player->state->volume > 0) {
                        player->state->volume -= 5;
                        spotify_set_volume(player->api, player->state, player->state->volume);
                    }
                    break;
                default: break;
                }
            }
        }

        sleep_ms(50);
    }

    mode_renderer_free(renderer);
    tui_cleanup();
    input_cleanup();
}
