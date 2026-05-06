#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "state.h"
#include "auth.h"
#include "api.h"
#include "http.h"
#include "player.h"

static void seed_demo_data(player_state_t *state)
{
    snprintf(state->current_track.name,   sizeof(state->current_track.name),   "Midnight City");
    snprintf(state->current_track.artist, sizeof(state->current_track.artist), "M83");
    snprintf(state->current_track.album,  sizeof(state->current_track.album),  "Hurry Up, We're Dreaming");
    state->current_track.duration_ms = 244000;
    state->current_track.progress_ms = 62000;
    state->playback_state = PLAYBACK_STATE_PLAYING;

    queue_add(state, "1", "We Own The Sky",  "M83");
    queue_add(state, "2", "Outro",           "M83");
    queue_add(state, "3", "Wait",            "M83");
    queue_add(state, "4", "Steve McQueen",   "M83");
    queue_add(state, "5", "Reunion",         "M83");

    lyrics_add(state, 0,      "Waiting in a car");
    lyrics_add(state, 4000,   "Waiting for a ride in the dark");
    lyrics_add(state, 9000,   "The night city grows");
    lyrics_add(state, 14000,  "Look and see her eyes, they glow");
    lyrics_add(state, 22000,  "Waiting for a roar");
    lyrics_add(state, 26000,  "Looking at the mutating skyline");
    lyrics_add(state, 32000,  "The city is my church");
    lyrics_add(state, 38000,  "It wraps me in its blinding twilight");
    lyrics_add(state, 60000,  "Midnight city");
    lyrics_add(state, 64000,  "Waiting, waiting for the sun to set");
    lyrics_add(state, 72000,  "The city, my city");
    lyrics_add(state, 78000,  "Heartbeat under neon lights");
}

int main(int argc, char **argv)
{
    bool force_demo = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--demo") == 0) force_demo = true;
        if (strcmp(argv[i], "--login") == 0) {
            /* force re-auth: delete cached token */
            FILE *f = fopen(auth_default_config_path(), "wb");
            if (f) fclose(f);
        }
    }

    const char *client_id     = getenv("SPOTIFY_CLIENT_ID");
    const char *client_secret = getenv("SPOTIFY_CLIENT_SECRET");
    const char *redirect_uri  = getenv("SPOTIFY_REDIRECT_URI");
    if (!redirect_uri) redirect_uri = "http://127.0.0.1:8888/callback";

    bool have_creds = client_id && client_secret && client_id[0] && client_secret[0];
    bool demo_mode = force_demo || !have_creds;

    if (demo_mode) {
#ifdef _WIN32
        _putenv("SPOTICLI_DEMO=1");
#else
        setenv("SPOTICLI_DEMO", "1", 1);
#endif
    }

    http_init();

    player_state_t *state = player_state_init();
    if (!state) {
        fprintf(stderr, "fatal: state init failed\n");
        return 1;
    }
    seed_demo_data(state);

    auth_state_t *auth = auth_init(
        have_creds ? client_id     : "demo_client_id",
        have_creds ? client_secret : "demo_client_secret",
        redirect_uri);
    if (!auth) { player_state_free(state); return 1; }

    if (demo_mode) {
        strcpy(auth->token.access_token, "demo_token");
        auth->authenticated = true;
        fprintf(stderr,
            "SpotiCLI starting in DEMO mode.\n"
            "  To connect to your real Spotify account, set:\n"
            "    SPOTIFY_CLIENT_ID and SPOTIFY_CLIENT_SECRET (https://developer.spotify.com/dashboard)\n"
            "    Add http://127.0.0.1:8888/callback as a Redirect URI on the app.\n"
            "  Then run again without --demo.\n\n");
    } else {
        fprintf(stderr, "SpotiCLI starting. Authenticating with Spotify...\n");
        if (!auth_perform_oauth_flow(auth)) {
            fprintf(stderr, "[!] Auth failed -- falling back to demo mode.\n");
            http_set_demo_mode(true);
            strcpy(auth->token.access_token, "demo_token");
            auth->authenticated = true;
        }
    }

    spotify_api_t *api = spotify_api_init(auth);
    if (!api) { auth_free(auth); player_state_free(state); return 1; }

    player_t *player = player_create(state, api);
    if (!player) {
        spotify_api_free(api); auth_free(auth); player_state_free(state);
        return 1;
    }

    player_run(player);

    player_free(player);
    spotify_api_free(api);
    auth_free(auth);
    player_state_free(state);
    http_cleanup();
    return 0;
}
