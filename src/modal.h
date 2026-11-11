#ifndef SPOTICLI_MODAL_H
#define SPOTICLI_MODAL_H

#include "input.h"
#include <stdbool.h>

/* Modal text-input prompt: a centered popup with a title and an editable
   single-line buffer. The owner sets active=true to open it, feeds events
   via modal_input_handle_event, and reads modal->buf when it confirms. */
typedef struct {
    char title[64];
    char buf[256];
    int  len;
    bool active;
    bool confirmed;     /* true if last interaction was Enter (success), false on Esc/cancel */
} modal_input_t;

void modal_input_open(modal_input_t *m, const char *title, const char *initial);

/* Returns true when the modal closed (confirmed or canceled) on this event;
   the caller should then check modal->confirmed and read modal->buf. */
bool modal_input_handle_event(modal_input_t *m, const input_event_t *evt);

void modal_input_render(const modal_input_t *m);

#endif
