#include "player.h"
#include "input.h"
#include "tui.h"
#include "modes.h"
#include "album_art.h"
#include "http.h"
#include "lyrics.h"
#include "search.h"
#include "library.h"
#include "playlists_ui.h"
/* Queue screen removed — functionality covered by Search + Now Playing sidebar */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(int ms) { Sleep(ms); }
static long long now_ms(void) {
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    long long t = ((long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return t / 10000;
}
#else
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
static void sleep_ms(int ms) { usleep(ms * 1000); }
static long long now_ms(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}
#endif

/* Thread arguments for async album art download */
typedef struct {
    char cover_url[1024];
    char access_token[2048];
    int art_w;
    int art_h;
    player_state_t *state;
} art_download_args_t;

/* Thread arguments for async lyrics fetch */
typedef struct {
    char track_id[256];
    char track_name[512];
    char artist_name[512];
    char album_name[512];
    int  duration_ms;
    player_state_t *state;
} lyrics_fetch_args_t;

#ifdef _WIN32
static DWORD WINAPI lyrics_fetch_thread(LPVOID arg)
#else
static void *lyrics_fetch_thread(void *arg)
#endif
{
    lyrics_fetch_args_t *args = (lyrics_fetch_args_t *)arg;
    if (!args || !args->state) {
#ifdef _WIN32
        return 1;
#else
        return NULL;
#endif
    }

    player_state_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.lyrics_capacity = 100;
    tmp.lyrics = (lyric_line_t *)malloc(tmp.lyrics_capacity * sizeof(lyric_line_t));
    if (!tmp.lyrics) {
        args->state->lyrics_fetch_pending = false;
        free(args);
#ifdef _WIN32
        return 1;
#else
        return NULL;
#endif
    }
    strncpy(tmp.current_track.album, args->album_name, sizeof(tmp.current_track.album) - 1);
    tmp.current_track.duration_ms = args->duration_ms;

    lyrics_fetch(args->track_id, args->track_name, args->artist_name, &tmp);

#ifdef _WIN32
    WaitForSingleObject(args->state->lyrics_mutex, INFINITE);
#else
    pthread_mutex_lock(&args->state->lyrics_mutex);
#endif

    free(args->state->pending_lyrics);
    args->state->pending_lyrics          = tmp.lyrics;
    args->state->pending_lyrics_count    = tmp.lyrics_count;
    args->state->pending_lyrics_capacity = tmp.lyrics_capacity;
    strncpy(args->state->pending_lyrics_track_id, args->track_id,
            sizeof(args->state->pending_lyrics_track_id) - 1);
    args->state->pending_lyrics_track_id[sizeof(args->state->pending_lyrics_track_id) - 1] = '\0';
    args->state->lyrics_fetch_complete   = true;
    args->state->lyrics_fetch_pending    = false;

#ifdef _WIN32
    ReleaseMutex(args->state->lyrics_mutex);
#else
    pthread_mutex_unlock(&args->state->lyrics_mutex);
#endif

    free(args);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static void start_lyrics_fetch(player_state_t *state,
                               const char *track_id, const char *track_name,
                               const char *artist_name, const char *album_name,
                               int duration_ms)
{
    if (state->lyrics_fetch_pending) return;
    if (!track_name || !track_name[0]) return;

    lyrics_fetch_args_t *args = (lyrics_fetch_args_t *)malloc(sizeof(lyrics_fetch_args_t));
    if (!args) return;

    strncpy(args->track_id,    track_id    ? track_id    : "", sizeof(args->track_id)    - 1);
    args->track_id[sizeof(args->track_id) - 1] = 0;
    strncpy(args->track_name,  track_name,  sizeof(args->track_name)  - 1);
    args->track_name[sizeof(args->track_name) - 1] = 0;
    strncpy(args->artist_name, artist_name ? artist_name : "", sizeof(args->artist_name) - 1);
    args->artist_name[sizeof(args->artist_name) - 1] = 0;
    strncpy(args->album_name,  album_name  ? album_name  : "", sizeof(args->album_name)  - 1);
    args->album_name[sizeof(args->album_name) - 1] = 0;
    args->duration_ms = duration_ms;
    args->state = state;

    state->lyrics_fetch_pending  = true;
    state->lyrics_fetch_complete = false;

#ifdef _WIN32
    if (state->lyrics_thread) {
        WaitForSingleObject(state->lyrics_thread, 0);
        CloseHandle(state->lyrics_thread);
        state->lyrics_thread = NULL;
    }
    HANDLE t = CreateThread(NULL, 0, lyrics_fetch_thread, args, 0, NULL);
    if (t) state->lyrics_thread = t;
    else { state->lyrics_fetch_pending = false; free(args); }
#else
    if (state->lyrics_thread) {
        pthread_join(state->lyrics_thread, NULL);
        state->lyrics_thread = 0;
    }
    if (pthread_create(&state->lyrics_thread, NULL, lyrics_fetch_thread, args) != 0) {
        state->lyrics_fetch_pending = false;
        free(args);
    }
#endif
}

typedef struct {
    player_t *player;
    volatile bool *running;
} api_poller_args_t;

#ifdef _WIN32
static DWORD WINAPI api_poller_thread(LPVOID arg)
#else
static void *api_poller_thread(void *arg)
#endif
{
    api_poller_args_t *a = (api_poller_args_t *)arg;
    while (a->player && *a->running) {
        spotify_get_current_playback(a->player->api, a->player->state);
        spotify_get_queue(a->player->api, a->player->state);
        for (int i = 0; i < 20 && *a->running; i++) sleep_ms(50);
    }
    free(a);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static void check_lyrics_fetch_complete(player_state_t *state)
{
    if (!state->lyrics_fetch_complete) return;

#ifdef _WIN32
    WaitForSingleObject(state->lyrics_mutex, INFINITE);
#else
    pthread_mutex_lock(&state->lyrics_mutex);
#endif

    if (state->lyrics_fetch_complete && state->pending_lyrics) {
        /* Drop results for a track the user has already moved past — e.g.
           by jumping in the Up Next list while a fetch was still in flight. */
        bool stale = strcmp(state->pending_lyrics_track_id,
                            state->current_track.id) != 0;
        if (stale) {
            free(state->pending_lyrics);
        } else {
            free(state->lyrics);
            state->lyrics             = state->pending_lyrics;
            state->lyrics_count       = state->pending_lyrics_count;
            state->lyrics_capacity    = state->pending_lyrics_capacity;
            state->current_lyric_index = 0;
            strncpy(state->lyrics_track_id, state->pending_lyrics_track_id,
                    sizeof(state->lyrics_track_id) - 1);
            state->lyrics_track_id[sizeof(state->lyrics_track_id) - 1] = '\0';
        }

        state->pending_lyrics          = NULL;
        state->pending_lyrics_count    = 0;
        state->pending_lyrics_capacity = 0;
        state->pending_lyrics_track_id[0] = '\0';
        state->lyrics_fetch_complete   = false;
    }

#ifdef _WIN32
    ReleaseMutex(state->lyrics_mutex);
#else
    pthread_mutex_unlock(&state->lyrics_mutex);
#endif
}

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
    if (p->state->queue_sidebar_sel > 0) p->state->queue_sidebar_sel--;
}

void player_queue_down(player_t *p)
{
    if (!p) return;
    if (p->state->queue_sidebar_sel < p->state->queue_size - 1)
        p->state->queue_sidebar_sel++;
}

static void player_queue_jump_to_sel(player_t *p)
{
    if (!p) return;
    int target = p->state->queue_sidebar_sel;
    int cur    = p->state->current_queue_index;
    int steps  = target - cur;
    if (steps <= 0) return;
    if (target >= p->state->queue_size) return;

    /* Pause and mute before skipping so the skipped songs aren't briefly
       audible AND the target song doesn't begin playing while still muted
       (which would clip its opening). Both are restored from the main loop
       once current_track matches the target. Don't overwrite a save already
       in progress. */
    if (!p->state->jump_in_progress) {
        p->state->jump_saved_volume = p->state->volume;
        p->state->jump_was_playing  =
            (p->state->playback_state == PLAYBACK_STATE_PLAYING);
        strncpy(p->state->jump_target_track_id,
                p->state->queue[target].id,
                sizeof(p->state->jump_target_track_id) - 1);
        p->state->jump_target_track_id[sizeof(p->state->jump_target_track_id) - 1] = '\0';
        p->state->jump_in_progress = true;
        spotify_pause(p->api, p->state);
        spotify_set_volume(p->api, p->state, 0);
    } else {
        /* User extended the jump while a previous one was still resolving —
           update the target without touching the saved volume. */
        strncpy(p->state->jump_target_track_id,
                p->state->queue[target].id,
                sizeof(p->state->jump_target_track_id) - 1);
        p->state->jump_target_track_id[sizeof(p->state->jump_target_track_id) - 1] = '\0';
    }

    /* Spotify exposes no "jump to N" — call next N times. */
    for (int i = 0; i < steps; i++) {
        spotify_next(p->api, p->state);
    }
    p->state->needs_redraw = true;
}

void player_volume_up(player_t *p)
{ if (p && p->state->volume < 100) p->state->volume += 5; }

void player_volume_down(player_t *p)
{ if (p && p->state->volume > 0) p->state->volume -= 5; }

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

static void start_art_download(player_state_t *state, const char *url, const char *token, int w, int h)
{
    if (state->art_download_pending) return;
    if (!url || !url[0]) return;

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

static void check_art_download_complete(player_state_t *state)
{
    if (!state->art_download_complete) return;

#ifdef _WIN32
    WaitForSingleObject(state->art_mutex, INFINITE);
#else
    pthread_mutex_lock(&state->art_mutex);
#endif

    if (state->art_download_complete && state->pending_album_ascii[0]) {
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

static void cycle_display_mode(player_state_t *state)
{
    state->display_mode = (display_mode_e)(((int)state->display_mode + 1) % 3);
}

static void switch_screen(player_state_t *state, screen_e s,
                          spotify_api_t *api,
                          library_ui_t *lib, playlists_ui_t *pls)
{
    state->screen = s;
    if (s == SCREEN_LIBRARY && lib) library_ensure_loaded(lib, api);
    if (s == SCREEN_PLAYLISTS && pls) playlists_ui_ensure_loaded(pls, api);
}

static void toggle_like(spotify_api_t *api, player_state_t *state)
{
    if (!state->current_track.id[0]) return;
    if (state->current_track_liked) {
        if (spotify_unsave_track(api, state->current_track.id))
            state->current_track_liked = false;
    } else {
        if (spotify_save_track(api, state->current_track.id))
            state->current_track_liked = true;
    }
}

void player_run(player_t *player)
{
    if (!player) return;

    input_init();

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

    int art_w = 64;
    int art_h = 32;

    spotify_get_current_playback(player->api, player->state);

    {
        char key[1024];
        snprintf(key, sizeof(key), "%s|%s",
                 player->state->current_track.album[0] ? player->state->current_track.album : "Album",
                 player->state->current_track.name[0]  ? player->state->current_track.name  : "Track");
        album_art_generate_placeholder(player->state->album_ascii, sizeof(player->state->album_ascii),
                                       art_w, art_h, key);
        album_art_generate_placeholder(player->state->album_ascii_small, sizeof(player->state->album_ascii_small),
                                       32, 16, key);
        album_art_generate_placeholder(player->state->album_ascii_tiny, sizeof(player->state->album_ascii_tiny),
                                       10, 5, key);
    }

    mode_renderer_t *renderer = mode_renderer_create(W, H);
    search_ui_t     *search   = search_ui_create();
    library_ui_t    *library  = library_create();
    playlists_ui_t  *pls      = playlists_ui_create();

    /* Cache current user id (used for playlist ownership flag). */
    spotify_get_current_user_id(player->api);

    api_poller_args_t *pa = (api_poller_args_t *)malloc(sizeof(api_poller_args_t));
    pa->player = player;
    pa->running = &player->running;
#ifdef _WIN32
    HANDLE poller_handle = CreateThread(NULL, 0, api_poller_thread, pa, 0, NULL);
#else
    pthread_t poller_handle = 0;
    pthread_create(&poller_handle, NULL, api_poller_thread, pa);
#endif

    while (player->running) {
        tui_get_size(&W, &H);
        renderer->screen_width = W;
        renderer->screen_height = H;

        refresh_art_for_track(player->state, art_w, art_h);

        tui_clear();

        if (player->state->screen == SCREEN_NOW_PLAYING) {
            mode_render(renderer, player->state);
        } else {
            mode_render_header(player->state);

            /* Search and Library share the Now Playing queue sidebar. The
               renderers truncate their content to avail_w so nothing leaks
               under the sidebar. */
            int avail_w = W;
            if (player->state->screen == SCREEN_SEARCH ||
                player->state->screen == SCREEN_LIBRARY) {
                int sw = mode_render_queue_sidebar(player->state);
                if (sw > 0) avail_w = W - sw;
            }

            switch (player->state->screen) {
            case SCREEN_SEARCH:    search_ui_render(search, avail_w);              break;
            case SCREEN_LIBRARY:   library_render(library, avail_w);               break;
            case SCREEN_PLAYLISTS: playlists_ui_render(pls);                       break;
            default: break;
            }
        }
        tui_flush();

        check_art_download_complete(player->state);
        check_lyrics_fetch_complete(player->state);

        /* Up Next jump resolution: once the target song is actually playing,
           restore the volume we muted on jump start and snap the green
           highlight back to the first upcoming track. */
        if (player->state->jump_in_progress &&
            player->state->jump_target_track_id[0] &&
            strcmp(player->state->current_track.id,
                   player->state->jump_target_track_id) == 0) {
            /* Restore volume first, then resume play (if we paused it),
               so the song's opening is heard at the user's chosen level. */
            spotify_set_volume(player->api, player->state,
                               player->state->jump_saved_volume);
            if (player->state->jump_was_playing) {
                spotify_play(player->api, player->state);
            }
            player->state->jump_in_progress = false;
            player->state->jump_target_track_id[0] = '\0';

            int next = player->state->current_queue_index + 1;
            if (next >= player->state->queue_size)
                next = player->state->queue_size - 1;
            if (next < 0) next = 0;
            player->state->queue_sidebar_sel = next;
        }

        /* If the loaded lyrics belong to a different track than what's now
           playing (e.g. an in-flight fetch was discarded as stale after the
           user jumped songs), kick off a fresh fetch. */
        if (player->state->current_track.id[0] &&
            !player->state->lyrics_fetch_pending &&
            strcmp(player->state->lyrics_track_id,
                   player->state->current_track.id) != 0) {
            start_lyrics_fetch(player->state,
                               player->state->current_track.id,
                               player->state->current_track.name,
                               player->state->current_track.artist,
                               player->state->current_track.album,
                               player->state->current_track.duration_ms);
        }

        /* Debounced search */
        search_ui_tick(search, player->api, now_ms());

        static char last_track_id[256] = {0};
        const char *cur_id = player->state->current_track.id;
        if (cur_id[0] && strcmp(last_track_id, cur_id) != 0) {
            char key[1024];
            snprintf(key, sizeof(key), "%s|%s",
                     player->state->current_track.album[0] ? player->state->current_track.album : "Album",
                     player->state->current_track.name[0]  ? player->state->current_track.name  : "Track");
            album_art_generate_placeholder(player->state->album_ascii,
                                           sizeof(player->state->album_ascii),
                                           art_w, art_h, key);
            album_art_generate_placeholder(player->state->album_ascii_small,
                                           sizeof(player->state->album_ascii_small),
                                           32, 16, key);
            album_art_generate_placeholder(player->state->album_ascii_tiny,
                                           sizeof(player->state->album_ascii_tiny),
                                           10, 5, key);

            lyrics_clear(player->state);
            lyrics_add(player->state, 0, "♪ Loading lyrics… ♪");
            player->state->current_lyric_index = 0;

            start_lyrics_fetch(player->state,
                               player->state->current_track.id,
                               player->state->current_track.name,
                               player->state->current_track.artist,
                               player->state->current_track.album,
                               player->state->current_track.duration_ms);

            if (player->state->current_track.cover_url[0]) {
                start_art_download(player->state,
                                   player->state->current_track.cover_url,
                                   player->api->auth->token.access_token,
                                   art_w, art_h);
            }

            /* Refresh like status for the new track. */
            bool saved = false;
            if (spotify_check_track_saved(player->api, cur_id, &saved)) {
                player->state->current_track_liked = saved;
            }

            strncpy(last_track_id, cur_id, sizeof(last_track_id) - 1);
            last_track_id[sizeof(last_track_id) - 1] = 0;
        }

        if (player->state->playback_state == PLAYBACK_STATE_PLAYING) {
            if (player->state->current_track.progress_ms < player->state->current_track.duration_ms) {
                player->state->current_track.progress_ms += 50;
            }
        }

        input_event_t *evt = input_poll();
        if (evt) {
            if (evt->type == INPUT_QUIT) break;

            screen_e scr = player->state->screen;

            /* Modal-first dispatch: any active modal eats input. */
            if (scr == SCREEN_PLAYLISTS && pls && pls->modal.active) {
                playlists_ui_handle_modal_event(pls, player->api, evt);
                sleep_ms(50);
                continue;
            }
            /* Search screen has two sub-modes:
               - editing=true: printable chars feed the query, Esc exits typing.
               - editing=false: navigation keys work, global keys still apply. */
            if (scr == SCREEN_SEARCH) {
                if (search->editing) {
                    if (evt->type == INPUT_KEY) {
                        switch (evt->special_key) {
                        case INPUT_KEY_ESCAPE:
                            search->editing = false;
                            break;
                        case INPUT_KEY_BACKSPACE:
                            search_ui_backspace(search, now_ms());
                            break;
                        case INPUT_KEY_TAB:
                            search_ui_cycle_type(search, player->api);
                            break;
                        case INPUT_KEY_UP:    search_ui_select_prev(search); break;
                        case INPUT_KEY_DOWN:  search_ui_select_next(search); break;
                        case INPUT_KEY_ENTER: {
                            const search_result_t *r = search_ui_get_selected(search);
                            if (r && r->uri[0]) {
                                if (r->type == SR_TRACK) spotify_play_uri(player->api, r->uri);
                                else                     spotify_play_context(player->api, r->uri, NULL);
                            }
                            break;
                        }
                        default: break;
                        }
                    } else if (evt->type == INPUT_CHAR) {
                        search_ui_input_char(search, evt->ch, now_ms());
                    }
                    sleep_ms(50);
                    continue;
                }

                /* Not editing: handle search-screen nav keys, then fall through
                   to the global key handler so 1-5, space, n/p still work. */
                if (evt->type == INPUT_KEY) {
                    switch (evt->special_key) {
                    case INPUT_KEY_ESCAPE:
                        switch_screen(player->state, SCREEN_NOW_PLAYING, player->api, library, pls);
                        sleep_ms(50); continue;
                    case INPUT_KEY_TAB:
                        search_ui_cycle_type(search, player->api);
                        sleep_ms(50); continue;
                    case INPUT_KEY_UP:    search_ui_select_prev(search); sleep_ms(50); continue;
                    case INPUT_KEY_DOWN:  search_ui_select_next(search); sleep_ms(50); continue;
                    case INPUT_KEY_ENTER: {
                        const search_result_t *r = search_ui_get_selected(search);
                        if (r && r->uri[0]) {
                            if (r->type == SR_TRACK) spotify_play_uri(player->api, r->uri);
                            else                     spotify_play_context(player->api, r->uri, NULL);
                        }
                        sleep_ms(50); continue;
                    }
                    default: break;
                    }
                } else if (evt->type == INPUT_CHAR && evt->ch == '/') {
                    search->editing = true;
                    sleep_ms(50); continue;
                } else if (evt->type == INPUT_CHAR && evt->ch == 'q') {
                    const search_result_t *r = search_ui_get_selected(search);
                    if (r && r->uri[0]) spotify_add_to_queue(player->api, r->uri);
                    sleep_ms(50); continue;
                } else if (evt->type == INPUT_CHAR && evt->ch == 'Q') {
                    player_toggle_queue(player);
                    sleep_ms(50); continue;
                }
                /* fall through to global keys */
            }

            /* Global keys (Now Playing / Library / Playlists / Queue). */
            if (evt->type == INPUT_CHAR) {
                switch (evt->ch) {
                case '1': switch_screen(player->state, SCREEN_NOW_PLAYING, player->api, library, pls); sleep_ms(50); continue;
                case '2': switch_screen(player->state, SCREEN_SEARCH,      player->api, library, pls); sleep_ms(50); continue;
                case '3': switch_screen(player->state, SCREEN_LIBRARY,     player->api, library, pls); sleep_ms(50); continue;
                case '4': switch_screen(player->state, SCREEN_PLAYLISTS,   player->api, library, pls); sleep_ms(50); continue;
                case ' ':
                    if (player->state->playback_state == PLAYBACK_STATE_PLAYING) {
                        spotify_pause(player->api, player->state);
                        player->state->playback_state = PLAYBACK_STATE_PAUSED;
                    } else {
                        spotify_play(player->api, player->state);
                        player->state->playback_state = PLAYBACK_STATE_PLAYING;
                    }
                    sleep_ms(50); continue;
                case 'n': case 'N':
                    if (scr == SCREEN_PLAYLISTS) {
                        playlists_ui_open_create(pls);
                    } else {
                        spotify_next(player->api, player->state);
                        player->state->needs_redraw = true;
                    }
                    sleep_ms(50); continue;
                case 'p': case 'P':
                    spotify_previous(player->api, player->state);
                    player->state->needs_redraw = true;
                    sleep_ms(50); continue;
                case 's': case 'S':
                    spotify_set_shuffle(player->api, player->state,
                                       !player->state->shuffle_enabled);
                    sleep_ms(50); continue;
                case 'r': case 'R':
                    if (scr == SCREEN_PLAYLISTS) {
                        playlists_ui_open_rename(pls);
                    } else {
                        repeat_mode_e m = (repeat_mode_e)((player->state->repeat_mode + 1) % 3);
                        spotify_set_repeat(player->api, player->state, m);
                    }
                    sleep_ms(50); continue;
                case '+': case '=':
                    if (player->state->volume < 100) {
                        player->state->volume += 5;
                        spotify_set_volume(player->api, player->state, player->state->volume);
                    }
                    sleep_ms(50); continue;
                case '-': case '_':
                    if (player->state->volume > 0) {
                        player->state->volume -= 5;
                        spotify_set_volume(player->api, player->state, player->state->volume);
                    }
                    sleep_ms(50); continue;
                case 'm': case 'M':
                    cycle_display_mode(player->state);
                    sleep_ms(50); continue;
                case 'l': case 'L':
                    toggle_like(player->api, player->state);
                    sleep_ms(50); continue;
                default: break;
                }
            }

            /* Per-screen handling for non-search screens. */
            if (scr == SCREEN_NOW_PLAYING) {
                if (evt->type == INPUT_KEY) {
                    switch (evt->special_key) {
                    case INPUT_KEY_ESCAPE: player->running = false; break;
                    case INPUT_KEY_UP:     player_queue_up(player); break;
                    case INPUT_KEY_DOWN:   player_queue_down(player); break;
                    case INPUT_KEY_ENTER:  player_queue_jump_to_sel(player); break;
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
                    if (evt->ch == 'q' || evt->ch == 'Q') player_toggle_queue(player);
                }
            } else if (scr == SCREEN_LIBRARY) {
                if (evt->type == INPUT_KEY) {
                    switch (evt->special_key) {
                    case INPUT_KEY_ESCAPE:
                        if (!library_drill_back(library))
                            switch_screen(player->state, SCREEN_NOW_PLAYING, player->api, library, pls);
                        break;
                    case INPUT_KEY_UP:    library_select_prev(library); break;
                    case INPUT_KEY_DOWN:  library_select_next(library); break;
                    case INPUT_KEY_RIGHT: library_drill_in(library, player->api); break;
                    case INPUT_KEY_TAB:   library_cycle_view(library, player->api); break;
                    case INPUT_KEY_ENTER: library_play_selected(library, player->api); break;
                    default: break;
                    }
                } else if (evt->type == INPUT_CHAR) {
                    if (evt->ch == 'q')
                        library_queue_selected(library, player->api);
                    else if (evt->ch == 'Q')
                        player_toggle_queue(player);
                    else if (evt->ch == 'd' || evt->ch == 'D')
                        library_remove_selected(library, player->api);
                }
            } else if (scr == SCREEN_PLAYLISTS) {
                if (evt->type == INPUT_KEY) {
                    switch (evt->special_key) {
                    case INPUT_KEY_ESCAPE:
                        switch_screen(player->state, SCREEN_NOW_PLAYING, player->api, library, pls);
                        break;
                    case INPUT_KEY_UP:    playlists_ui_select_prev(pls, player->api); break;
                    case INPUT_KEY_DOWN:  playlists_ui_select_next(pls, player->api); break;
                    case INPUT_KEY_LEFT:  playlists_ui_pane_left(pls);  break;
                    case INPUT_KEY_RIGHT: playlists_ui_pane_right(pls); break;
                    case INPUT_KEY_ENTER: playlists_ui_play_selected(pls, player->api); break;
                    case INPUT_KEY_SHIFT_UP:   playlists_ui_move_track_up(pls, player->api);   break;
                    case INPUT_KEY_SHIFT_DOWN: playlists_ui_move_track_down(pls, player->api); break;
                    default: break;
                    }
                } else if (evt->type == INPUT_CHAR) {
                    switch (evt->ch) {
                    case 'a': case 'A':
                        playlists_ui_add_current_track(pls, player->api, player->state); break;
                    case 'x': case 'X':
                        playlists_ui_remove_track(pls, player->api); break;
                    case 'd': case 'D':
                        playlists_ui_delete_selected(pls, player->api); break;
                    default: break;
                    }
                }
            }
        }

        sleep_ms(50);
    }

#ifdef _WIN32
    if (poller_handle) {
        WaitForSingleObject(poller_handle, 2000);
        CloseHandle(poller_handle);
    }
#else
    if (poller_handle) {
        pthread_join(poller_handle, NULL);
    }
#endif

    playlists_ui_free(pls);
    library_free(library);
    search_ui_free(search);
    mode_renderer_free(renderer);
    tui_cleanup();
    input_cleanup();
}
