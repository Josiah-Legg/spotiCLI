#ifndef SPOTICLI_ALBUM_ART_H
#define SPOTICLI_ALBUM_ART_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Album art configuration */
typedef struct {
    int width;              /* Output width in characters */
    bool colored;           /* Use color output */
    bool reverse_chars;     /* Reverse character mapping */
    char *character_ramp;   /* Custom character set for ASCII art */
} album_art_config_t;

/* Album art cache */
typedef struct {
    char *ascii_output;     /* Rendered ASCII art */
    int width;
    int height;
    char *cover_url;        /* Original cover URL */
    uint8_t *image_data;    /* Cached image data */
    int image_width;
    int image_height;
} album_art_cache_t;

/* Initialize album art module */
bool album_art_init(void);

/* Create default configuration */
album_art_config_t *album_art_config_create(int width, bool colored);

/* Free configuration */
void album_art_config_free(album_art_config_t *config);

/* Convert image buffer to ASCII art */
char *album_art_convert(const uint8_t *image_data, int width, int height,
                        const album_art_config_t *config);

/* Load image from file and convert to ASCII */
char *album_art_from_file(const char *filepath, const album_art_config_t *config);

/* Load image from URL and convert to ASCII (requires HTTP) */
char *album_art_from_url(const char *url, const char *token, const album_art_config_t *config);

/* Decode an in-memory image (e.g. PNG/JPEG bytes) and resize to width x height
   3-channel RGB. Caller frees the returned buffer with free(). Returns NULL on
   failure. Useful when you want to convert the same image at multiple sizes. */
uint8_t *album_art_decode_resize(const uint8_t *data, size_t size, int width, int height);

/* Free ASCII art output */
void album_art_free(char *ascii_output);

/* Procedural placeholder art seeded from a string (e.g. track id+name).
   Writes up to out_size bytes into out, including ANSI color codes. */
void album_art_generate_placeholder(char *out, int out_size,
                                     int width, int height, const char *seed_str);

/* Create cache */
album_art_cache_t *album_art_cache_create(void);

/* Free cache */
void album_art_cache_free(album_art_cache_t *cache);

/* Cache album art */
void album_art_cache_set(album_art_cache_t *cache, const char *cover_url,
                         const char *ascii_output, int width, int height);

/* Get cached album art */
const char *album_art_cache_get(album_art_cache_t *cache, const char *cover_url,
                                 int *width, int *height);

#endif
