#include "state.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define INITIAL_QUEUE_CAPACITY 50
#define INITIAL_LYRICS_CAPACITY 100

player_state_t *player_state_init(void)
{
    player_state_t *state = (player_state_t *)malloc(sizeof(player_state_t));
    if (!state) return NULL;

    memset(state, 0, sizeof(player_state_t));

    /* Initialize defaults */
    state->playback_state = PLAYBACK_STATE_STOPPED;
    state->display_mode = MODE_ALBUM_ART;
    state->repeat_mode = REPEAT_OFF;
    state->shuffle_enabled = false;
    state->volume = 50;
    state->queue_sidebar_visible = true;
    state->active_device_index = -1;

    /* Allocate queue */
    state->queue_capacity = INITIAL_QUEUE_CAPACITY;
    state->queue = (queue_item_t *)malloc(INITIAL_QUEUE_CAPACITY * sizeof(queue_item_t));
    if (!state->queue) {
        free(state);
        return NULL;
    }
    state->queue_size = 0;

    /* Allocate lyrics */
    state->lyrics_capacity = INITIAL_LYRICS_CAPACITY;
    state->lyrics = (lyric_line_t *)malloc(INITIAL_LYRICS_CAPACITY * sizeof(lyric_line_t));
    if (!state->lyrics) {
        free(state->queue);
        free(state);
        return NULL;
    }
    state->lyrics_count = 0;

    /* Allocate devices */
    state->devices = (device_t *)malloc(10 * sizeof(device_t));
    if (!state->devices) {
        free(state->lyrics);
        free(state->queue);
        free(state);
        return NULL;
    }
    state->device_count = 0;

    state->last_api_update = 0;
    state->needs_redraw = true;

    /* Initialize mutex for async album art */
#ifdef _WIN32
    state->art_mutex = CreateMutex(NULL, FALSE, NULL);
    state->art_thread = NULL;
#else
    pthread_mutex_init(&state->art_mutex, NULL);
#endif
    state->art_download_pending = false;
    state->art_download_complete = false;
    state->pending_cover_url[0] = '\0';
    state->pending_album_ascii[0] = '\0';
    state->album_ascii_small[0] = '\0';
    state->pending_album_ascii_small[0] = '\0';

    return state;
}

void player_state_free(player_state_t *state)
{
    if (!state) return;

#ifdef _WIN32
    if (state->art_thread) {
        WaitForSingleObject(state->art_thread, 1000);
        CloseHandle(state->art_thread);
    }
    if (state->art_mutex) CloseHandle(state->art_mutex);
#else
    if (state->art_thread) {
        pthread_join(state->art_thread, NULL);
    }
    pthread_mutex_destroy(&state->art_mutex);
#endif

    free(state->queue);
    free(state->lyrics);
    free(state->devices);
    free(state);
}

void queue_add(player_state_t *state, const char *id, const char *name, const char *artist)
{
    if (!state || !id || !name) return;

    /* Resize if needed */
    if (state->queue_size >= state->queue_capacity) {
        state->queue_capacity *= 2;
        queue_item_t *new_queue = (queue_item_t *)realloc(state->queue, state->queue_capacity * sizeof(queue_item_t));
        if (!new_queue) return;
        state->queue = new_queue;
    }

    strncpy(state->queue[state->queue_size].id, id, sizeof(state->queue[state->queue_size].id) - 1);
    strncpy(state->queue[state->queue_size].name, name, sizeof(state->queue[state->queue_size].name) - 1);
    if (artist) {
        strncpy(state->queue[state->queue_size].artist, artist, sizeof(state->queue[state->queue_size].artist) - 1);
    }
    state->queue_size++;
    state->needs_redraw = true;
}

void queue_clear(player_state_t *state)
{
    if (!state) return;
    state->queue_size = 0;
    state->current_queue_index = 0;
    state->needs_redraw = true;
}

void queue_remove(player_state_t *state, int index)
{
    if (!state || index < 0 || index >= state->queue_size) return;

    /* Shift remaining items */
    for (int i = index; i < state->queue_size - 1; i++) {
        state->queue[i] = state->queue[i + 1];
    }
    state->queue_size--;
    state->needs_redraw = true;
}

void lyrics_add(player_state_t *state, int timestamp_ms, const char *text)
{
    if (!state || !text) return;

    /* Resize if needed */
    if (state->lyrics_count >= state->lyrics_capacity) {
        state->lyrics_capacity *= 2;
        lyric_line_t *new_lyrics = (lyric_line_t *)realloc(state->lyrics, state->lyrics_capacity * sizeof(lyric_line_t));
        if (!new_lyrics) return;
        state->lyrics = new_lyrics;
    }

    state->lyrics[state->lyrics_count].timestamp_ms = timestamp_ms;
    strncpy(state->lyrics[state->lyrics_count].text, text, sizeof(state->lyrics[state->lyrics_count].text) - 1);
    state->lyrics_count++;
}

void lyrics_clear(player_state_t *state)
{
    if (!state) return;
    state->lyrics_count = 0;
    state->current_lyric_index = 0;
}
