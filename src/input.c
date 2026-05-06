#include "input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>

static HANDLE hStdin;
static DWORD original_mode;

void input_init(void)
{
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hStdin, &original_mode);
    
    DWORD new_mode = original_mode;
    new_mode &= ~ENABLE_LINE_INPUT;
    new_mode &= ~ENABLE_ECHO_INPUT;
    SetConsoleMode(hStdin, new_mode);
}

void input_cleanup(void)
{
    SetConsoleMode(hStdin, original_mode);
}

input_event_t *input_poll(void)
{
    if (!_kbhit()) {
        return NULL;
    }

    static input_event_t evt;
    memset(&evt, 0, sizeof(input_event_t));

    int ch = _getch();

    if (ch == 0 || ch == 224) {
        /* Extended key - read next byte */
        int extended = _getch();
        evt.type = INPUT_KEY;
        switch (extended) {
        case 72:
            evt.special_key = INPUT_KEY_UP;
            break;
        case 80:
            evt.special_key = INPUT_KEY_DOWN;
            break;
        case 75:
            evt.special_key = INPUT_KEY_LEFT;
            break;
        case 77:
            evt.special_key = INPUT_KEY_RIGHT;
            break;
        default:
            evt.special_key = INPUT_KEY_UNKNOWN;
        }
    } else if (ch == 27) {
        evt.type = INPUT_KEY;
        evt.special_key = INPUT_KEY_ESCAPE;
    } else if (ch == 13) {
        evt.type = INPUT_KEY;
        evt.special_key = INPUT_KEY_ENTER;
    } else if (ch == 3) {
        evt.type = INPUT_QUIT;
    } else {
        evt.type = INPUT_CHAR;
        evt.ch = ch;
    }

    return &evt;
}

bool input_has_key_available(int timeout_ms)
{
    (void)timeout_ms;
    return _kbhit() != 0;
}

#else
/* Unix/Linux/Mac implementation */
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

static struct termios original_termios;
static int stdin_flags;

void input_init(void)
{
    tcgetattr(STDIN_FILENO, &original_termios);
    struct termios raw = original_termios;

    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    stdin_flags = fcntl(STDIN_FILENO, F_GETFL);
    fcntl(STDIN_FILENO, F_SETFL, stdin_flags | O_NONBLOCK);
}

void input_cleanup(void)
{
    fcntl(STDIN_FILENO, F_SETFL, stdin_flags);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
}

input_event_t *input_poll(void)
{
    static unsigned char buf[3];
    static int buf_idx = 0;
    static input_event_t evt;

    ssize_t n = read(STDIN_FILENO, &buf[buf_idx], 1);
    if (n <= 0) {
        return NULL;
    }

    buf_idx++;

    memset(&evt, 0, sizeof(input_event_t));

    if (buf[0] == 27 && buf_idx >= 2) {
        if (buf[1] == '[' && buf_idx >= 3) {
            evt.type = INPUT_KEY;
            switch (buf[2]) {
            case 'A':
                evt.special_key = INPUT_KEY_UP;
                buf_idx = 0;
                return &evt;
            case 'B':
                evt.special_key = INPUT_KEY_DOWN;
                buf_idx = 0;
                return &evt;
            case 'C':
                evt.special_key = INPUT_KEY_RIGHT;
                buf_idx = 0;
                return &evt;
            case 'D':
                evt.special_key = INPUT_KEY_LEFT;
                buf_idx = 0;
                return &evt;
            }
        } else if (buf[1] == 27) {
            evt.type = INPUT_KEY;
            evt.special_key = INPUT_KEY_ESCAPE;
            buf_idx = 0;
            return &evt;
        }
    } else if (buf[0] == 3) {
        evt.type = INPUT_QUIT;
        buf_idx = 0;
        return &evt;
    } else if (buf[0] == 10 || buf[0] == 13) {
        evt.type = INPUT_KEY;
        evt.special_key = INPUT_KEY_ENTER;
        buf_idx = 0;
        return &evt;
    } else if (buf_idx >= 1) {
        evt.type = INPUT_CHAR;
        evt.ch = buf[0];
        buf_idx = 0;
        return &evt;
    }

    return NULL;
}

bool input_has_key_available(int timeout_ms)
{
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    
    int ret = poll(&pfd, 1, timeout_ms);
    return ret > 0;
}

#endif
