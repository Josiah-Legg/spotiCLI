#include "lyrics.h"
#include "http.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

bool lyrics_init(lyrics_config_t *config)
{
    if (!config) return false;
    config->preferred_source = LYRICS_SOURCE_SPOTIFY;
    config->cache_enabled = true;
    return true;
}

/* Extract a top-level string field from a JSON body without going through
   json_util's small static buffer (lyric blobs run several KB).  Caller frees.
   Returns NULL if the key isn't present or its value is null/non-string. */
static char *extract_json_string_field(const char *json, const char *key)
{
    if (!json || !key) return NULL;
    char needle[64];
    int nlen = snprintf(needle, sizeof(needle), "\"%s\"", key);
    if (nlen <= 0 || nlen >= (int)sizeof(needle)) return NULL;

    const char *p = strstr(json, needle);
    if (!p) return NULL;
    p += nlen;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != ':') return NULL;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') return NULL;  /* null or non-string */
    p++;

    /* Locate closing quote, handling escapes. */
    const char *start = p;
    while (*p) {
        if (*p == '\\' && p[1]) { p += 2; continue; }
        if (*p == '"') break;
        p++;
    }
    if (*p != '"') return NULL;

    size_t raw_len = (size_t)(p - start);
    char *out = (char *)malloc(raw_len + 1);
    if (!out) return NULL;

    size_t oi = 0;
    for (size_t i = 0; i < raw_len; ) {
        char c = start[i];
        if (c == '\\' && i + 1 < raw_len) {
            char e = start[i + 1];
            switch (e) {
            case 'n':  out[oi++] = '\n'; break;
            case 't':  out[oi++] = '\t'; break;
            case 'r':  out[oi++] = '\r'; break;
            case '"':  out[oi++] = '"';  break;
            case '\\': out[oi++] = '\\'; break;
            case '/':  out[oi++] = '/';  break;
            case 'u':
                if (i + 5 < raw_len) {
                    unsigned cp = 0;
                    int ok = 1;
                    for (int k = 0; k < 4; k++) {
                        char h = start[i + 2 + k];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                        else { ok = 0; break; }
                    }
                    if (ok) {
                        if (cp < 0x80) {
                            out[oi++] = (char)cp;
                        } else if (cp < 0x800) {
                            out[oi++] = (char)(0xC0 | (cp >> 6));
                            out[oi++] = (char)(0x80 | (cp & 0x3F));
                        } else {
                            out[oi++] = (char)(0xE0 | (cp >> 12));
                            out[oi++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                            out[oi++] = (char)(0x80 | (cp & 0x3F));
                        }
                        i += 6;
                        continue;
                    }
                }
                out[oi++] = e;
                break;
            default: out[oi++] = e; break;
            }
            i += 2;
        } else {
            out[oi++] = c;
            i++;
        }
    }
    out[oi] = 0;
    return out;
}

/* Parse one [mm:ss.xx] (or [mm:ss.xxx]) timestamp.  Returns ms, or -1 if not
   a timestamp.  On success, *after points just past the closing bracket. */
static int parse_lrc_timestamp(const char *p, const char **after)
{
    if (*p != '[') return -1;
    const char *q = p + 1;
    if (!isdigit((unsigned char)*q)) return -1;

    int mm = 0;
    while (isdigit((unsigned char)*q)) { mm = mm * 10 + (*q - '0'); q++; }
    if (*q != ':') return -1;
    q++;

    if (!isdigit((unsigned char)*q)) return -1;
    int ss = 0;
    while (isdigit((unsigned char)*q)) { ss = ss * 10 + (*q - '0'); q++; }

    int frac = 0, frac_digits = 0;
    if (*q == '.' || *q == ':') {
        q++;
        while (isdigit((unsigned char)*q) && frac_digits < 3) {
            frac = frac * 10 + (*q - '0');
            q++; frac_digits++;
        }
        while (isdigit((unsigned char)*q)) q++;
    }
    if (*q != ']') return -1;

    while (frac_digits < 3) { frac *= 10; frac_digits++; }

    *after = q + 1;
    return mm * 60000 + ss * 1000 + frac;
}

/* Append synced LRC text into state->lyrics. */
static int load_synced_lyrics(player_state_t *state, const char *lrc)
{
    int added = 0;
    const char *p = lrc;
    while (*p) {
        const char *line_start = p;
        const char *line_end = p;
        while (*line_end && *line_end != '\n') line_end++;

        /* A line may carry multiple timestamps for the same text. */
        int stamps[16];
        int n_stamps = 0;
        const char *cursor = line_start;
        while (cursor < line_end) {
            const char *after;
            int ms = parse_lrc_timestamp(cursor, &after);
            if (ms < 0) break;
            if (n_stamps < (int)(sizeof(stamps) / sizeof(stamps[0]))) {
                stamps[n_stamps++] = ms;
            }
            cursor = after;
        }

        if (n_stamps > 0) {
            while (cursor < line_end &&
                   (*cursor == ' ' || *cursor == '\t')) cursor++;

            char text[1024];
            size_t tlen = (size_t)(line_end - cursor);
            if (tlen >= sizeof(text)) tlen = sizeof(text) - 1;
            memcpy(text, cursor, tlen);
            text[tlen] = 0;
            while (tlen > 0 && (text[tlen - 1] == '\r' || text[tlen - 1] == ' '))
                text[--tlen] = 0;

            for (int i = 0; i < n_stamps; i++) {
                lyrics_add(state, stamps[i], text[0] ? text : " ");
                added++;
            }
        }

        p = (*line_end == '\n') ? line_end + 1 : line_end;
    }
    return added;
}

static int load_plain_lyrics(player_state_t *state, const char *plain, int duration_ms)
{
    /* Two passes: first count lines so we can spread them across duration. */
    int total = 0;
    for (const char *q = plain; *q; ) {
        const char *e = q;
        while (*e && *e != '\n') e++;
        total++;
        q = (*e == '\n') ? e + 1 : e;
    }
    if (total == 0) return 0;
    int span_ms = duration_ms > 0 ? duration_ms : total * 3000;

    int idx = 0;
    const char *p = plain;
    while (*p) {
        const char *line_end = p;
        while (*line_end && *line_end != '\n') line_end++;
        char text[1024];
        size_t tlen = (size_t)(line_end - p);
        if (tlen >= sizeof(text)) tlen = sizeof(text) - 1;
        memcpy(text, p, tlen);
        text[tlen] = 0;
        while (tlen > 0 && (text[tlen - 1] == '\r' || text[tlen - 1] == ' '))
            text[--tlen] = 0;
        int ts = (int)((long long)span_ms * idx / total);
        lyrics_add(state, ts, text);
        idx++;
        p = (*line_end == '\n') ? line_end + 1 : line_end;
    }
    return idx;
}

static bool fetch_from_lrclib(const char *track_name, const char *artist_name,
                              const char *album_name, int duration_ms,
                              player_state_t *state, bool *out_synced)
{
    *out_synced = false;
    char *etrack  = http_urlencode(track_name);
    char *eartist = http_urlencode(artist_name);
    char *ealbum  = http_urlencode(album_name && album_name[0] ? album_name : "");
    if (!etrack || !eartist) {
        http_free_encoded(etrack); http_free_encoded(eartist); http_free_encoded(ealbum);
        return false;
    }

    char url[2048];
    int dur_s = duration_ms > 0 ? duration_ms / 1000 : 0;
    snprintf(url, sizeof(url),
             "https://lrclib.net/api/get?track_name=%s&artist_name=%s&album_name=%s&duration=%d",
             etrack, eartist, ealbum ? ealbum : "", dur_s);

    http_response_t *resp = http_request(HTTP_GET, url, NULL, NULL, NULL);

    /* If the strict /get failed, fall back to /search which is more forgiving. */
    if (!resp || resp->status_code != 200) {
        if (resp) http_response_free(resp);
        char url2[2048];
        snprintf(url2, sizeof(url2),
                 "https://lrclib.net/api/search?track_name=%s&artist_name=%s",
                 etrack, eartist);
        resp = http_request(HTTP_GET, url2, NULL, NULL, NULL);
    }

    http_free_encoded(etrack);
    http_free_encoded(eartist);
    http_free_encoded(ealbum);

    if (!resp) return false;
    if (resp->status_code != 200 || !resp->body || !resp->body[0]) {
        http_response_free(resp);
        return false;
    }

    char *synced = extract_json_string_field(resp->body, "syncedLyrics");
    char *plain  = NULL;
    if (!synced || !synced[0]) {
        free(synced); synced = NULL;
        plain = extract_json_string_field(resp->body, "plainLyrics");
    }
    http_response_free(resp);

    bool ok = false;
    if (synced && synced[0]) {
        if (load_synced_lyrics(state, synced) > 0) {
            ok = true;
            *out_synced = true;
        }
    } else if (plain && plain[0]) {
        if (load_plain_lyrics(state, plain, state->current_track.duration_ms) > 0) ok = true;
    }
    free(synced);
    free(plain);
    return ok;
}

bool lyrics_fetch(const char *track_id, const char *track_name, const char *artist_name,
                  player_state_t *state)
{
    if (!track_id || !track_name || !artist_name || !state) return false;

    fprintf(stderr, "[lyrics] Fetching lyrics for '%s' by '%s'\n",
            track_name, artist_name);

    lyrics_clear(state);

    bool synced = false;
    bool ok = fetch_from_lrclib(track_name, artist_name,
                                state->current_track.album,
                                state->current_track.duration_ms,
                                state, &synced);

    if (!ok) {
        char header[600];
        snprintf(header, sizeof(header), "♪ %s ♪", track_name);
        lyrics_add(state, 0, header);
        snprintf(header, sizeof(header), "by %s", artist_name);
        lyrics_add(state, 1, header);
        lyrics_add(state, 2, "");
        lyrics_add(state, 3, "(no lyrics found on LRCLIB)");
        return false;
    }

    state->current_lyric_index = 0;
    return true;
}

void lyrics_update_position(player_state_t *state, int progress_ms)
{
    if (!state || state->lyrics_count == 0) return;

    int adjusted_ms = progress_ms - 300;

    int best_idx = 0;
    for (int i = 0; i < state->lyrics_count; i++) {
        if (state->lyrics[i].timestamp_ms <= adjusted_ms) {
            best_idx = i;
        } else {
            break;
        }
    }

    state->current_lyric_index = best_idx;
}

const char *lyrics_get_current(player_state_t *state)
{
    if (!state || state->lyrics_count == 0) return NULL;
    if (state->current_lyric_index < 0 || state->current_lyric_index >= state->lyrics_count) {
        return NULL;
    }
    return state->lyrics[state->current_lyric_index].text;
}

const char *lyrics_get_next(player_state_t *state)
{
    if (!state || state->lyrics_count == 0) return NULL;
    int next_idx = state->current_lyric_index + 1;
    if (next_idx >= state->lyrics_count) return NULL;
    return state->lyrics[next_idx].text;
}

const char *lyrics_get_previous(player_state_t *state)
{
    if (!state || state->lyrics_count == 0) return NULL;
    int prev_idx = state->current_lyric_index - 1;
    if (prev_idx < 0) return NULL;
    return state->lyrics[prev_idx].text;
}

char *lyrics_format_display(player_state_t *state, int max_width)
{
    if (!state) return NULL;

    int buffer_size = 4096;
    char *output = (char *)malloc(buffer_size);
    if (!output) return NULL;

    int pos = 0;

    const char *prev = lyrics_get_previous(state);
    if (prev) {
        pos += snprintf(output + pos, buffer_size - pos, "\x1b[2m%s\x1b[0m\n", prev);
    }

    const char *current = lyrics_get_current(state);
    if (current) {
        pos += snprintf(output + pos, buffer_size - pos, "\x1b[1m%s\x1b[0m\n", current);
    }

    const char *next = lyrics_get_next(state);
    if (next) {
        pos += snprintf(output + pos, buffer_size - pos, "\x1b[2m%s\x1b[0m\n", next);
    }

    (void)max_width;
    return output;
}

void lyrics_free_formatted(char *formatted)
{
    if (formatted) free(formatted);
}
