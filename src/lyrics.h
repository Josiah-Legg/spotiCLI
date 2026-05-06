#ifndef SPOTICLI_LYRICS_H
#define SPOTICLI_LYRICS_H

#include "state.h"
#include <stdbool.h>

/* Lyrics source */
typedef enum {
    LYRICS_SOURCE_SPOTIFY,
    LYRICS_SOURCE_GENIUS,
    LYRICS_SOURCE_NONE
} lyrics_source_e;

/* Lyrics fetcher */
typedef struct {
    lyrics_source_e preferred_source;
    bool cache_enabled;
} lyrics_config_t;

/* Initialize lyrics module */
bool lyrics_init(lyrics_config_t *config);

/* Fetch lyrics for a track */
bool lyrics_fetch(const char *track_id, const char *track_name, const char *artist_name,
                  player_state_t *state);

/* Update current lyric based on playback progress */
void lyrics_update_position(player_state_t *state, int progress_ms);

/* Get current lyric line */
const char *lyrics_get_current(player_state_t *state);

/* Get next lyric line */
const char *lyrics_get_next(player_state_t *state);

/* Get previous lyric line */
const char *lyrics_get_previous(player_state_t *state);

/* Format lyrics for display with timestamp */
char *lyrics_format_display(player_state_t *state, int max_width);

/* Free formatted lyrics */
void lyrics_free_formatted(char *formatted);

#endif
