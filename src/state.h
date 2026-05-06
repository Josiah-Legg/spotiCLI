#ifndef SPOTICLI_STATE_H
#define SPOTICLI_STATE_H

#include <time.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

/* Track information */
typedef struct {
    char id[256];
    char name[512];
    char artist[512];
    char album[512];
    char album_id[256];
    char cover_url[1024];
    int duration_ms;
    int progress_ms;
    bool is_playing;
} track_t;

/* Queue entry */
typedef struct {
    char id[256];
    char name[512];
    char artist[512];
} queue_item_t;

/* Player state */
typedef enum {
    PLAYBACK_STATE_PLAYING,
    PLAYBACK_STATE_PAUSED,
    PLAYBACK_STATE_STOPPED
} playback_state_e;

/* Display modes */
typedef enum {
    MODE_ALBUM_ART,
    MODE_HYBRID,
    MODE_LYRICS
} display_mode_e;

/* Repeat modes */
typedef enum {
    REPEAT_OFF = 0,
    REPEAT_ALL = 1,
    REPEAT_ONE = 2
} repeat_mode_e;

/* Devices */
typedef struct {
    char id[256];
    char name[512];
    bool is_active;
} device_t;

/* Lyrics line */
typedef struct {
    int timestamp_ms;
    char text[1024];
} lyric_line_t;

/* Player context */
typedef struct {
    track_t current_track;
    playback_state_e playback_state;
    display_mode_e display_mode;
    
    /* Queue */
    queue_item_t *queue;
    int queue_size;
    int queue_capacity;
    int current_queue_index;
    
    /* Lyrics */
    lyric_line_t *lyrics;
    int lyrics_count;
    int lyrics_capacity;
    int current_lyric_index;
    
    /* Devices */
    device_t *devices;
    int device_count;
    int active_device_index;
    
    /* Playback settings */
    bool shuffle_enabled;
    repeat_mode_e repeat_mode;
    int volume;
    
    /* UI state */
    bool queue_sidebar_visible;
    time_t last_api_update;
    bool needs_redraw;
    
    /* Cached album art */
    char album_ascii[65536];
    int album_width;
    int album_height;
    /* Small (32x16) variant for hybrid mode. Same source image, lower resolution. */
    char album_ascii_small[16384];

    /* Async album art download state */
    char pending_cover_url[1024];
    volatile bool art_download_pending;
    volatile bool art_download_complete;
    char pending_album_ascii[65536];
    char pending_album_ascii_small[16384];

#ifdef _WIN32
    HANDLE art_mutex;
    HANDLE art_thread;
#else
    pthread_mutex_t art_mutex;
    pthread_t art_thread;
#endif
} player_state_t;

/* Initialize player state */
player_state_t *player_state_init(void);

/* Free player state */
void player_state_free(player_state_t *state);

/* Queue operations */
void queue_add(player_state_t *state, const char *id, const char *name, const char *artist);
void queue_clear(player_state_t *state);
void queue_remove(player_state_t *state, int index);

/* Lyrics operations */
void lyrics_add(player_state_t *state, int timestamp_ms, const char *text);
void lyrics_clear(player_state_t *state);

#endif
