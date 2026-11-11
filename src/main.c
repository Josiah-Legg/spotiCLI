#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "state.h"
#include "auth.h"
#include "api.h"
#include "http.h"
#include "player.h"
#include "config.h"

static void print_usage(void)
{
    fprintf(stderr,
        "Usage: spoticli [options]\n"
        "\n"
        "  --setup    Enter (or change) your Spotify app Client ID and secret\n"
        "  --login    Sign in to Spotify again\n"
        "  --help     Show this help\n"
        "\n"
        "Credentials are stored in %s\n"
        "SPOTIFY_CLIENT_ID / SPOTIFY_CLIENT_SECRET / SPOTIFY_REDIRECT_URI\n"
        "environment variables override the saved file if set.\n",
        config_credentials_path());
}

/* Forget the cached Spotify sign-in so the browser login runs again. */
static void clear_token(void)
{
    remove(auth_default_config_path());
}

int main(int argc, char **argv)
{
    bool force_setup = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--login") == 0) {
            clear_token();
        } else if (strcmp(argv[i], "--setup") == 0) {
            force_setup = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n\n", argv[i]);
            print_usage();
            return 1;
        }
    }

    credentials_t creds;
    bool have_creds = config_load_credentials(&creds);
    if (force_setup || !have_creds) {
        if (!config_run_setup(&creds))
            return 1;
        /* A refresh token belongs to one client id; start a fresh sign-in. */
        clear_token();
    }

    http_init();

    player_state_t *state = player_state_init();
    if (!state) {
        fprintf(stderr, "fatal: state init failed\n");
        return 1;
    }

    auth_state_t *auth = auth_init(creds.client_id, creds.client_secret, creds.redirect_uri);
    if (!auth) { player_state_free(state); return 1; }

    fprintf(stderr, "SpotiCLI starting. Authenticating with Spotify...\n");
    if (!auth_perform_oauth_flow(auth)) {
        fprintf(stderr,
            "[!] Auth failed. Try `spoticli --login` to sign in again,\n"
            "    or `spoticli --setup` if your Client ID / secret changed.\n");
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
