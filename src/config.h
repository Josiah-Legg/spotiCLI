#ifndef SPOTICLI_CONFIG_H
#define SPOTICLI_CONFIG_H

#include <stdbool.h>

#define SPOTICLI_DEFAULT_REDIRECT_URI "http://127.0.0.1:8888/callback"

typedef struct {
    char client_id[256];
    char client_secret[256];
    char redirect_uri[512];
} credentials_t;

/* Directory holding config + token: %APPDATA%\spoticli or ~/.spoticli.
 * Created on first call. */
const char *config_dir(void);

/* Path of the saved credentials file (credentials.json in config_dir). */
const char *config_credentials_path(void);

/* Fill creds from, in order of priority:
 *   1. SPOTIFY_CLIENT_ID / SPOTIFY_CLIENT_SECRET / SPOTIFY_REDIRECT_URI env vars
 *   2. credentials.json
 * Returns true when both client id and secret are present. */
bool config_load_credentials(credentials_t *creds);

/* Interactive first-run setup: explains what to do, optionally opens the
 * Spotify dashboard, prompts for id + secret, validates and saves them.
 * Returns true if valid credentials were saved into creds. */
bool config_run_setup(credentials_t *creds);

#endif
