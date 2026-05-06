#include "json_util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/*
 * Pragmatic JSON value extraction.
 *
 * Supports dotted paths with optional array indexing, e.g.:
 *   "item.name"
 *   "item.artists[0].name"
 *   "item.album.images[0].url"
 *
 * Implementation: walks the JSON text resolving one path segment at a time.
 * For each segment we find "key":, skip its value (handling nested {}, []
 * and quoted strings with escapes), then narrow the search range to that
 * value before descending. For [N] suffixes we walk array commas at the
 * top level of that array.
 *
 * This is not a real parser — it assumes well-formed JSON and unique
 * keys at each nesting level (which is true for the Spotify responses
 * we hit). It is enough to drive the player.
 */

static const char *skip_ws(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

/* Given p pointing at the first char of a JSON value, return pointer
   just past the end of that value. Handles strings (with \" escapes),
   objects, arrays, and primitives. */
static const char *skip_value(const char *p, const char *end)
{
    if (p >= end) return p;
    if (*p == '"') {
        p++;
        while (p < end) {
            if (*p == '\\' && p + 1 < end) { p += 2; continue; }
            if (*p == '"') return p + 1;
            p++;
        }
        return p;
    }
    if (*p == '{' || *p == '[') {
        char open = *p;
        char close = open == '{' ? '}' : ']';
        int depth = 0;
        while (p < end) {
            if (*p == '"') { p = skip_value(p, end); continue; }
            if (*p == open)  depth++;
            else if (*p == close) { depth--; if (depth == 0) return p + 1; }
            p++;
        }
        return p;
    }
    /* primitive: number, true, false, null */
    while (p < end && *p != ',' && *p != '}' && *p != ']'
                   && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t')
        p++;
    return p;
}

/* Find "key": at top level within [start, end), return pointer just past
   the colon (so caller can read the value). NULL if not found. */
static const char *find_key_in_object(const char *start, const char *end,
                                      const char *key)
{
    if (start >= end || *start != '{') return NULL;
    const char *p = start + 1;
    size_t klen = strlen(key);

    while (p < end) {
        p = skip_ws(p, end);
        if (p >= end || *p == '}') return NULL;
        if (*p != '"') return NULL;

        const char *kstart = p + 1;
        const char *kend = skip_value(p, end);
        size_t cur_klen = (size_t)(kend - 1 - kstart);

        p = skip_ws(kend, end);
        if (p >= end || *p != ':') return NULL;
        p++;
        p = skip_ws(p, end);

        if (cur_klen == klen && memcmp(kstart, key, klen) == 0) {
            return p; /* points at value start */
        }

        const char *vend = skip_value(p, end);
        p = skip_ws(vend, end);
        if (p < end && *p == ',') p++;
    }
    return NULL;
}

/* Pick element idx from an array; on success returns pointer at element
   value start, and writes the element end into *out_end. */
static const char *index_array(const char *start, const char *end, int idx,
                               const char **out_end)
{
    if (start >= end || *start != '[') return NULL;
    const char *p = start + 1;
    int i = 0;
    while (p < end) {
        p = skip_ws(p, end);
        if (p >= end || *p == ']') return NULL;
        const char *vstart = p;
        const char *vend = skip_value(p, end);
        if (i == idx) { *out_end = vend; return vstart; }
        p = skip_ws(vend, end);
        if (p < end && *p == ',') p++;
        i++;
    }
    return NULL;
}

/* Resolve a dotted path inside [start, end), filling *vstart/*vend with
   the bounds of the value. Supports key, key.subkey, key[N], key[N].sub */
static int resolve_path(const char *start, const char *end, const char *path,
                        const char **vstart, const char **vend)
{
    char buf[256];
    size_t plen = strlen(path);
    if (plen >= sizeof(buf)) return 0;
    memcpy(buf, path, plen + 1);

    const char *cur_start = start;
    const char *cur_end = end;

    char *seg = buf;
    while (*seg) {
        char *dot = strchr(seg, '.');
        if (dot) *dot = 0;

        /* split off optional [N] */
        int idx = -1;
        char *br = strchr(seg, '[');
        if (br) {
            *br = 0;
            idx = atoi(br + 1);
        }

        /* descend into key */
        if (*seg) {
            const char *vp = find_key_in_object(cur_start, cur_end, seg);
            if (!vp) return 0;
            const char *ve = skip_value(vp, cur_end);
            cur_start = vp;
            cur_end = ve;
        }

        if (idx >= 0) {
            const char *ae;
            const char *ap = index_array(cur_start, cur_end, idx, &ae);
            if (!ap) return 0;
            cur_start = ap;
            cur_end = ae;
        }

        if (!dot) break;
        seg = dot + 1;
    }

    *vstart = cur_start;
    *vend   = cur_end;
    return 1;
}

/* ----------------------------------------------------------------- public */

const char *json_get_string(json_t *obj, const char *path, const char *def)
{
    if (!obj || !obj->raw || !path) return def;
    static char result[2048];
    const char *start = obj->raw;
    const char *end = obj->raw + obj->size;

    const char *vs, *ve;
    if (!resolve_path(start, end, path, &vs, &ve)) return def;
    if (vs >= ve || *vs != '"') return def;

    /* copy the unescaped string contents */
    const char *p = vs + 1;
    const char *strend = ve - 1; /* points at closing quote */
    size_t i = 0;
    while (p < strend && i < sizeof(result) - 1) {
        if (*p == '\\' && p + 1 < strend) {
            char c = p[1];
            switch (c) {
            case 'n':  result[i++] = '\n'; break;
            case 't':  result[i++] = '\t'; break;
            case 'r':  result[i++] = '\r'; break;
            case '"':  result[i++] = '"';  break;
            case '\\': result[i++] = '\\'; break;
            case '/':  result[i++] = '/';  break;
            default:   result[i++] = c;    break;
            }
            p += 2;
        } else {
            result[i++] = *p++;
        }
    }
    result[i] = 0;
    return i ? result : def;
}

int json_get_int(json_t *obj, const char *path, int def)
{
    if (!obj || !obj->raw || !path) return def;
    const char *vs, *ve;
    if (!resolve_path(obj->raw, obj->raw + obj->size, path, &vs, &ve)) return def;
    if (vs >= ve) return def;
    if (*vs == '"') vs++; /* tolerate stringy numbers */
    if (!(*vs == '-' || (*vs >= '0' && *vs <= '9'))) return def;
    return (int)strtol(vs, NULL, 10);
}

bool json_get_bool(json_t *obj, const char *path, bool def)
{
    if (!obj || !obj->raw || !path) return def;
    const char *vs, *ve;
    if (!resolve_path(obj->raw, obj->raw + obj->size, path, &vs, &ve)) return def;
    if (vs >= ve) return def;
    if (ve - vs >= 4 && memcmp(vs, "true", 4) == 0)  return true;
    if (ve - vs >= 5 && memcmp(vs, "false", 5) == 0) return false;
    return def;
}

json_t *json_get_object(json_t *o, const char *k) { (void)o; (void)k; return NULL; }
json_t *json_get_array (json_t *o, const char *k) { (void)o; (void)k; return NULL; }

json_t *json_parse_string(const char *s)
{
    if (!s) return NULL;
    json_t *o = (json_t *)malloc(sizeof(json_t));
    if (!o) return NULL;
    o->size = (int)strlen(s);
    o->raw = (char *)malloc((size_t)o->size + 1);
    if (!o->raw) { free(o); return NULL; }
    memcpy(o->raw, s, (size_t)o->size + 1);
    return o;
}

void json_decref(json_t *o)
{
    if (!o) return;
    if (o->raw) free(o->raw);
    free(o);
}

const char *json_get_nested_string(json_t *o, const char *path, const char *def)
{
    return json_get_string(o, path, def);
}
