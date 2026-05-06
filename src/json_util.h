#ifndef SPOTICLI_JSON_H
#define SPOTICLI_JSON_H

#include <stdbool.h>

/* Simple JSON parsing - uses string search
   For full JSON parsing, integrate jansson library */

typedef struct {
    char *raw;
    int size;
} json_t;

/* Safe JSON getters with null checking */
const char *json_get_string(json_t *obj, const char *key, const char *default_val);
int json_get_int(json_t *obj, const char *key, int default_val);
bool json_get_bool(json_t *obj, const char *key, bool default_val);
json_t *json_get_object(json_t *obj, const char *key);
json_t *json_get_array(json_t *obj, const char *key);

/* Parse JSON from string */
json_t *json_parse_string(const char *json_str);

/* Free JSON object */
void json_decref(json_t *obj);

/* Get nested value by path (e.g., "item.track.name") */
const char *json_get_nested_string(json_t *obj, const char *path, const char *default_val);

#endif
