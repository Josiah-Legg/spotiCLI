#ifndef SPOTICLI_SETTINGS_H
#define SPOTICLI_SETTINGS_H

#include "state.h"
#include "api.h"
#include <stdbool.h>

/* Settings UI */
typedef struct {
    int selected_option;
    bool device_menu_open;
    bool volume_editing;
    int temp_volume;
} settings_ui_t;

/* Initialize settings */
settings_ui_t *settings_ui_create(void);

/* Free settings */
void settings_ui_free(settings_ui_t *ui);

/* Render settings menu */
void settings_ui_render(settings_ui_t *ui, player_state_t *state);

/* Render device selector */
void settings_ui_render_devices(player_state_t *state, int selected);

/* Render volume control */
void settings_ui_render_volume(player_state_t *state, int y);

/* Change device */
bool settings_change_device(spotify_api_t *api, player_state_t *state, int device_idx);

/* Set volume */
bool settings_set_volume(spotify_api_t *api, player_state_t *state, int volume);

/* Cycle repeat mode */
void settings_cycle_repeat(spotify_api_t *api, player_state_t *state);

/* Toggle shuffle */
void settings_toggle_shuffle(spotify_api_t *api, player_state_t *state);

#endif
