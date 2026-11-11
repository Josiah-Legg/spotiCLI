#include "playlists_ui.h"
#include "tui.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

playlists_ui_t *playlists_ui_create(void)
{
    playlists_ui_t *ui = (playlists_ui_t *)calloc(1, sizeof(*ui));
    if (!ui) return NULL;
    ui->pane = PLU_PANE_PLAYLISTS;
    ui->modal_kind = PLU_MODAL_NONE;
    return ui;
}

void playlists_ui_free(playlists_ui_t *ui) { if (ui) free(ui); }

void playlists_ui_ensure_loaded(playlists_ui_t *ui, spotify_api_t *api)
{
    if (!ui || !api || ui->playlists_loaded) return;
    spotify_get_user_playlists(api, ui->playlists, &ui->playlist_count, PLU_MAX_PLAYLISTS);
    ui->playlists_loaded = true;
    if (ui->playlist_count > 0) playlists_ui_load_selected_tracks(ui, api);
}

void playlists_ui_load_selected_tracks(playlists_ui_t *ui, spotify_api_t *api)
{
    if (!ui || !api) return;
    if (ui->sel_playlist < 0 || ui->sel_playlist >= ui->playlist_count) {
        ui->track_count = 0;
        ui->loaded_for_id[0] = 0;
        return;
    }
    const playlist_t *p = &ui->playlists[ui->sel_playlist];
    if (strcmp(p->id, ui->loaded_for_id) == 0) return;
    ui->track_count = 0;
    ui->sel_track = 0;
    spotify_get_playlist_tracks(api, p->id, ui->tracks, &ui->track_count, PLU_MAX_TRACKS);
    strncpy(ui->loaded_for_id, p->id, sizeof(ui->loaded_for_id) - 1);
    ui->loaded_for_id[sizeof(ui->loaded_for_id) - 1] = 0;
}

void playlists_ui_select_next(playlists_ui_t *ui, spotify_api_t *api)
{
    if (!ui) return;
    if (ui->pane == PLU_PANE_PLAYLISTS) {
        if (ui->sel_playlist < ui->playlist_count - 1) {
            ui->sel_playlist++;
            playlists_ui_load_selected_tracks(ui, api);
        }
    } else {
        if (ui->sel_track < ui->track_count - 1) ui->sel_track++;
    }
}

void playlists_ui_select_prev(playlists_ui_t *ui, spotify_api_t *api)
{
    if (!ui) return;
    if (ui->pane == PLU_PANE_PLAYLISTS) {
        if (ui->sel_playlist > 0) {
            ui->sel_playlist--;
            playlists_ui_load_selected_tracks(ui, api);
        }
    } else {
        if (ui->sel_track > 0) ui->sel_track--;
    }
}

void playlists_ui_pane_left(playlists_ui_t *ui)  { if (ui) ui->pane = PLU_PANE_PLAYLISTS; }
void playlists_ui_pane_right(playlists_ui_t *ui) { if (ui) ui->pane = PLU_PANE_TRACKS; }

bool playlists_ui_play_selected(playlists_ui_t *ui, spotify_api_t *api)
{
    if (!ui || !api) return false;
    if (ui->sel_playlist < 0 || ui->sel_playlist >= ui->playlist_count) return false;
    const playlist_t *p = &ui->playlists[ui->sel_playlist];
    char ctx[320];
    snprintf(ctx, sizeof(ctx), "spotify:playlist:%s", p->id);

    if (ui->pane == PLU_PANE_PLAYLISTS) {
        return spotify_play_context(api, ctx, NULL);
    }
    if (ui->sel_track < 0 || ui->sel_track >= ui->track_count) return false;
    char track_uri[320];
    snprintf(track_uri, sizeof(track_uri), "spotify:track:%s", ui->tracks[ui->sel_track].id);
    return spotify_play_context(api, ctx, track_uri);
}

void playlists_ui_open_create(playlists_ui_t *ui)
{
    if (!ui) return;
    ui->modal_kind = PLU_MODAL_CREATE;
    modal_input_open(&ui->modal, "New playlist name", "");
}

void playlists_ui_open_rename(playlists_ui_t *ui)
{
    if (!ui) return;
    if (ui->sel_playlist < 0 || ui->sel_playlist >= ui->playlist_count) return;
    if (!ui->playlists[ui->sel_playlist].owned) return;
    ui->modal_kind = PLU_MODAL_RENAME;
    modal_input_open(&ui->modal, "Rename playlist", ui->playlists[ui->sel_playlist].name);
}

bool playlists_ui_delete_selected(playlists_ui_t *ui, spotify_api_t *api)
{
    if (!ui || !api) return false;
    if (ui->sel_playlist < 0 || ui->sel_playlist >= ui->playlist_count) return false;
    const playlist_t *p = &ui->playlists[ui->sel_playlist];
    if (!spotify_unfollow_playlist(api, p->id)) return false;
    for (int i = ui->sel_playlist; i < ui->playlist_count - 1; i++) {
        ui->playlists[i] = ui->playlists[i + 1];
    }
    ui->playlist_count--;
    if (ui->sel_playlist >= ui->playlist_count && ui->sel_playlist > 0) ui->sel_playlist--;
    ui->loaded_for_id[0] = 0;
    playlists_ui_load_selected_tracks(ui, api);
    return true;
}

bool playlists_ui_add_current_track(playlists_ui_t *ui, spotify_api_t *api,
                                    const player_state_t *state)
{
    if (!ui || !api || !state) return false;
    if (ui->sel_playlist < 0 || ui->sel_playlist >= ui->playlist_count) return false;
    if (!ui->playlists[ui->sel_playlist].owned) return false;
    if (!state->current_track.id[0]) return false;
    char uri[320];
    snprintf(uri, sizeof(uri), "spotify:track:%s", state->current_track.id);
    if (!spotify_add_track_to_playlist(api, ui->playlists[ui->sel_playlist].id, uri)) return false;
    /* Reflect locally. */
    if (ui->track_count < PLU_MAX_TRACKS &&
        strcmp(ui->loaded_for_id, ui->playlists[ui->sel_playlist].id) == 0) {
        queue_item_t *t = &ui->tracks[ui->track_count++];
        strncpy(t->id, state->current_track.id, sizeof(t->id) - 1);
        strncpy(t->name, state->current_track.name, sizeof(t->name) - 1);
        strncpy(t->artist, state->current_track.artist, sizeof(t->artist) - 1);
    }
    ui->playlists[ui->sel_playlist].track_count++;
    return true;
}

bool playlists_ui_remove_track(playlists_ui_t *ui, spotify_api_t *api)
{
    if (!ui || !api) return false;
    if (ui->sel_playlist < 0 || ui->sel_playlist >= ui->playlist_count) return false;
    if (!ui->playlists[ui->sel_playlist].owned) return false;
    if (ui->sel_track < 0 || ui->sel_track >= ui->track_count) return false;

    char uri[320];
    snprintf(uri, sizeof(uri), "spotify:track:%s", ui->tracks[ui->sel_track].id);
    if (!spotify_remove_track_from_playlist(api, ui->playlists[ui->sel_playlist].id, uri)) {
        return false;
    }
    for (int i = ui->sel_track; i < ui->track_count - 1; i++) ui->tracks[i] = ui->tracks[i + 1];
    ui->track_count--;
    if (ui->sel_track >= ui->track_count && ui->sel_track > 0) ui->sel_track--;
    if (ui->playlists[ui->sel_playlist].track_count > 0) {
        ui->playlists[ui->sel_playlist].track_count--;
    }
    return true;
}

static bool reorder_local(playlists_ui_t *ui, int from, int to)
{
    if (from < 0 || from >= ui->track_count) return false;
    if (to < 0 || to >= ui->track_count) return false;
    queue_item_t tmp = ui->tracks[from];
    if (from < to) {
        for (int i = from; i < to; i++) ui->tracks[i] = ui->tracks[i + 1];
    } else {
        for (int i = from; i > to; i--) ui->tracks[i] = ui->tracks[i - 1];
    }
    ui->tracks[to] = tmp;
    return true;
}

bool playlists_ui_move_track_up(playlists_ui_t *ui, spotify_api_t *api)
{
    if (!ui || !api) return false;
    if (ui->sel_playlist < 0 || ui->sel_playlist >= ui->playlist_count) return false;
    if (!ui->playlists[ui->sel_playlist].owned) return false;
    if (ui->sel_track <= 0) return false;

    int from = ui->sel_track;
    int insert_before = from - 1;
    if (!spotify_reorder_playlist_track(api, ui->playlists[ui->sel_playlist].id,
                                        from, insert_before)) {
        return false;
    }
    reorder_local(ui, from, from - 1);
    ui->sel_track--;
    return true;
}

bool playlists_ui_move_track_down(playlists_ui_t *ui, spotify_api_t *api)
{
    if (!ui || !api) return false;
    if (ui->sel_playlist < 0 || ui->sel_playlist >= ui->playlist_count) return false;
    if (!ui->playlists[ui->sel_playlist].owned) return false;
    if (ui->sel_track >= ui->track_count - 1) return false;

    int from = ui->sel_track;
    /* Spotify reorder: insert_before = from + 2 means "place after the next item". */
    int insert_before = from + 2;
    if (!spotify_reorder_playlist_track(api, ui->playlists[ui->sel_playlist].id,
                                        from, insert_before)) {
        return false;
    }
    reorder_local(ui, from, from + 1);
    ui->sel_track++;
    return true;
}

bool playlists_ui_handle_modal_event(playlists_ui_t *ui, spotify_api_t *api,
                                     const input_event_t *evt)
{
    if (!ui || !ui->modal.active || !evt) return false;
    bool closed = modal_input_handle_event(&ui->modal, evt);
    if (!closed) return true;

    if (ui->modal.confirmed && ui->modal.len > 0 && api) {
        if (ui->modal_kind == PLU_MODAL_CREATE) {
            char new_id[256] = {0};
            if (spotify_create_playlist(api, ui->modal.buf, new_id, sizeof(new_id))) {
                ui->playlists_loaded = false;
                spotify_get_user_playlists(api, ui->playlists, &ui->playlist_count,
                                           PLU_MAX_PLAYLISTS);
                ui->playlists_loaded = true;
                for (int i = 0; i < ui->playlist_count; i++) {
                    if (strcmp(ui->playlists[i].id, new_id) == 0) {
                        ui->sel_playlist = i;
                        break;
                    }
                }
                ui->loaded_for_id[0] = 0;
                playlists_ui_load_selected_tracks(ui, api);
            }
        } else if (ui->modal_kind == PLU_MODAL_RENAME) {
            if (ui->sel_playlist >= 0 && ui->sel_playlist < ui->playlist_count) {
                if (spotify_rename_playlist(api, ui->playlists[ui->sel_playlist].id,
                                            ui->modal.buf)) {
                    strncpy(ui->playlists[ui->sel_playlist].name, ui->modal.buf,
                            sizeof(ui->playlists[ui->sel_playlist].name) - 1);
                }
            }
        }
    }
    ui->modal_kind = PLU_MODAL_NONE;
    return true;
}

static void truncate_to(char *out, size_t out_sz, const char *in, int max_cols)
{
    if (!in) { out[0] = 0; return; }
    int len = (int)strlen(in);
    if (len <= max_cols) {
        snprintf(out, out_sz, "%.*s", (int)(out_sz - 1), in);
        return;
    }
    if (max_cols < 4) { snprintf(out, out_sz, "%.*s", max_cols, in); return; }
    snprintf(out, out_sz, "%.*s...", max_cols - 3, in);
}

void playlists_ui_render(playlists_ui_t *ui)
{
    if (!ui) return;

    int W, H;
    tui_get_size(&W, &H);
    int top = 3;

    tui_cursor_move(2, top);
    tui_set_color(COLOR_CYAN, COLOR_BLACK);
    tui_set_attr(ATTR_BOLD);
    tui_write("PLAYLISTS");
    tui_reset_style();

    int list_y = top + 2;
    int rows = H - list_y - 2;
    if (rows < 4) rows = 4;

    int left_x = 2;
    int left_w = W / 2 - 2;
    if (left_w < 20) left_w = 20;
    int right_x = left_x + left_w + 2;
    int right_w = W - right_x - 2;
    if (right_w < 20) right_w = 20;

    /* Pane labels */
    tui_cursor_move(left_x, list_y);
    if (ui->pane == PLU_PANE_PLAYLISTS) {
        tui_set_color(COLOR_BLACK, COLOR_GREEN); tui_set_attr(ATTR_BOLD);
    } else {
        tui_set_color(COLOR_WHITE, COLOR_BLACK);
    }
    tui_printf(" Playlists (%d) ", ui->playlist_count);
    tui_reset_style();

    tui_cursor_move(right_x, list_y);
    if (ui->pane == PLU_PANE_TRACKS) {
        tui_set_color(COLOR_BLACK, COLOR_GREEN); tui_set_attr(ATTR_BOLD);
    } else {
        tui_set_color(COLOR_WHITE, COLOR_BLACK);
    }
    tui_printf(" Tracks (%d) ", ui->track_count);
    tui_reset_style();

    int body_y = list_y + 2;
    int body_rows = H - body_y - 2;
    if (body_rows < 2) body_rows = 2;

    /* Left pane: playlists */
    {
        int n = ui->playlist_count;
        int sel = ui->sel_playlist;
        int start = sel - body_rows / 2;
        if (start < 0) start = 0;
        if (start + body_rows > n) start = n - body_rows;
        if (start < 0) start = 0;

        if (n == 0) {
            tui_cursor_move(left_x, body_y);
            tui_set_color(COLOR_WHITE, COLOR_BLACK);
            tui_write("(no playlists)");
            tui_reset_style();
        } else {
            for (int i = 0; i < body_rows && start + i < n; i++) {
                int idx = start + i;
                tui_cursor_move(left_x, body_y + i);
                char buf[1024];
                char marker = ui->playlists[idx].owned ? ' ' : 'r';
                snprintf(buf, sizeof(buf), "%c %s (%d)", marker,
                         ui->playlists[idx].name, ui->playlists[idx].track_count);
                char line[1024];
                truncate_to(line, sizeof(line), buf, left_w - 2);
                if (idx == sel && ui->pane == PLU_PANE_PLAYLISTS) {
                    tui_set_color(COLOR_BLACK, COLOR_GREEN); tui_set_attr(ATTR_BOLD);
                    tui_printf("> %s", line);
                } else if (idx == sel) {
                    tui_set_color(COLOR_CYAN, COLOR_BLACK);
                    tui_printf("> %s", line);
                } else {
                    tui_set_color(COLOR_WHITE, COLOR_BLACK);
                    tui_printf("  %s", line);
                }
                tui_reset_style();
            }
        }
    }

    /* Right pane: tracks */
    {
        int n = ui->track_count;
        int sel = ui->sel_track;
        int start = sel - body_rows / 2;
        if (start < 0) start = 0;
        if (start + body_rows > n) start = n - body_rows;
        if (start < 0) start = 0;

        if (n == 0) {
            tui_cursor_move(right_x, body_y);
            tui_set_color(COLOR_WHITE, COLOR_BLACK);
            tui_write(ui->playlist_count == 0 ? "" : "(empty)");
            tui_reset_style();
        } else {
            for (int i = 0; i < body_rows && start + i < n; i++) {
                int idx = start + i;
                tui_cursor_move(right_x, body_y + i);
                char buf[1024];
                snprintf(buf, sizeof(buf), "%s — %s",
                         ui->tracks[idx].name, ui->tracks[idx].artist);
                char line[1024];
                truncate_to(line, sizeof(line), buf, right_w - 2);
                if (idx == sel && ui->pane == PLU_PANE_TRACKS) {
                    tui_set_color(COLOR_BLACK, COLOR_GREEN); tui_set_attr(ATTR_BOLD);
                    tui_printf("> %s", line);
                } else {
                    tui_set_color(COLOR_WHITE, COLOR_BLACK);
                    tui_printf("  %s", line);
                }
                tui_reset_style();
            }
        }
    }

    /* Footer */
    tui_cursor_move(2, H - 1);
    tui_set_color_idx(240, -1);
    tui_set_attr(ATTR_NORMAL);
    tui_write(" [Up/Dn] Sel  [Lt/Rt] Pane  [Enter] Play  [N]ew  [R]ename  [D]elete  [A]dd cur  [X] Remove  [Sh-Up/Dn] Move  [Esc] Back ");
    tui_reset_style();

    /* Modal overlay */
    if (ui->modal.active) modal_input_render(&ui->modal);
}
