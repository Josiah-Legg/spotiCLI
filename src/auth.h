#ifndef SPOTICLI_AUTH_H
#define SPOTICLI_AUTH_H

#include <stdbool.h>
#include <time.h>

/* OAuth token structure */
typedef struct {
    char access_token[1024];
    char refresh_token[1024];
    int expires_in;
    time_t acquired_at;
} oauth_token_t;

/* OAuth state */
typedef struct {
    oauth_token_t token;
    char client_id[256];
    char client_secret[256];
    char redirect_uri[512];
    bool authenticated;
} auth_state_t;

/* Initialize authentication */
auth_state_t *auth_init(const char *client_id, const char *client_secret, const char *redirect_uri);

/* Start OAuth flow - returns auth URL for user */
char *auth_get_authorization_url(auth_state_t *auth);

/* Exchange authorization code for token */
bool auth_exchange_code(auth_state_t *auth, const char *code);

/* Check if token is still valid */
bool auth_is_token_valid(auth_state_t *auth);

/* Refresh expired token */
bool auth_refresh_token(auth_state_t *auth);

/* Load token from config file */
bool auth_load_token(auth_state_t *auth, const char *config_path);

/* Save token to config file */
bool auth_save_token(auth_state_t *auth, const char *config_path);

/* Get current access token */
const char *auth_get_access_token(auth_state_t *auth);

/* Free authentication state */
void auth_free(auth_state_t *auth);

/* Run the full interactive OAuth flow:
 *   1. open the user's browser to the authorize URL
 *   2. listen on the redirect_uri (must be http://localhost:<port>/callback)
 *   3. capture the ?code= parameter
 *   4. exchange it for an access/refresh token
 * Returns true on success. Prints progress to stderr.
 */
bool auth_perform_oauth_flow(auth_state_t *auth);

/* Default token storage path inside %APPDATA% / $HOME. */
const char *auth_default_config_path(void);

#endif
