#include "queue_widget.h"
#include "tui.h"
#include <stdlib.h>
#include <stdio.h>

queue_widget_t *queue_widget_create(int x, int y, int width, int height)
{
    queue_widget_t *widget = (queue_widget_t *)malloc(sizeof(queue_widget_t));
    if (!widget) return NULL;

    widget->x = x;
    widget->y = y;
    widget->width = width;
    widget->height = height;
    widget->visible_items = height - 3;
    widget->selected_index = 0;
    widget->visible = true;

    return widget;
}

void queue_widget_free(queue_widget_t *widget)
{
    if (widget) free(widget);
}

void queue_widget_render(queue_widget_t *widget, player_state_t *state)
{
    if (!widget || !state || !widget->visible) return;

    /* Draw box */
    tui_draw_box(widget->x, widget->y, widget->width, widget->height, true);

    /* Title */
    tui_cursor_move(widget->x + 2, widget->y);
    tui_write(" QUEUE ");

    /* Queue items */
    int start_idx = widget->selected_index > 0 ? widget->selected_index - 1 : 0;
    int items_to_show = widget->height - 3;

    for (int i = 0; i < items_to_show && (start_idx + i) < state->queue_size; i++) {
        int queue_idx = start_idx + i;
        int render_y = widget->y + 1 + i;

        tui_cursor_move(widget->x + 1, render_y);

        if (queue_idx == widget->selected_index) {
            tui_set_color(COLOR_BLACK, COLOR_CYAN);
            tui_write("► ");
            tui_printf("%-32s", state->queue[queue_idx].name);
            tui_reset_style();
        } else {
            tui_write("  ");
            tui_printf("%-32s", state->queue[queue_idx].name);
        }
    }

    /* Footer: show count */
    tui_cursor_move(widget->x + 1, widget->y + widget->height - 1);
    tui_printf("Queue: %d songs", state->queue_size);
}

void queue_widget_select_next(queue_widget_t *widget, player_state_t *state)
{
    if (!widget || !state) return;
    if (widget->selected_index < state->queue_size - 1) {
        widget->selected_index++;
    }
}

void queue_widget_select_prev(queue_widget_t *widget, player_state_t *state)
{
    if (!widget || !state) return;
    if (widget->selected_index > 0) {
        widget->selected_index--;
    }
}

void queue_widget_toggle(queue_widget_t *widget)
{
    if (!widget) return;
    widget->visible = !widget->visible;
}

bool queue_widget_remove_selected(queue_widget_t *widget, player_state_t *state)
{
    if (!widget || !state) return false;

    if (widget->selected_index < 0 || widget->selected_index >= state->queue_size) {
        return false;
    }

    queue_remove(state, widget->selected_index);

    if (widget->selected_index >= state->queue_size && widget->selected_index > 0) {
        widget->selected_index--;
    }

    return true;
}
