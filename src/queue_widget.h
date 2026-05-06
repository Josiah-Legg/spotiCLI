#ifndef SPOTICLI_QUEUE_WIDGET_H
#define SPOTICLI_QUEUE_WIDGET_H

#include "state.h"
#include <stdbool.h>

/* Queue widget config */
typedef struct {
    int x, y;              /* Position on screen */
    int width, height;     /* Widget dimensions */
    int visible_items;     /* How many items to show */
    int selected_index;    /* Currently highlighted item */
    bool visible;          /* Is sidebar shown */
} queue_widget_t;

/* Create queue widget */
queue_widget_t *queue_widget_create(int x, int y, int width, int height);

/* Free queue widget */
void queue_widget_free(queue_widget_t *widget);

/* Render queue widget */
void queue_widget_render(queue_widget_t *widget, player_state_t *state);

/* Handle selection navigation */
void queue_widget_select_next(queue_widget_t *widget, player_state_t *state);
void queue_widget_select_prev(queue_widget_t *widget, player_state_t *state);

/* Toggle widget visibility */
void queue_widget_toggle(queue_widget_t *widget);

/* Remove selected item from queue */
bool queue_widget_remove_selected(queue_widget_t *widget, player_state_t *state);

#endif
