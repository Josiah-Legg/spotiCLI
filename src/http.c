#include "http.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

/*
 * HTTP backend.
 *
 * - On Windows: WinHTTP (built into the OS, no extra deps).
 * - --demo / SPOTICLI_DEMO=1: returns canned JSON so the UI runs without
 *   network access.
 *
 * The demo path covers the few endpoints the player actually pokes at
 * idle. Anything else returns an empty 200 so JSON parsers don't crash.
 */

static bool g_demo_mode = false;

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>

static HINTERNET g_session = NULL;

static wchar_t *utf8_to_w(const char *s)
{
    if (!s) return NULL;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

#endif /* _WIN32 */

void http_init(void)
{
    const char *d = getenv("SPOTICLI_DEMO");
    if (d && (d[0] == '1' || d[0] == 't' || d[0] == 'T')) g_demo_mode = true;

#ifdef _WIN32
    if (!g_demo_mode) {
        g_session = WinHttpOpen(L"SpotiCLI/1.0",
                                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS, 0);
        if (!g_session) {
            fprintf(stderr, "[http] WinHttpOpen failed; falling back to demo\n");
            g_demo_mode = true;
        }
    }
#else
    g_demo_mode = true;
#endif
}

void http_cleanup(void)
{
#ifdef _WIN32
    if (g_session) { WinHttpCloseHandle(g_session); g_session = NULL; }
#endif
}

void http_set_demo_mode(bool enabled) { g_demo_mode = enabled; }
bool http_is_demo_mode(void)         { return g_demo_mode; }

/* ------------------------------------------------------------------ demo  */

static http_response_t *make_resp(int code, const char *body)
{
    http_response_t *r = (http_response_t *)malloc(sizeof(http_response_t));
    if (!r) return NULL;
    r->status_code = code;
    if (body) {
        size_t n = strlen(body);
        r->body = (char *)malloc(n + 1);
        if (!r->body) { free(r); return NULL; }
        memcpy(r->body, body, n + 1);
        r->size = n;
    } else {
        r->body = (char *)malloc(1);
        if (r->body) r->body[0] = 0;
        r->size = 0;
    }
    return r;
}

static http_response_t *demo_response(const char *url)
{
    if (strstr(url, "/me/player/currently-playing")) {
        return make_resp(200,
            "{"
            "\"item\":{"
                "\"id\":\"demo-track-001\","
                "\"name\":\"Midnight City\","
                "\"album\":{"
                    "\"name\":\"Hurry Up, We're Dreaming\","
                    "\"images\":[{\"url\":\"https://i.scdn.co/image/ab67616d0000b2734f6d6e6e6e6e6e6e6e6e6e6e\"}]"
                "},"
                "\"artists\":[{\"name\":\"M83\"}],"
                "\"duration_ms\":244000"
            "},"
            "\"progress_ms\":62000,"
            "\"is_playing\":true"
            "}");
    }
    if (strstr(url, "/me/player/queue")) {
        return make_resp(200,
            "{\"currently_playing\":{},"
             "\"queue\":["
               "{\"id\":\"track-002\",\"name\":\"We Own The Sky\",\"artists\":[{\"name\":\"M83\"}]},"
               "{\"id\":\"track-003\",\"name\":\"Outro\",\"artists\":[{\"name\":\"M83\"}]},"
               "{\"id\":\"track-004\",\"name\":\"Wait\",\"artists\":[{\"name\":\"M83\"}]}"
             "]}");
    }
    if (strstr(url, "/search"))    return make_resp(200, "{\"tracks\":{\"items\":[]}}");
    if (strstr(url, "/me/player")) return make_resp(204, "");
    return make_resp(200, "{}");
}

/* ------------------------------------------------------------------ winhttp */

#ifdef _WIN32

static const char *method_str(http_method_e m)
{
    switch (m) {
    case HTTP_GET:    return "GET";
    case HTTP_POST:   return "POST";
    case HTTP_PUT:    return "PUT";
    case HTTP_DELETE: return "DELETE";
    }
    return "GET";
}

static http_response_t *winhttp_request(http_method_e method, const char *url,
                                        const char *access_token,
                                        const char *body, const char *content_type)
{
    if (!g_session) return NULL;

    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {0};
    wchar_t path[2048] = {0};
    uc.lpszHostName = host;     uc.dwHostNameLength = (DWORD)(sizeof(host) / sizeof(host[0]));
    uc.lpszUrlPath = path;      uc.dwUrlPathLength = (DWORD)(sizeof(path) / sizeof(path[0]));
    uc.dwSchemeLength = (DWORD)-1;

    wchar_t *wurl = utf8_to_w(url);
    if (!wurl) return NULL;

    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) {
        free(wurl);
        return make_resp(0, "{\"error\":\"bad url\"}");
    }

    HINTERNET conn = WinHttpConnect(g_session, host, uc.nPort, 0);
    if (!conn) { free(wurl); return make_resp(0, "{\"error\":\"connect failed\"}"); }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    wchar_t wmethod[16];
    MultiByteToWideChar(CP_UTF8, 0, method_str(method), -1, wmethod, 16);

    HINTERNET req = WinHttpOpenRequest(conn, wmethod, path, NULL,
                                       WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) { WinHttpCloseHandle(conn); free(wurl); return make_resp(0, "{\"error\":\"open req\"}"); }

    /* headers */
    char hdr[2048];
    int hlen = 0;
    if (access_token && access_token[0]) {
        hlen += snprintf(hdr + hlen, sizeof(hdr) - hlen,
                         "Authorization: Bearer %s\r\n", access_token);
    }
    if (content_type && content_type[0]) {
        hlen += snprintf(hdr + hlen, sizeof(hdr) - hlen,
                         "Content-Type: %s\r\n", content_type);
    }
    if (hlen > 0) {
        wchar_t *whdr = utf8_to_w(hdr);
        if (whdr) {
            WinHttpAddRequestHeaders(req, whdr, (DWORD)-1L,
                                     WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
            free(whdr);
        }
    }

    DWORD body_len = body ? (DWORD)strlen(body) : 0;
    BOOL ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 body ? (LPVOID)body : WINHTTP_NO_REQUEST_DATA,
                                 body_len, body_len, 0);
    if (!ok) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); free(wurl);
        return make_resp(0, "{\"error\":\"send failed\"}");
    }

    if (!WinHttpReceiveResponse(req, NULL)) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); free(wurl);
        return make_resp(0, "{\"error\":\"recv failed\"}");
    }

    DWORD status = 0;
    DWORD status_sz = sizeof(status);
    WinHttpQueryHeaders(req,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_sz,
                        WINHTTP_NO_HEADER_INDEX);

    /* read body */
    size_t cap = 8192, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); free(wurl);
        return make_resp((int)status, "");
    }

    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
        if (len + avail + 1 > cap) {
            while (len + avail + 1 > cap) cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); buf = NULL; break; }
            buf = nb;
        }
        DWORD got = 0;
        if (!WinHttpReadData(req, buf + len, avail, &got)) break;
        if (got == 0) break;
        len += got;
    }
    if (buf) buf[len] = 0;

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    free(wurl);

    http_response_t *r = (http_response_t *)malloc(sizeof(http_response_t));
    if (!r) { free(buf); return NULL; }
    r->status_code = (int)status;
    r->body = buf ? buf : (char *)calloc(1, 1);
    r->size = len;
    return r;
}

static http_binary_response_t *winhttp_download_binary(const char *url, const char *access_token)
{
    if (!g_session) return NULL;

    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {0};
    wchar_t path[2048] = {0};
    uc.lpszHostName = host;     uc.dwHostNameLength = (DWORD)(sizeof(host) / sizeof(host[0]));
    uc.lpszUrlPath = path;      uc.dwUrlPathLength = (DWORD)(sizeof(path) / sizeof(path[0]));
    uc.dwSchemeLength = (DWORD)-1;

    wchar_t *wurl = utf8_to_w(url);
    if (!wurl) return NULL;

    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) {
        free(wurl);
        return NULL;
    }

    HINTERNET conn = WinHttpConnect(g_session, host, uc.nPort, 0);
    if (!conn) { free(wurl); return NULL; }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    wchar_t wmethod[16];
    MultiByteToWideChar(CP_UTF8, 0, "GET", -1, wmethod, 16);

    HINTERNET req = WinHttpOpenRequest(conn, wmethod, path, NULL,
                                       WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) { WinHttpCloseHandle(conn); free(wurl); return NULL; }

    char hdr[512];
    if (access_token && access_token[0]) {
        snprintf(hdr, sizeof(hdr), "Authorization: Bearer %s\r\n", access_token);
        wchar_t *whdr = utf8_to_w(hdr);
        if (whdr) {
            WinHttpAddRequestHeaders(req, whdr, (DWORD)-1L,
                                     WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
            free(whdr);
        }
    }

    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0)) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); free(wurl); return NULL;
    }

    if (!WinHttpReceiveResponse(req, NULL)) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); free(wurl); return NULL;
    }

    http_binary_response_t *mem = malloc(sizeof(http_binary_response_t));
    if (!mem) return NULL;
    mem->data = malloc(1);
    mem->size = 0;

    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
        void *buf = malloc(avail);
        if (!buf) break;
        DWORD got = 0;
        if (!WinHttpReadData(req, buf, avail, &got)) { free(buf); break; }

        char *nb = realloc(mem->data, mem->size + got);
        if (!nb) { free(buf); break; }
        mem->data = nb;
        memcpy(mem->data + mem->size, buf, got);
        mem->size += got;
        free(buf);
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    free(wurl);
    return mem;
}

#endif /* _WIN32 */

/* ------------------------------------------------------------------ entry */

http_response_t *http_request(http_method_e method, const char *url,
                              const char *access_token,
                              const char *body, const char *content_type)
{
    if (!url) return NULL;

    fprintf(stderr, "[http] %s %s%s\n",
            method == HTTP_GET ? "GET" :
            method == HTTP_POST ? "POST" :
            method == HTTP_PUT ? "PUT" : "DEL",
            url,
            g_demo_mode ? "  (demo)" : "");

    if (g_demo_mode) return demo_response(url);

#ifdef _WIN32
    return winhttp_request(method, url, access_token, body, content_type);
#else
    (void)method; (void)access_token; (void)body; (void)content_type;
    return demo_response(url);
#endif
}

http_binary_response_t *http_download_binary(const char *url, const char *token)
{
    if (!url) return NULL;
    if (g_demo_mode) return NULL;
#ifdef _WIN32
    return winhttp_download_binary(url, token);
#else
    return NULL;
#endif
}

void http_binary_response_free(http_binary_response_t *resp)
{
    if (!resp) return;
    if (resp->data) free(resp->data);
    free(resp);
}

void http_response_free(http_response_t *r)
{
    if (!r) return;
    if (r->body) free(r->body);
    free(r);
}

char *http_urlencode(const char *str)
{
    if (!str) return NULL;
    size_t n = strlen(str);
    char *out = (char *)malloc(n * 3 + 1);
    if (!out) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)str[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out[j++] = (char)c;
        } else {
            sprintf(out + j, "%%%02X", c);
            j += 3;
        }
    }
    out[j] = 0;
    return out;
}

void http_free_encoded(char *s) { if (s) free(s); }