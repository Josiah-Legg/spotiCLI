#include "library.h"
#include "tui.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

library_ui_t *library_create(void)
{
    library_ui_t *ui = (library_ui_t *)calloc(1, sizeof(library_ui_t));
    if (!ui) return NULL;
    ui->view = LIB_VIEW_TRACKS;
    ui->sub = LIB_SUB_NONE;
    return ui;
}

void library_free(library_ui_t *ui) { if (ui) free(ui); }

void library_ensure_loaded(library_ui_t *ui, spotify_api_t *api)
{
    if (!ui || !api) return;
    if (ui->view == LIB_VIEW_TRACKS && !ui->tracks_loaded) {
        spotify_get_saved_tracks(api, ui->tracks, &ui->track_count, LIB_MAX_TRACKS);
        ui->tracks_loaded = true;
    } else if (ui->view == LIB_VIEW_ALBUMS && !ui->albums_loaded) {
        spotify_get_saved_albums(api, ui->albums, &ui->album_count, LIB_MAX_ALBUMS);
        ui->albums_loaded = true;
    } else if (ui->view == LIB_VIEW_ARTISTS && !ui->artists_loaded) {
        spotify_get_followed_artists(api, ui->artists, &ui->artist_count, LIB_MAX_ARTISTS);
        ui->artists_loaded = true;
    }
}

void library_cycle_view(library_ui_t *ui, spotify_api_t *api)
{
    if (!ui) return;
    ui->sub = LIB_SUB_NONE;
    ui->view = (library_view_e)(((int)ui->view + 1) % 3);
    library_ensure_loaded(ui, api);
}

static int *current_sel_ptr(library_ui_t *ui)
{
    if (ui->sub != LIB_SUB_NONE) return &ui->sub_sel;
    if (ui->view == LIB_VIEW_TRACKS)  return &ui->sel_track;
    if (ui->view == LIB_VIEW_ALBUMS)  return &ui->sel_album;
    return &ui->sel_artist;
}

static int current_count(library_ui_t *ui)
{
    if (ui->sub != LIB_SUB_NONE) return ui->sub_track_count;
    if (ui->view == LIB_VIEW_TRACKS)  return ui->track_count;
    if (ui->view == LIB_VIEW_ALBUMS)  return ui->album_count;
    return ui->artist_count;
}

void library_select_next(library_ui_t *ui)
{
    if (!ui) return;
    int *s = current_sel_ptr(ui);
    int n = current_count(ui);
    if (*s < n - 1) (*s)++;
}

void library_select_prev(library_ui_t *ui)
{
    if (!ui) return;
    int *s = current_sel_ptr(ui);
    if (*s > 0) (*s)--;
}

void library_render(library_ui_t *ui, int avail_w)
{
    if (!ui) return;

    int W, H;
    tui_get_size(&W, &H);
    if (avail_w > 0 && avail_w < W) W = avail_w;
    int top = 3;

    /* Header / view tabs */
    tui_cursor_move(2, top);
    tui_set_color(COLOR_CYAN, COLOR_BLACK);
    tui_set_attr(ATTR_BOLD);
    if (ui->sub == LIB_SUB_NONE) {
        tui_write("LIBRARY");
    } else {
        tui_printf("LIBRARY > %s", ui->sub_title);
    }
    tui_reset_style();

    if (ui->sub == LIB_SUB_NONE) {
        tui_cursor_move(2, top + 2);
        const char *labels[] = { "Liked Songs", "Saved Albums", "Followed Artists" };
        for (int i = 0; i < 3; i++) {
            if (i == (int)ui->view) {
                tui_set_color(COLOR_BLACK, COLOR_GREEN);
                tui_set_attr(ATTR_BOLD);
            } else {
                tui_set_color(COLOR_WHITE, COLOR_BLACK);
            }
            tui_printf(" %s ", labels[i]);
            tui_reset_style();
            tui_write(" ");
        }
    }

    int list_y = top + 4;
    int rows = H - list_y - 2;
    if (rows < 4) rows = 4;
    int n = current_count(ui);
    int sel = *current_sel_ptr(ui);

    int start = sel - rows / 2;
    if (start < 0) start = 0;
    if (start + rows > n) start = n - rows;
    if (start < 0) start = 0;

    if (n == 0) {
        tui_cursor_move(2, list_y);
        tui_set_color(COLOR_WHITE, COLOR_BLACK);
        tui_write("(empty)");
        tui_reset_style();
    } else {
        for (int i = 0; i < rows && start + i < n; i++) {
            int idx = start + i;
            tui_cursor_move(2, list_y + i);

            char line[1024];
            if (ui->sub != LIB_SUB_NONE) {
                snprintf(line, sizeof(line), "%s — %s",
                         ui->sub_tracks[idx].name, ui->sub_tracks[idx].artist);
            } else if (ui->view == LIB_VIEW_TRACKS) {
                snprintf(line, sizeof(line), "%s — %s",
                         ui->tracks[idx].name, ui->tracks[idx].artist);
            } else if (ui->view == LIB_VIEW_ALBUMS) {
                snprintf(line, sizeof(line), "%s — %s",
                         ui->albums[idx].name, ui->albums[idx].artist);
            } else {
                snprintf(line, sizeof(line), "%s", ui->artists[idx].name);
            }

            int max_text = W - 6;
            if (max_text < 4) max_text = 4;
            if ((int)strlen(line) > max_text) line[max_text] = 0;

            if (idx == sel) {
                tui_set_color(COLOR_BLACK, COLOR_GREEN);
                tui_set_attr(ATTR_BOLD);
                tui_printf("> %s", line);
            } else {
                tui_set_color(COLOR_WHITE, COLOR_BLACK);
                tui_printf("  %s", line);
            }
            tui_reset_style();
        }
    }

    /* Footer hint */
    tui_cursor_move(2, H - 1);
    tui_set_color_idx(240, -1);
    tui_set_attr(ATTR_NORMAL);
    if (ui->sub != LIB_SUB_NONE) {
        tui_write(" [Up/Down] Select  [Enter] Play  [Q] Queue  [Esc] Back ");
    } else if (ui->view == LIB_VIEW_TRACKS) {
        tui_write(" [Tab] View  [Up/Down] Select  [Enter] Play  [Q] Queue  [D] Unsave  [Esc] Back ");
    } else if (ui->view == LIB_VIEW_ALBUMS) {
        tui_write(" [Tab] View  [Enter] Play  [Right] Tracks  [D] Unsave  [Esc] Back ");
    } else {
        tui_write(" [Tab] View  [Enter] Top tracks  [Right] Top tracks  [D] Unfollow  [Esc] Back ");
    }
    tui_reset_style();
}

bool library_play_selected(library_ui_t *ui, spotify_api_t *api)
{
    if (!ui || !api) return false;

    if (ui->sub != LIB_SUB_NONE) {
        if (ui->sub_sel < 0 || ui->sub_sel >= ui->sub_track_count) return false;
        const queue_item_t *t = &ui->sub_tracks[ui->sub_sel];
        if (ui->sub_context_uri[0]) {
            char track_uri[320];
            snprintf(track_uri, sizeof(track_uri), "spotify:track:%s", t->id);
            return spotify_play_context(api, ui->sub_context_uri, track_uri);
        }
        char uri[320]; snprintf(uri, sizeof(uri), "spotify:track:%s", t->id);
        return spotify_play_uri(api, uri);
    }

    if (ui->view == LIB_VIEW_TRACKS) {
        if (ui->sel_track < 0 || ui->sel_track >= ui->track_count) return false;
        char uri[320];
        snprintf(uri, sizeof(uri), "spotify:track:%s", ui->tracks[ui->sel_track].id);
        return spotify_play_uri(api, uri);
    }
    if (ui->view == LIB_VIEW_ALBUMS) {
        if (ui->sel_album < 0 || ui->sel_album >= ui->album_count) return false;
        char ctx[320];
        snprintf(ctx, sizeof(ctx), "spotify:album:%s", ui->albums[ui->sel_album].id);
        return spotify_play_context(api, ctx, NULL);
    }
    if (ui->view == LIB_VIEW_ARTISTS) {
        if (ui->sel_artist < 0 || ui->sel_artist >= ui->artist_count) return false;
        char ctx[320];
        snprintf(ctx, sizeof(ctx), "spotify:artist:%s", ui->artists[ui->sel_artist].id);
        return spotify_play_context(api, ctx, NULL);
    }
    return false;
}

bool library_queue_selected(library_ui_t *ui, spotify_api_t *api)
{
    if (!ui || !api) return false;

    const char *track_id = NULL;
    if (ui->sub != LIB_SUB_NONE) {
        if (ui->sub_sel < 0 || ui->sub_sel >= ui->sub_track_count) return false;
        track_id = ui->sub_tracks[ui->sub_sel].id;
    } else if (ui->view == LIB_VIEW_TRACKS) {
        if (ui->sel_track < 0 || ui->sel_track >= ui->track_count) return false;
        track_id = ui->tracks[ui->sel_track].id;
    } else {
        return false;
    }
    char uri[320]; snprintf(uri, sizeof(uri), "spotify:track:%s", track_id);
    return spotify_add_to_queue(api, uri);
}

bool library_remove_selected(library_ui_t *ui, spotify_api_t *api)
{
    if (!ui || !api || ui->sub != LIB_SUB_NONE) return false;

    if (ui->view == LIB_VIEW_TRACKS) {
        if (ui->sel_track < 0 || ui->sel_track >= ui->track_count) return false;
        if (!spotify_unsave_track(api, ui->tracks[ui->sel_track].id)) return false;
        for (int i = ui->sel_track; i < ui->track_count - 1; i++) ui->tracks[i] = ui->tracks[i + 1];
        ui->track_count--;
        if (ui->sel_track >= ui->track_count && ui->sel_track > 0) ui->sel_track--;
        return true;
    }
    if (ui->view == LIB_VIEW_ALBUMS) {
        if (ui->sel_album < 0 || ui->sel_album >= ui->album_count) return false;
        if (!spotify_unsave_album(api, ui->albums[ui->sel_album].id)) return false;
        for (int i = ui->sel_album; i < ui->album_count - 1; i++) ui->albums[i] = ui->albums[i + 1];
        ui->album_count--;
        if (ui->sel_album >= ui->album_count && ui->sel_album > 0) ui->sel_album--;
        return true;
    }
    if (ui->view == LIB_VIEW_ARTISTS) {
        if (ui->sel_artist < 0 || ui->sel_artist >= ui->artist_count) return false;
        if (!spotify_unfollow_artist(api, ui->artists[ui->sel_artist].id)) return false;
        for (int i = ui->sel_artist; i < ui->artist_count - 1; i++) ui->artists[i] = ui->artists[i + 1];
        ui->artist_count--;
        if (ui->sel_artist >= ui->artist_count && ui->sel_artist > 0) ui->sel_artist--;
        return true;
    }
    return false;
}

bool library_drill_in(library_ui_t *ui, spotify_api_t *api)
{
    if (!ui || !api) return false;
    if (ui->sub != LIB_SUB_NONE) return false;

    ui->sub_track_count = 0;
    ui->sub_sel = 0;
    ui->sub_title[0] = 0;
    ui->sub_context_uri[0] = 0;

    if (ui->view == LIB_VIEW_ALBUMS) {
        if (ui->sel_album < 0 || ui->sel_album >= ui->album_count) return false;
        const saved_album_t *a = &ui->albums[ui->sel_album];
        spotify_get_album_tracks(api, a->id, ui->sub_tracks, &ui->sub_track_count, LIB_MAX_TRACKS);
        snprintf(ui->sub_title, sizeof(ui->sub_title), "%s", a->name);
        snprintf(ui->sub_context_uri, sizeof(ui->sub_context_uri), "spotify:album:%s", a->id);
        ui->sub = LIB_SUB_ALBUM_TRACKS;
        return true;
    }
    if (ui->view == LIB_VIEW_ARTISTS) {
        if (ui->sel_artist < 0 || ui->sel_artist >= ui->artist_count) return false;
        const artist_t *a = &ui->artists[ui->sel_artist];
        spotify_get_artist_top_tracks(api, a->id, ui->sub_tracks, &ui->sub_track_count, LIB_MAX_TRACKS);
        snprintf(ui->sub_title, sizeof(ui->sub_title), "%s", a->name);
        /* artist URI as context — Spotify will play the artist radio. */
        snprintf(ui->sub_context_uri, sizeof(ui->sub_context_uri), "spotify:artist:%s", a->id);
        ui->sub = LIB_SUB_ARTIST_TOP;
        return true;
    }
    return false;
}

bool library_drill_back(library_ui_t *ui)
{
    if (!ui || ui->sub == LIB_SUB_NONE) return false;
    ui->sub = LIB_SUB_NONE;
    ui->sub_track_count = 0;
    ui->sub_sel = 0;
    return true;
}
