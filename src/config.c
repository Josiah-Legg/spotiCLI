#include "config.h"
#include "json_util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <direct.h>
#define PATH_SEP "\\"
#else
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#define PATH_SEP "/"
#endif

#define DASHBOARD_URL "https://developer.spotify.com/dashboard"

/* ------------------------------------------------------------------ paths */

const char *config_dir(void)
{
    static char dir[1024] = {0};
    if (dir[0]) return dir;
#ifdef _WIN32
    const char *base = getenv("APPDATA");
    snprintf(dir, sizeof(dir), "%s\\spoticli", base ? base : ".");
    _mkdir(dir);
#else
    const char *base = getenv("HOME");
    snprintf(dir, sizeof(dir), "%s/.spoticli", base ? base : ".");
    mkdir(dir, 0700);
#endif
    return dir;
}

const char *config_credentials_path(void)
{
    static char path[1100] = {0};
    if (!path[0])
        snprintf(path, sizeof(path), "%s" PATH_SEP "credentials.json", config_dir());
    return path;
}

/* ------------------------------------------------------------------ helpers */

/* Strip surrounding whitespace and quotes in place (pasted values often
 * carry a trailing space, newline or quotes). */
static void sanitize(char *s)
{
    char *start = s;
    while (*start && (isspace((unsigned char)*start) || *start == '"' || *start == '\''))
        start++;
    size_t len = strlen(start);
    while (len > 0 && (isspace((unsigned char)start[len - 1]) ||
                       start[len - 1] == '"' || start[len - 1] == '\''))
        len--;
    memmove(s, start, len);
    s[len] = 0;
}

/* Spotify client ids and secrets are 32 hex characters. */
static bool looks_like_key(const char *s)
{
    if (strlen(s) != 32) return false;
    for (const char *p = s; *p; p++)
        if (!isxdigit((unsigned char)*p)) return false;
    return true;
}

static void copy_str(char *dst, size_t cap, const char *src)
{
    if (!src) return;
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = 0;
    sanitize(dst);
}

static void set_echo(bool on)
{
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    if (!GetConsoleMode(h, &mode)) return;
    if (on) mode |= ENABLE_ECHO_INPUT;
    else    mode &= ~(DWORD)ENABLE_ECHO_INPUT;
    SetConsoleMode(h, mode);
#else
    struct termios t;
    if (tcgetattr(STDIN_FILENO, &t) != 0) return;
    if (on) t.c_lflag |= ECHO;
    else    t.c_lflag &= ~(tcflag_t)ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
#endif
}

/* Read a line from stdin into buf. Returns false on EOF. */
static bool read_line(char *buf, size_t cap, bool hidden)
{
    if (hidden) set_echo(false);
    char *r = fgets(buf, (int)cap, stdin);
    if (hidden) { set_echo(true); fputc('\n', stderr); }
    if (!r) return false;
    sanitize(buf);
    return true;
}

static void open_url(const char *url)
{
#ifdef _WIN32
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "open '%s' >/dev/null 2>&1", url);
    if (system(cmd) != 0) { /* ignore */ }
#else
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "xdg-open '%s' >/dev/null 2>&1 &", url);
    if (system(cmd) != 0) { /* ignore */ }
#endif
}

/* Prompt until a valid 32-hex key is entered. Empty input / EOF cancels. */
static bool prompt_key(const char *label, char *out, size_t cap, bool hidden)
{
    char buf[512];
    for (;;) {
        fprintf(stderr, "  %s%s: ", label, hidden ? " (hidden)" : "");
        fflush(stderr);
        if (!read_line(buf, sizeof(buf), hidden) || !buf[0]) return false;
        if (looks_like_key(buf)) {
            copy_str(out, cap, buf);
            return true;
        }
        fprintf(stderr, "  That doesn't look right: expected 32 letters/digits, got %d characters. Try again.\n",
                (int)strlen(buf));
    }
}

/* ------------------------------------------------------------------ load / save */

static bool load_file(credentials_t *creds)
{
    FILE *f = fopen(config_credentials_path(), "rb");
    if (!f) return false;
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;

    json_t *j = json_parse_string(buf);
    if (!j) {
        fprintf(stderr, "[config] %s is not valid JSON; ignoring it.\n", config_credentials_path());
        return false;
    }
    copy_str(creds->client_id, sizeof(creds->client_id), json_get_string(j, "client_id", NULL));
    copy_str(creds->client_secret, sizeof(creds->client_secret), json_get_string(j, "client_secret", NULL));
    copy_str(creds->redirect_uri, sizeof(creds->redirect_uri), json_get_string(j, "redirect_uri", NULL));
    json_decref(j);
    return true;
}

static bool save_file(const credentials_t *creds)
{
    FILE *f = fopen(config_credentials_path(), "wb");
    if (!f) {
        fprintf(stderr, "[config] cannot write %s\n", config_credentials_path());
        return false;
    }
    fprintf(f, "{\n  \"client_id\": \"%s\",\n  \"client_secret\": \"%s\"", creds->client_id, creds->client_secret);
    if (creds->redirect_uri[0] && strcmp(creds->redirect_uri, SPOTICLI_DEFAULT_REDIRECT_URI) != 0 &&
        !strpbrk(creds->redirect_uri, "\"\\"))
        fprintf(f, ",\n  \"redirect_uri\": \"%s\"", creds->redirect_uri);
    fprintf(f, "\n}\n");
    fclose(f);
    return true;
}

bool config_load_credentials(credentials_t *creds)
{
    memset(creds, 0, sizeof(*creds));
    load_file(creds);

    const char *env;
    if ((env = getenv("SPOTIFY_CLIENT_ID")) && env[0])
        copy_str(creds->client_id, sizeof(creds->client_id), env);
    if ((env = getenv("SPOTIFY_CLIENT_SECRET")) && env[0])
        copy_str(creds->client_secret, sizeof(creds->client_secret), env);
    if ((env = getenv("SPOTIFY_REDIRECT_URI")) && env[0])
        copy_str(creds->redirect_uri, sizeof(creds->redirect_uri), env);

    if (!creds->redirect_uri[0])
        copy_str(creds->redirect_uri, sizeof(creds->redirect_uri), SPOTICLI_DEFAULT_REDIRECT_URI);

    return creds->client_id[0] && creds->client_secret[0];
}

/* ------------------------------------------------------------------ setup */

bool config_run_setup(credentials_t *creds)
{
    char buf[64];

    fprintf(stderr,
        "\n"
        "  SpotiCLI setup\n"
        "  --------------\n"
        "  SpotiCLI needs your own Spotify app credentials (one-time setup).\n"
        "\n"
        "   1. Open %s and click \"Create app\".\n"
        "   2. Add this Redirect URI exactly:  %s\n"
        "      and tick \"Web API\".\n"
        "   3. Open the app's Settings and copy the Client ID and Client secret.\n"
        "\n",
        DASHBOARD_URL, SPOTICLI_DEFAULT_REDIRECT_URI);

    fprintf(stderr, "  Open the Spotify dashboard in your browser now? [Y/n]: ");
    fflush(stderr);
    if (!read_line(buf, sizeof(buf), false)) return false;
    if (buf[0] != 'n' && buf[0] != 'N') open_url(DASHBOARD_URL);

    fprintf(stderr, "\n  Paste the values below (leave empty to cancel).\n");
    credentials_t fresh;
    memset(&fresh, 0, sizeof(fresh));
    if (!prompt_key("Client ID", fresh.client_id, sizeof(fresh.client_id), false)) goto cancelled;
    if (!prompt_key("Client secret", fresh.client_secret, sizeof(fresh.client_secret), true)) goto cancelled;
    copy_str(fresh.redirect_uri, sizeof(fresh.redirect_uri), SPOTICLI_DEFAULT_REDIRECT_URI);

    if (!save_file(&fresh)) return false;
    fprintf(stderr, "\n  Saved to %s\n", config_credentials_path());

    if ((getenv("SPOTIFY_CLIENT_ID") && getenv("SPOTIFY_CLIENT_ID")[0]) ||
        (getenv("SPOTIFY_CLIENT_SECRET") && getenv("SPOTIFY_CLIENT_SECRET")[0]))
        fprintf(stderr,
            "  Note: SPOTIFY_CLIENT_ID / SPOTIFY_CLIENT_SECRET environment variables are set\n"
            "  and take priority over the saved file. Remove them to use these values.\n");
    fprintf(stderr, "\n");

    *creds = fresh;
    return true;

cancelled:
    fprintf(stderr, "\n  Setup cancelled. Run `spoticli --setup` any time to try again.\n");
    return false;
}
