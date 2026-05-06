#include "lyrics.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

bool lyrics_init(lyrics_config_t *config)
{
    if (!config) return false;
    config->preferred_source = LYRICS_SOURCE_SPOTIFY;
    config->cache_enabled = true;
    return true;
}

bool lyrics_fetch(const char *track_id, const char *track_name, const char *artist_name,
                  player_state_t *state)
{
    if (!track_id || !track_name || !artist_name || !state) return false;

    fprintf(stderr, "[lyrics] Fetching lyrics for '%s' by '%s'\n",
            track_name, artist_name);

    /* In a production environment, this would call a Genius or Spotify API.
       For now, we generate track-specific placeholder lyrics. */

    lyrics_clear(state);

    /* Generate lyrics that include the track/artist name for context */
    char line1[512], line2[512], line3[512], line4[512];
    snprintf(line1, sizeof(line1), "♪ %s ♪", track_name);
    snprintf(line2, sizeof(line2), "by %s", artist_name);
    snprintf(line3, sizeof(line3), "");
    snprintf(line4, sizeof(line4), "");

    lyrics_add(state, 0, line1);
    lyrics_add(state, 5000, line2);
    lyrics_add(state, 10000, line3);
    lyrics_add(state, 15000, line4);

    /* Add some generic lyrics lines */
    const char *generic_lines[] = {
        "♪ ♪ ♪",
        "Loading lyrics...",
        "",
        "(Lyrics would be fetched from",
        "Genius or Spotify API)",
        "",
        "Supports synchronized display",
        "as the song progresses"
    };
    int num_generic = sizeof(generic_lines) / sizeof(generic_lines[0]);
    for (int i = 0; i < num_generic; i++) {
        lyrics_add(state, 20000 + i * 5000, generic_lines[i]);
    }

    return true;
}

void lyrics_update_position(player_state_t *state, int progress_ms)
{
    if (!state || state->lyrics_count == 0) return;

    /* Find lyric line matching current progress */
    int best_idx = 0;
    for (int i = 0; i < state->lyrics_count; i++) {
        if (state->lyrics[i].timestamp_ms <= progress_ms) {
            best_idx = i;
        } else {
            break;
        }
    }

    state->current_lyric_index = best_idx;
}

const char *lyrics_get_current(player_state_t *state)
{
    if (!state || state->lyrics_count == 0) return NULL;
    if (state->current_lyric_index < 0 || state->current_lyric_index >= state->lyrics_count) {
        return NULL;
    }
    return state->lyrics[state->current_lyric_index].text;
}

const char *lyrics_get_next(player_state_t *state)
{
    if (!state || state->lyrics_count == 0) return NULL;
    int next_idx = state->current_lyric_index + 1;
    if (next_idx >= state->lyrics_count) return NULL;
    return state->lyrics[next_idx].text;
}

const char *lyrics_get_previous(player_state_t *state)
{
    if (!state || state->lyrics_count == 0) return NULL;
    int prev_idx = state->current_lyric_index - 1;
    if (prev_idx < 0) return NULL;
    return state->lyrics[prev_idx].text;
}

char *lyrics_format_display(player_state_t *state, int max_width)
{
    if (!state) return NULL;

    int buffer_size = 4096;
    char *output = (char *)malloc(buffer_size);
    if (!output) return NULL;

    int pos = 0;

    /* Format previous line (dimmed) */
    const char *prev = lyrics_get_previous(state);
    if (prev) {
        pos += snprintf(output + pos, buffer_size - pos, "\x1b[2m%s\x1b[0m\n", prev);
    }

    /* Format current line (bright) */
    const char *current = lyrics_get_current(state);
    if (current) {
        pos += snprintf(output + pos, buffer_size - pos, "\x1b[1m%s\x1b[0m\n", current);
    }

    /* Format next line (dimmed) */
    const char *next = lyrics_get_next(state);
    if (next) {
        pos += snprintf(output + pos, buffer_size - pos, "\x1b[2m%s\x1b[0m\n", next);
    }

    (void)max_width;  /* For future text wrapping */
    return output;
}

void lyrics_free_formatted(char *formatted)
{
    if (formatted) free(formatted);
}
