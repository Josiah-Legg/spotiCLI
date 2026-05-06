#include "settings.h"
#include "tui.h"
#include <stdlib.h>
#include <stdio.h>

settings_ui_t *settings_ui_create(void)
{
    settings_ui_t *ui = (settings_ui_t *)malloc(sizeof(settings_ui_t));
    if (!ui) return NULL;

    ui->selected_option = 0;
    ui->device_menu_open = false;
    ui->volume_editing = false;
    ui->temp_volume = 50;

    return ui;
}

void settings_ui_free(settings_ui_t *ui)
{
    if (ui) free(ui);
}

void settings_ui_render(settings_ui_t *ui, player_state_t *state)
{
    if (!ui || !state) return;

    tui_cursor_move(5, 5);
    tui_set_color(COLOR_CYAN, COLOR_BLACK);
    tui_set_attr(ATTR_BOLD);
    tui_write("SETTINGS");
    tui_reset_style();

    int y = 7;

    /* Devices */
    tui_cursor_move(5, y);
    if (ui->selected_option == 0) {
        tui_set_color(COLOR_BLACK, COLOR_GREEN);
        tui_write("► Devices");
        tui_reset_style();
    } else {
        tui_write("  Devices");
    }
    y++;

    /* Volume */
    tui_cursor_move(5, y);
    if (ui->selected_option == 1) {
        tui_set_color(COLOR_BLACK, COLOR_GREEN);
        tui_printf("► Volume: %d%%", state->volume);
        tui_reset_style();
    } else {
        tui_printf("  Volume: %d%%", state->volume);
    }
    y++;

    /* Repeat Mode */
    tui_cursor_move(5, y);
    if (ui->selected_option == 2) {
        tui_set_color(COLOR_BLACK, COLOR_GREEN);
        tui_write("► Repeat: ");
        tui_reset_style();
    } else {
        tui_write("  Repeat: ");
    }

    const char *repeat_str = state->repeat_mode == REPEAT_OFF ? "Off" :
                              state->repeat_mode == REPEAT_ALL ? "All" : "One";
    tui_printf("%s", repeat_str);
    y++;

    /* Shuffle */
    tui_cursor_move(5, y);
    if (ui->selected_option == 3) {
        tui_set_color(COLOR_BLACK, COLOR_GREEN);
        tui_write("► Shuffle: ");
        tui_reset_style();
    } else {
        tui_write("  Shuffle: ");
    }
    tui_write(state->shuffle_enabled ? "On" : "Off");
}

void settings_ui_render_devices(player_state_t *state, int selected)
{
    if (!state) return;

    tui_cursor_move(20, 8);
    tui_write("Available Devices:");

    for (int i = 0; i < state->device_count; i++) {
        tui_cursor_move(22, 9 + i);

        if (i == selected) {
            tui_set_color(COLOR_BLACK, COLOR_YELLOW);
            tui_printf("► %s %s", state->devices[i].name,
                       state->devices[i].is_active ? "(active)" : "");
            tui_reset_style();
        } else {
            tui_printf("  %s %s", state->devices[i].name,
                       state->devices[i].is_active ? "(active)" : "");
        }
    }
}

void settings_ui_render_volume(player_state_t *state, int y)
{
    if (!state) return;

    tui_cursor_move(5, y);
    tui_write("Volume: [");

    int bars = state->volume / 10;
    for (int i = 0; i < 10; i++) {
        tui_write(i < bars ? "=" : "-");
    }

    tui_printf("] %d%%", state->volume);
}

bool settings_change_device(spotify_api_t *api, player_state_t *state, int device_idx)
{
    if (!api || !state || device_idx < 0 || device_idx >= state->device_count) {
        return false;
    }

    fprintf(stderr, "[TODO] settings_change_device: Switch to device '%s' via API\n",
            state->devices[device_idx].name);

    state->active_device_index = device_idx;
    return true;
}

bool settings_set_volume(spotify_api_t *api, player_state_t *state, int volume)
{
    if (!api || !state || volume < 0 || volume > 100) {
        return false;
    }

    fprintf(stderr, "[TODO] settings_set_volume: Set volume to %d%% via API\n", volume);

    state->volume = volume;
    return true;
}

void settings_cycle_repeat(spotify_api_t *api, player_state_t *state)
{
    (void)api;
    if (!state) return;

    state->repeat_mode = (state->repeat_mode + 1) % 3;
    fprintf(stderr, "[TODO] settings_cycle_repeat: Update repeat mode via API\n");
}

void settings_toggle_shuffle(spotify_api_t *api, player_state_t *state)
{
    (void)api;
    if (!state) return;

    state->shuffle_enabled = !state->shuffle_enabled;
    fprintf(stderr, "[TODO] settings_toggle_shuffle: Update shuffle via API\n");
}
