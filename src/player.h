#ifndef SPOTICLI_PLAYER_H
#define SPOTICLI_PLAYER_H

#include "state.h"
#include "api.h"

typedef struct {
    player_state_t *state;
    spotify_api_t *api;
    bool running;
} player_t;

/* Create player instance */
player_t *player_create(player_state_t *state, spotify_api_t *api);

/* Free player */
void player_free(player_t *player);

/* Main player loop - blocks until quit */
void player_run(player_t *player);

/* Player control functions */
void player_toggle_play(player_t *player);
void player_next_track(player_t *player);
void player_prev_track(player_t *player);
void player_change_mode(player_t *player, display_mode_e mode);
void player_toggle_queue(player_t *player);
void player_queue_up(player_t *player);
void player_queue_down(player_t *player);
void player_volume_up(player_t *player);
void player_volume_down(player_t *player);

#endif
