#ifndef SPOTICLI_INPUT_H
#define SPOTICLI_INPUT_H

#include <stdbool.h>

typedef enum {
    INPUT_CHAR,
    INPUT_KEY,
    INPUT_QUIT
} input_type_e;

typedef enum {
    INPUT_KEY_UP,
    INPUT_KEY_DOWN,
    INPUT_KEY_LEFT,
    INPUT_KEY_RIGHT,
    INPUT_KEY_ESCAPE,
    INPUT_KEY_ENTER,
    INPUT_KEY_UNKNOWN
} input_special_key_e;

typedef struct {
    input_type_e type;
    char ch;
    input_special_key_e special_key;
} input_event_t;

/* Initialize input system */
void input_init(void);

/* Cleanup input system */
void input_cleanup(void);

/* Poll for input (non-blocking) - returns NULL if no input */
input_event_t *input_poll(void);

/* Check if key was pressed (blocking with timeout) */
bool input_has_key_available(int timeout_ms);

#endif
