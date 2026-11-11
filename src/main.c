#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "state.h"
#include "auth.h"
#include "api.h"
#include "http.h"
#include "player.h"

static void print_setup_message(void)
{
    fprintf(stderr,
        "SpotiCLI requires Spotify API credentials.\n"
        "  1. Create an app at https://developer.spotify.com/dashboard\n"
        "  2. Add http://127.0.0.1:8888/callback as a Redirect URI\n"
        "  3. Set environment variables:\n"
        "       SPOTIFY_CLIENT_ID\n"
        "       SPOTIFY_CLIENT_SECRET\n"
        "       SPOTIFY_REDIRECT_URI (optional, default http://127.0.0.1:8888/callback)\n");
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
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

    if (!client_id || !client_secret || !client_id[0] || !client_secret[0]) {
        print_setup_message();
        return 1;
    }

    http_init();

    player_state_t *state = player_state_init();
    if (!state) {
        fprintf(stderr, "fatal: state init failed\n");
        return 1;
    }

    auth_state_t *auth = auth_init(client_id, client_secret, redirect_uri);
    if (!auth) { player_state_free(state); return 1; }

    fprintf(stderr, "SpotiCLI starting. Authenticating with Spotify...\n");
    if (!auth_perform_oauth_flow(auth)) {
        fprintf(stderr, "[!] Auth failed. Run with --login to re-authenticate.\n");
        auth_free(auth); player_state_free(state); http_cleanup();
        return 1;
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
