#ifndef SPOTICLI_TUI_H
#define SPOTICLI_TUI_H

#include "state.h"
#include <stdbool.h>

/* TUI event types */
typedef enum {
    TUI_EVENT_NONE,
    TUI_EVENT_KEY,
    TUI_EVENT_MOUSE,
    TUI_EVENT_RESIZE,
    TUI_EVENT_QUIT
} tui_event_type_e;

/* Key codes */
typedef enum {
    KEY_NONE = 0,
    KEY_CHAR = 1,       /* Regular character */
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_ENTER,
    KEY_TAB,
    KEY_ESC,
    KEY_BACKSPACE,
    KEY_SPACE,
    KEY_HOME,
    KEY_END,
    KEY_PAGEUP,
    KEY_PAGEDOWN,
    KEY_DELETE,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10
} key_code_e;

/* TUI event */
typedef struct {
    tui_event_type_e type;
    union {
        struct {
            key_code_e code;
            char ch;
        } key;
        struct {
            int x, y;
            int button; /* 0=left, 1=middle, 2=right, 3=scroll up, 4=scroll down */
            bool down;
        } mouse;
    } data;
} tui_event_t;

/* Terminal colors */
typedef enum {
    COLOR_BLACK = 0,
    COLOR_RED,
    COLOR_GREEN,
    COLOR_YELLOW,
    COLOR_BLUE,
    COLOR_MAGENTA,
    COLOR_CYAN,
    COLOR_WHITE
} tui_color_e;

/* Attributes */
typedef enum {
    ATTR_NORMAL = 0,
    ATTR_BOLD = 1,
    ATTR_DIM = 2,
    ATTR_ITALIC = 4,
    ATTR_UNDERLINE = 8,
    ATTR_BLINK = 16,
    ATTR_REVERSE = 32
} tui_attr_e;

/* Initialize TUI */
bool tui_init(void);

/* Cleanup TUI */
void tui_cleanup(void);

/* Check if TUI is initialized */
bool tui_is_initialized(void);

/* Get terminal dimensions */
void tui_get_size(int *width, int *height);

/* Clear screen */
void tui_clear(void);

/* Move cursor */
void tui_cursor_move(int x, int y);

/* Show/hide cursor */
void tui_cursor_show(bool show);

/* Set text color and attributes */
void tui_set_color(tui_color_e fg, tui_color_e bg);
void tui_set_color_idx(int fg, int bg); /* 256-color palette; -1 = default */
void tui_set_attr(tui_attr_e attr);

/* Reset to default style */
void tui_reset_style(void);

/* Write text at current cursor position */
void tui_write(const char *text);

/* Write formatted text */
void tui_printf(const char *fmt, ...);

/* Refresh/flush output */
void tui_refresh(void);

/* Flush output (synonym for tui_refresh) */
void tui_flush(void);

/* Move cursor to home (0,0) */
void tui_cursor_home(void);

/* Get next event (non-blocking, returns true if event is available) */
bool tui_get_event(tui_event_t *event);

/* Wait for event (blocking, with optional timeout in ms) */
bool tui_wait_event(tui_event_t *event, int timeout_ms);

/* Draw a box */
void tui_draw_box(int x, int y, int width, int height, bool double_line);

/* Draw a horizontal line */
void tui_draw_hline(int x, int y, int width);

/* Draw a vertical line */
void tui_draw_vline(int x, int y, int height);

#endif
