#include "auth.h"
#include "http.h"
#include "json_util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

auth_state_t *auth_init(const char *client_id, const char *client_secret, const char *redirect_uri)
{
    auth_state_t *auth = (auth_state_t *)malloc(sizeof(auth_state_t));
    if (!auth) return NULL;

    memset(auth, 0, sizeof(auth_state_t));

    if (client_id)
        strncpy(auth->client_id, client_id, sizeof(auth->client_id) - 1);
    if (client_secret)
        strncpy(auth->client_secret, client_secret, sizeof(auth->client_secret) - 1);
    if (redirect_uri)
        strncpy(auth->redirect_uri, redirect_uri, sizeof(auth->redirect_uri) - 1);

    auth->authenticated = false;

    return auth;
}

char *auth_get_authorization_url(auth_state_t *auth)
{
    if (!auth) return NULL;

    /* Build the authorization URL */
    static char url[2048];
    snprintf(url, sizeof(url),
        "https://accounts.spotify.com/authorize?"
        "client_id=%s&"
        "response_type=code&"
        "redirect_uri=%s&"
        "scope=streaming,user-read-email,user-read-private,user-read-playback-state,user-modify-playback-state",
        auth->client_id,
        auth->redirect_uri);

    return url;
}

bool auth_exchange_code(auth_state_t *auth, const char *code)
{
    if (!auth || !code) return false;

    /* Build the request body */
    char body[2048];
    snprintf(body, sizeof(body),
        "grant_type=authorization_code&"
        "code=%s&"
        "redirect_uri=%s&"
        "client_id=%s&"
        "client_secret=%s",
        code, auth->redirect_uri, auth->client_id, auth->client_secret);

    /* Make HTTP request */
    http_response_t *resp = http_request(
        HTTP_POST,
        "https://accounts.spotify.com/api/token",
        NULL,
        body,
        "application/x-www-form-urlencoded"
    );

    if (!resp || resp->status_code != 200) {
        fprintf(stderr, "Token exchange failed (status %d)\n", resp ? resp->status_code : 0);
        if (resp) http_response_free(resp);
        return false;
    }

    /* Parse response JSON */
    json_t *json = json_parse_string(resp->body);
    if (!json) {
        fprintf(stderr, "Failed to parse token response\n");
        http_response_free(resp);
        return false;
    }

    /* Extract tokens */
    const char *access_token = json_get_string(json, "access_token", NULL);
    const char *refresh_token = json_get_string(json, "refresh_token", NULL);
    int expires_in = json_get_int(json, "expires_in", 3600);

    if (!access_token) {
        fprintf(stderr, "No access token in response\n");
        json_decref(json);
        http_response_free(resp);
        return false;
    }

    /* Store tokens */
    strncpy(auth->token.access_token, access_token, sizeof(auth->token.access_token) - 1);
    if (refresh_token) {
        strncpy(auth->token.refresh_token, refresh_token, sizeof(auth->token.refresh_token) - 1);
    }
    auth->token.expires_in = expires_in;
    auth->token.acquired_at = time(NULL);
    auth->authenticated = true;

    json_decref(json);
    http_response_free(resp);

    return true;
}

bool auth_is_token_valid(auth_state_t *auth)
{
    if (!auth || !auth->token.access_token[0]) return false;

    time_t now = time(NULL);
    int age = (int)(now - auth->token.acquired_at);

    /* Token is valid if less than 50 minutes old (expires at 1 hour) */
    return age < (auth->token.expires_in - 600);
}

bool auth_refresh_token(auth_state_t *auth)
{
    if (!auth || !auth->token.refresh_token[0]) return false;

    /* Build the request body */
    char body[2048];
    snprintf(body, sizeof(body),
        "grant_type=refresh_token&"
        "refresh_token=%s&"
        "client_id=%s&"
        "client_secret=%s",
        auth->token.refresh_token, auth->client_id, auth->client_secret);

    /* Make HTTP request */
    http_response_t *resp = http_request(
        HTTP_POST,
        "https://accounts.spotify.com/api/token",
        NULL,
        body,
        "application/x-www-form-urlencoded"
    );

    if (!resp || resp->status_code != 200) {
        fprintf(stderr, "Token refresh failed (status %d)\n", resp ? resp->status_code : 0);
        if (resp) http_response_free(resp);
        return false;
    }

    /* Parse response JSON */
    json_t *json = json_parse_string(resp->body);
    if (!json) {
        fprintf(stderr, "Failed to parse refresh response\n");
        http_response_free(resp);
        return false;
    }

    /* Extract new access token */
    const char *access_token = json_get_string(json, "access_token", NULL);
    int expires_in = json_get_int(json, "expires_in", 3600);

    if (!access_token) {
        fprintf(stderr, "No access token in refresh response\n");
        json_decref(json);
        http_response_free(resp);
        return false;
    }

    /* Update tokens */
    strncpy(auth->token.access_token, access_token, sizeof(auth->token.access_token) - 1);
    auth->token.expires_in = expires_in;
    auth->token.acquired_at = time(NULL);

    json_decref(json);
    http_response_free(resp);

    return true;
}

/* ------------------------------------------------------------------ persistence */

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shlwapi.h>
#endif

const char *auth_default_config_path(void)
{
    static char path[1024] = {0};
    if (path[0]) return path;
#ifdef _WIN32
    const char *appdata = getenv("APPDATA");
    if (!appdata) appdata = ".";
    snprintf(path, sizeof(path), "%s\\spoticli", appdata);
    CreateDirectoryA(path, NULL);
    snprintf(path, sizeof(path), "%s\\spoticli\\token.json", appdata);
#else
    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(path, sizeof(path), "%s/.spoticli", home);
    /* mkdir best-effort, ignore errors */
    snprintf(path, sizeof(path), "%s/.spoticli/token.json", home);
#endif
    return path;
}

bool auth_load_token(auth_state_t *auth, const char *config_path)
{
    if (!auth) return false;
    if (!config_path) config_path = auth_default_config_path();

    FILE *f = fopen(config_path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 16384) { fclose(f); return false; }

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return false; }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = 0;
    fclose(f);

    json_t *j = json_parse_string(buf);
    free(buf);
    if (!j) return false;

    const char *at = json_get_string(j, "access_token", NULL);
    const char *rt = json_get_string(j, "refresh_token", NULL);
    int expires_in = json_get_int(j, "expires_in", 3600);
    int acquired = json_get_int(j, "acquired_at", 0);

    if (at && at[0]) {
        strncpy(auth->token.access_token, at, sizeof(auth->token.access_token) - 1);
    }
    if (rt && rt[0]) {
        strncpy(auth->token.refresh_token, rt, sizeof(auth->token.refresh_token) - 1);
    }
    auth->token.expires_in = expires_in;
    auth->token.acquired_at = (time_t)acquired;
    auth->authenticated = (auth->token.access_token[0] != 0);

    json_decref(j);
    return auth->authenticated;
}

bool auth_save_token(auth_state_t *auth, const char *config_path)
{
    if (!auth) return false;
    if (!config_path) config_path = auth_default_config_path();

    FILE *f = fopen(config_path, "wb");
    if (!f) {
        fprintf(stderr, "[auth] cannot write %s\n", config_path);
        return false;
    }
    fprintf(f,
            "{\n"
            "  \"access_token\":  \"%s\",\n"
            "  \"refresh_token\": \"%s\",\n"
            "  \"expires_in\":    %d,\n"
            "  \"acquired_at\":   %ld\n"
            "}\n",
            auth->token.access_token,
            auth->token.refresh_token,
            auth->token.expires_in,
            (long)auth->token.acquired_at);
    fclose(f);
    return true;
}

/* ------------------------------------------------------------------ OAuth flow */

#ifdef _WIN32

static int parse_port_from_redirect(const char *redirect_uri)
{
    /* expect http://<host>:PORT/path  -- find the colon after the host */
    if (!redirect_uri) return 8888;
    const char *p = strstr(redirect_uri, "://");
    if (!p) return 8888;
    p += 3;
    const char *colon = strchr(p, ':');
    if (!colon) return 8888;
    int port = atoi(colon + 1);
    return port > 0 ? port : 8888;
}

static char *capture_code_localhost(int port)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return NULL;

    SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == INVALID_SOCKET) { WSACleanup(); return NULL; }

    BOOL yes = TRUE;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((u_short)port);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "[auth] bind localhost:%d failed (port in use?)\n", port);
        closesocket(srv); WSACleanup(); return NULL;
    }
    if (listen(srv, 1) != 0) { closesocket(srv); WSACleanup(); return NULL; }

    fprintf(stderr, "[auth] waiting for browser callback on http://localhost:%d/callback ...\n", port);

    SOCKET cli = accept(srv, NULL, NULL);
    if (cli == INVALID_SOCKET) { closesocket(srv); WSACleanup(); return NULL; }

    char req[4096];
    int n = recv(cli, req, (int)sizeof(req) - 1, 0);
    if (n <= 0) {
        closesocket(cli); closesocket(srv); WSACleanup();
        return NULL;
    }
    req[n] = 0;

    char *code = NULL;
    char *q = strstr(req, "code=");
    if (q) {
        q += 5;
        char *end = q;
        while (*end && *end != '&' && *end != ' ' && *end != '\r' && *end != '\n') end++;
        size_t len = (size_t)(end - q);
        code = (char *)malloc(len + 1);
        if (code) { memcpy(code, q, len); code[len] = 0; }
    }

    const char *html =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n\r\n"
        "<!doctype html><html><head><title>SpotiCLI</title>"
        "<style>body{background:#0a0a0a;color:#e0e0e0;font-family:system-ui,sans-serif;"
        "display:flex;align-items:center;justify-content:center;height:100vh;margin:0}"
        ".card{background:#161616;border:1px solid #1db954;padding:32px 48px;border-radius:12px;text-align:center}"
        "h1{color:#1db954;margin:0 0 8px}</style></head>"
        "<body><div class=card><h1>SpotiCLI authorized</h1>"
        "<p>You can close this tab and return to the terminal.</p></div></body></html>";
    send(cli, html, (int)strlen(html), 0);

    closesocket(cli);
    closesocket(srv);
    WSACleanup();
    return code;
}

static void open_browser(const char *url)
{
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
}

#else  /* non-Windows: best-effort */
static char *capture_code_localhost(int port)  { (void)port; return NULL; }
static void open_browser(const char *url)
{
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "xdg-open \"%s\" >/dev/null 2>&1 || open \"%s\"", url, url);
    int r = system(cmd); (void)r;
}
static int parse_port_from_redirect(const char *r) { (void)r; return 8888; }
#endif

bool auth_perform_oauth_flow(auth_state_t *auth)
{
    if (!auth || !auth->client_id[0] || !auth->client_secret[0]) {
        fprintf(stderr, "[auth] missing client_id/secret\n");
        return false;
    }

    /* try cached token first */
    if (auth_load_token(auth, NULL)) {
        if (auth_is_token_valid(auth)) {
            fprintf(stderr, "[auth] using cached access token\n");
            return true;
        }
        if (auth->token.refresh_token[0] && auth_refresh_token(auth)) {
            fprintf(stderr, "[auth] refreshed access token\n");
            auth_save_token(auth, NULL);
            return true;
        }
    }

    /* Build full authorize URL with all useful scopes */
    const char *scopes =
        "user-read-playback-state user-modify-playback-state "
        "user-read-currently-playing user-read-private user-read-email "
        "playlist-read-private playlist-read-collaborative streaming";
    char *enc_scope = http_urlencode(scopes);
    char *enc_redir = http_urlencode(auth->redirect_uri);

    char url[3072];
    snprintf(url, sizeof(url),
        "https://accounts.spotify.com/authorize"
        "?client_id=%s&response_type=code&redirect_uri=%s&scope=%s&show_dialog=false",
        auth->client_id,
        enc_redir ? enc_redir : auth->redirect_uri,
        enc_scope ? enc_scope : "");
    if (enc_scope) http_free_encoded(enc_scope);
    if (enc_redir) http_free_encoded(enc_redir);

    fprintf(stderr, "[auth] opening browser for Spotify login...\n");
    open_browser(url);

    int port = parse_port_from_redirect(auth->redirect_uri);
    char *code = capture_code_localhost(port);
    if (!code) {
        fprintf(stderr, "[auth] did not capture authorization code\n");
        return false;
    }
    fprintf(stderr, "[auth] received code, exchanging for token...\n");

    bool ok = auth_exchange_code(auth, code);
    free(code);
    if (ok) auth_save_token(auth, NULL);
    return ok;
}

const char *auth_get_access_token(auth_state_t *auth)
{
    if (!auth) return NULL;
    return auth->token.access_token[0] ? auth->token.access_token : NULL;
}

void auth_free(auth_state_t *auth)
{
    if (!auth) return;
    free(auth);
}
