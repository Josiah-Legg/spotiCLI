#ifndef SPOTICLI_MODES_H
#define SPOTICLI_MODES_H

#include "state.h"
#include "tui.h"

/* Display mode renderer */
typedef struct {
    display_mode_e mode;
    int screen_width;
    int screen_height;
} mode_renderer_t;

/* Initialize mode renderer */
mode_renderer_t *mode_renderer_create(int width, int height);

/* Free mode renderer */
void mode_renderer_free(mode_renderer_t *renderer);

/* Render album art mode (large ASCII art) */
void mode_render_album(mode_renderer_t *renderer, player_state_t *state);

/* Render hybrid mode (smaller art + lyrics) */
void mode_render_hybrid(mode_renderer_t *renderer, player_state_t *state);

/* Render lyrics mode (full screen lyrics) */
void mode_render_lyrics(mode_renderer_t *renderer, player_state_t *state);

/* Render playback controls (common to all modes) */
void mode_render_controls(int y, player_state_t *state);

/* Render track info header */
void mode_render_header(player_state_t *state);

/* Render progress bar */
void mode_render_progress(int y, player_state_t *state, int width);

/* Dispatch to correct renderer based on mode */
void mode_render(mode_renderer_t *renderer, player_state_t *state);

#endif
