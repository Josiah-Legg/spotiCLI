#ifndef SPOTICLI_HTTP_H
#define SPOTICLI_HTTP_H

#include <stdbool.h>
#include <stddef.h>

/* HTTP response structure */
typedef struct {
    char *body;
    int status_code;
    size_t size;
} http_response_t;

/* Binary response for images/files */
typedef struct {
    char *data;
    size_t size;
} http_binary_response_t;

/* HTTP methods */
typedef enum {
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE
} http_method_e;

/* Initialize HTTP client */
void http_init(void);

/* Clean up HTTP client */
void http_cleanup(void);

/* Make HTTP request */
http_response_t *http_request(
    http_method_e method,
    const char *url,
    const char *access_token,
    const char *body,
    const char *content_type
);

/* Free HTTP response */
void http_response_free(http_response_t *resp);

/* Fetch binary data from a URL */
http_binary_response_t *http_download_binary(const char *url, const char *token);

/* Free a binary response */
void http_binary_response_free(http_binary_response_t *resp);

/* URL encoding helper */
char *http_urlencode(const char *str);
void http_free_encoded(char *encoded);

/* Demo mode controls */
void http_set_demo_mode(bool enabled);
bool http_is_demo_mode(void);

#endif