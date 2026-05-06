#include "album_art.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"

#include "http.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdbool.h>

/* Character ramp from img2ascii - ordered by density (reversed: sparse to dense, no spaces) */
static const char *DEFAULT_CHAR_RAMP = ".'`^\"':;,Il!i><~+_-?][}{1)(|\\/ftrjxnucvzXYUJLCQ0OZmwqpkdbhao*#MW&8%B@$";

/* ============== img2ascii algorithm ============== */

/* Get pixel intensity using luminance formula */
static uint8_t get_intensity(const uint8_t *image, int i, int channels)
{
    if (channels < 3) return 128;
    uint8_t r = image[i * channels];
    uint8_t g = image[i * channels + 1];
    uint8_t b = image[i * channels + 2];
    return (uint8_t)round(0.299 * r + 0.587 * g + 0.114 * b);
}

/* Get RGB from pixel */
static void get_rgb(uint8_t *r, uint8_t *g, uint8_t *b, const uint8_t *image, int i, int channels)
{
    if (channels < 3) {
        *r = *g = *b = 128;
        return;
    }
    *r = image[i * channels];
    *g = image[i * channels + 1];
    *b = image[i * channels + 2];
}

/* Find character index from intensity */
static int char_index_from_intensity(int intensity, int char_count)
{
    if (char_count <= 1) return 0;
    int idx = (int)(intensity / (255.0f / (float)(char_count - 1)));
    if (idx < 0) idx = 0;
    if (idx >= char_count) idx = char_count - 1;
    return idx;
}

/* Load image from file and resize - from img2ascii */
static uint8_t *load_image(const char *input_filepath, int *desired_width, int *desired_height, bool resize_image)
{
    const int channels = 3;
    int width, height;

    uint8_t *image = stbi_load(input_filepath, &width, &height, NULL, channels);
    if (!image) {
        fprintf(stderr, "[art] Could not load image: %s\n", input_filepath);
        return NULL;
    }

    if (resize_image) {
        if (*desired_width <= 0) *desired_width = 80;
        if (*desired_width > width) {
            fprintf(stderr, "[art] Width cannot exceed original (%d)\n", width);
            *desired_width = width;
        }
        *desired_height = height / (width / (float)*desired_width) / 2;

        uint8_t *resized = (uint8_t *)malloc((size_t)*desired_width * (size_t)*desired_height * channels);
        if (!resized) {
            stbi_image_free(image);
            return NULL;
        }

        stbir_resize_uint8(image, width, height, width * channels,
                          resized, *desired_width, *desired_height, *desired_width * channels, channels);

        stbi_image_free(image);
        return resized;
    }

    *desired_width = width;
    *desired_height = height / 2;
    return image;
}

/* Load image from memory buffer (for HTTP downloads) */
static uint8_t *load_image_from_memory(const uint8_t *data, size_t size, int *desired_width, int *desired_height, bool resize)
{
    const int channels = 3;
    int width, height;

    uint8_t *image = stbi_load_from_memory(data, (int)size, &width, &height, NULL, channels);
    if (!image) {
        fprintf(stderr, "[art] Could not decode image from memory\n");
        return NULL;
    }

    if (resize) {
        if (*desired_width <= 0) *desired_width = 80;
        if (*desired_height <= 0) *desired_height = 32;

        uint8_t *resized = (uint8_t *)malloc((size_t)*desired_width * (size_t)*desired_height * channels);
        if (!resized) {
            stbi_image_free(image);
            return NULL;
        }

        stbir_resize_uint8(image, width, height, width * channels,
                          resized, *desired_width, *desired_height, *desired_width * channels, channels);

        fprintf(stderr, "[art] Resized from %dx%d to %dx%d\n", width, height, *desired_width, *desired_height);
        stbi_image_free(image);
        return resized;
    }

    *desired_width = width;
    *desired_height = height / 2;
    return image;
}

/* Generate grayscale ASCII output - from img2ascii */
static char *get_output_grayscale(uint8_t *image, int width, int height, char *characters)
{
    int output_size = height * width + height + 1;
    char *output = (char *)malloc(output_size * sizeof(char));
    if (!output) return NULL;

    int characters_count = strlen(characters);
    int ptr = 0;

    for (int i = 0; i < height * width; i++) {
        int intensity = get_intensity(image, i, 3);
        int char_index = char_index_from_intensity(intensity, characters_count);
        output[ptr] = characters[char_index];
        if ((i + 1) % width == 0) {
            output[++ptr] = '\n';
        }
        ptr++;
    }
    output[ptr] = '\0';
    return output;
}

/* Generate RGB colored ASCII output - from img2ascii */
static char *get_output_rgb(uint8_t *image, int width, int height, char *characters)
{
    /* Allocate enough for worst case: each pixel has full color code (19 bytes) + char + newline */
    /* 64x32 = 2048 pixels, each could have 19 byte color code + 1 char = 20 bytes, + 32 newlines + 4 reset */
    int length = (height * width * 24) + height + 10;
    char *output = (char *)malloc(length * sizeof(char));
    if (!output) return NULL;

    int characters_count = strlen(characters);
    int ptr = 0;
    uint8_t r_prev = 255, g_prev = 255, b_prev = 255;
    int line_count = 0;

    for (int i = 0; i < height * width; i++) {
        int intensity = get_intensity(image, i, 3);
        int char_index = char_index_from_intensity(intensity, characters_count);

        uint8_t r, g, b;
        get_rgb(&r, &g, &b, image, i, 3);

        /* Boost dark pixels so they remain visible on a black terminal background.
           Without this, low-RGB pixels render in near-black and the glyph disappears. */
        {
            const int MIN_LUMA = 60;
            int max_c = r > g ? (r > b ? r : b) : (g > b ? g : b);
            if (max_c < MIN_LUMA) {
                int scale = max_c > 0 ? (MIN_LUMA * 256) / max_c : 256 * MIN_LUMA;
                int nr = (r * scale) >> 8;
                int ng = (g * scale) >> 8;
                int nb = (b * scale) >> 8;
                if (max_c == 0) { nr = ng = nb = MIN_LUMA; }
                r = nr > 255 ? 255 : (uint8_t)nr;
                g = ng > 255 ? 255 : (uint8_t)ng;
                b = nb > 255 ? 255 : (uint8_t)nb;
            }
        }

        if (!(r == r_prev && g == g_prev && b == b_prev)) {
            ptr += snprintf(output + ptr, length - ptr, "\x1b[38;2;%i;%i;%im", r, g, b);
        }
        r_prev = r; g_prev = g; b_prev = b;
        output[ptr++] = characters[char_index];

        if ((i + 1) % width == 0) {
            output[ptr++] = '\n';
            line_count++;
        }
    }
    snprintf(output + ptr, length - ptr, "\x1b[0m");
    fprintf(stderr, "[art] Generated %d lines, %d chars, buffer size %d\n", line_count, ptr, length);
    return output;
}

/* ============== SpotiCLI API ============== */

bool album_art_init(void) { return true; }

album_art_config_t *album_art_config_create(int width, bool colored)
{
    album_art_config_t *config = (album_art_config_t *)malloc(sizeof(album_art_config_t));
    if (!config) return NULL;
    config->width = width > 0 ? width : 80;
    config->colored = colored;
    config->reverse_chars = false;
    config->character_ramp = strdup(DEFAULT_CHAR_RAMP);
    return config;
}

void album_art_config_free(album_art_config_t *config)
{
    if (!config) return;
    free(config->character_ramp);
    free(config);
}

char *album_art_convert(const uint8_t *image_data, int width, int height, const album_art_config_t *config)
{
    if (!image_data || !config || width <= 0 || height <= 0) return NULL;

    /* Make a copy of the ramp since we might modify it */
    char *ramp = strdup(config->character_ramp);
    if (!ramp) return NULL;

    char *result = config->colored
        ? get_output_rgb((uint8_t *)image_data, width, height, ramp)
        : get_output_grayscale((uint8_t *)image_data, width, height, ramp);

    free(ramp);
    return result;
}

char *album_art_from_file(const char *filepath, const album_art_config_t *config)
{
    if (!filepath || !config) return NULL;

    int width = config->width;
    int height = 0;

    uint8_t *image = load_image(filepath, &width, &height, true);
    if (!image) return NULL;

    char *result = album_art_convert(image, width, height, config);
    stbi_image_free(image);
    return result;
}

uint8_t *album_art_decode_resize(const uint8_t *data, size_t size, int width, int height)
{
    if (!data || size == 0 || width <= 0 || height <= 0) return NULL;
    const int channels = 3;
    int src_w, src_h;
    uint8_t *src = stbi_load_from_memory(data, (int)size, &src_w, &src_h, NULL, channels);
    if (!src) return NULL;

    uint8_t *dst = (uint8_t *)malloc((size_t)width * (size_t)height * channels);
    if (!dst) { stbi_image_free(src); return NULL; }

    stbir_resize_uint8(src, src_w, src_h, src_w * channels,
                       dst, width, height, width * channels, channels);
    stbi_image_free(src);
    return dst;
}

char *album_art_from_url(const char *url, const char *token, const album_art_config_t *config)
{
    if (!url || !config) return NULL;
    if (!url[0]) {
        fprintf(stderr, "[art] Empty URL, skipping\n");
        return NULL;
    }

    fprintf(stderr, "[art] Downloading album art from: %s\n", url);

    http_binary_response_t *bin = http_download_binary(url, token);
    if (!bin || !bin->data) {
        fprintf(stderr, "[art] Failed to download album art\n");
        if (bin) http_binary_response_free(bin);
        return NULL;
    }

    fprintf(stderr, "[art] Downloaded %zu bytes\n", bin->size);

    int width = config->width;
    int height = 32;  /* Fixed height for square appearance in terminal */

    uint8_t *image = load_image_from_memory((const uint8_t *)bin->data, bin->size, &width, &height, true);
    http_binary_response_free(bin);

    if (!image) {
        fprintf(stderr, "[art] Failed to decode image\n");
        return NULL;
    }

    fprintf(stderr, "[art] Image decoded: %dx%d pixels\n", width, height);

    char *result = album_art_convert(image, width, height, config);
    stbi_image_free(image);

    if (result) {
        fprintf(stderr, "[art] ASCII art generated successfully\n");
    } else {
        fprintf(stderr, "[art] Failed to generate ASCII art\n");
    }

    return result;
}

void album_art_free(char *ascii_output)
{
    if (ascii_output) free(ascii_output);
}

/* ============== Procedural Placeholder for Demo Mode ============== */

static unsigned int hash_string(const char *s)
{
    unsigned int h = 2166136261u;
    if (!s) return h;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h ? h : 1u;
}

static unsigned int xrand(unsigned int *st)
{
    unsigned int x = *st;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *st = x ? x : 0xA5A5A5A5u;
    return *st;
}

void album_art_generate_placeholder(char *out, int out_size,
                                    int width, int height, const char *seed_str)
{
    if (!out || out_size < 64 || width < 4 || height < 4) {
        if (out && out_size > 0) out[0] = 0;
        return;
    }

    unsigned int seed = hash_string(seed_str);
    unsigned int s1 = seed ^ 0xDEADBEEFu;
    unsigned int s2 = seed ^ 0xCAFEBABEu;

    /* Use the SAME character ramp as img2ascii - ordered from sparse to dense (reversed, no spaces) */
    static const char *ramp = ".'`^\"':;,Il!i><~+_-?][}{1)(|\\/ftrjxnucvzXYUJLCQ0OZmwqpkdbhao*#MW&8%B@$";
    int ramp_len = (int)strlen(ramp);

    /* Pick colors from album art palette */
    static const int colors[] = {196, 202, 208, 214, 220, 178, 172, 166, 167, 131, 97, 61, 63, 67, 72, 78, 114, 150, 186, 222, 229, 230, 255, 254, 252};
    int num_colors = (int)(sizeof(colors) / sizeof(colors[0]));

    int p = 0;
    int last_color = -1;

    for (int y = 0; y < height && p < out_size - 64; y++) {
        for (int x = 0; x < width && p < out_size - 32; x++) {
            /* Generate deterministic but varied density using position and seed */
            unsigned int n = xrand(&s1);

            /* Base density varies across the image to create patterns */
            int base = (int)((n >> 8) % 100);

            /* Add variation based on position */
            int x_var = (int)((x * 73856093u ^ s2) % 60);
            int y_var = (int)((y * 19349663u ^ (s2 >> 8)) % 60);

            /* Create some structure - vertical and horizontal bands */
            int band_x = (x * 3) % width;
            int band_y = (y * 2) % height;
            int band_effect = ((band_x + band_y) * 50 / (width + height)) % 40;

            int density = (base + x_var / 2 + y_var / 2 + band_effect) % 100;
            if (density < 0) density = 0;
            if (density > 99) density = 99;

            /* Map density to character - high density = early in ramp (dark chars like $@B) */
            int char_idx = (density * (ramp_len - 1)) / 99;
            if (char_idx < 0) char_idx = 0;
            if (char_idx >= ramp_len) char_idx = ramp_len - 1;

            /* Pick color based on density and position */
            int color_idx = (density + (int)(n & 0x1F)) % num_colors;
            int color = colors[color_idx];

            if (color != last_color) {
                int n2 = snprintf(out + p, out_size - p, "\x1b[38;5;%dm", color);
                if (n2 < 0 || n2 >= out_size - p) goto done;
                p += n2;
                last_color = color;
            }

            out[p++] = ramp[char_idx];
        }
        if (p < out_size - 8) out[p++] = '\n';
    }
done:
    if (p < out_size - 8) {
        memcpy(out + p, "\x1b[0m", 4);
        p += 4;
    }
    out[p < out_size ? p : out_size - 1] = '\0';
}

/* ============== Cache ============== */

album_art_cache_t *album_art_cache_create(void) {
    return calloc(1, sizeof(album_art_cache_t));
}

void album_art_cache_free(album_art_cache_t *c) {
    if (!c) return;
    free(c->ascii_output);
    free(c->cover_url);
    free(c->image_data);
    free(c);
}

void album_art_cache_set(album_art_cache_t *c, const char *url, const char *art, int w, int h) {
    if (!c) return;
    free(c->ascii_output);
    free(c->cover_url);
    c->cover_url = url ? strdup(url) : NULL;
    c->ascii_output = art ? strdup(art) : NULL;
    c->width = w;
    c->height = h;
}

const char *album_art_cache_get(album_art_cache_t *c, const char *url, int *w, int *h) {
    if (!c || !url || !c->cover_url || strcmp(c->cover_url, url) != 0) return NULL;
    if (w) *w = c->width;
    if (h) *h = c->height;
    return c->ascii_output;
}