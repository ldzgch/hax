/* SPDX-License-Identifier: MIT */
#include "terminal/tty.h"

#include <errno.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <string.h>

#include "system/win_error.h"
#include "terminal/win_keys.h"
#else
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#endif

#include "xalloc.h"

struct tty_mode {
#ifdef _WIN32
    HANDLE input;
    DWORD saved;
    int output_mode;
#else
    struct termios saved;
#endif
};

#ifdef _WIN32
static struct win_keys pending_keys;
static HANDLE initialized_handles[2];
static DWORD output_modes[2];
static UINT output_codepage;
static int initialized;

static void bind_missing_standard(FILE *stream, DWORD descriptor, const char *device,
                                  const char *mode)
{
    HANDLE handle = GetStdHandle(descriptor);
    if (handle && handle != INVALID_HANDLE_VALUE)
        return;
    if (freopen(device, mode, stream))
        SetStdHandle(descriptor, (HANDLE)_get_osfhandle(_fileno(stream)));
}

#endif

void tty_restore_output(void)
{
#ifdef _WIN32
    for (size_t i = 0; i < 2; i++)
        if (initialized_handles[i])
            SetConsoleMode(initialized_handles[i], output_modes[i]);
    if (output_codepage)
        SetConsoleOutputCP(output_codepage);
#endif
}

void tty_init(void)
{
#ifdef _WIN32
    if (initialized)
        return;
    initialized = 1;
    /* Pseudoconsole launches can attach a console without populating the standard handles.
     * Rebind only missing handles, preserving every explicitly redirected stream. */
    bind_missing_standard(stdin, STD_INPUT_HANDLE, "CONIN$", "rb");
    bind_missing_standard(stdout, STD_OUTPUT_HANDLE, "CONOUT$", "wb");
    bind_missing_standard(stderr, STD_ERROR_HANDLE, "CONOUT$", "wb");
    const DWORD descriptors[] = {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    for (size_t i = 0; i < 2; i++) {
        HANDLE handle = GetStdHandle(descriptors[i]);
        DWORD mode;
        if (GetConsoleMode(handle, &mode)) {
            initialized_handles[i] = handle;
            output_modes[i] = mode;
        }
    }
    for (size_t i = 0; i < 2; i++) {
        if (!initialized_handles[i])
            continue;
        if (!SetConsoleMode(initialized_handles[i], output_modes[i] | ENABLE_PROCESSED_OUTPUT |
                                                        ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
            initialized_handles[i] = NULL;
            continue;
        }
        if (!output_codepage)
            output_codepage = GetConsoleOutputCP();
    }
    if (output_codepage)
        SetConsoleOutputCP(CP_UTF8);
    atexit(tty_restore_output);
#endif
}

struct tty_mode *tty_raw_enter(int signals)
{
    struct tty_mode *mode = xcalloc(1, sizeof(*mode));
#ifdef _WIN32
    tty_init();
    mode->input = GetStdHandle(STD_INPUT_HANDLE);
    if (!GetConsoleMode(mode->input, &mode->saved))
        goto error;
    /* Preserve terminal-injected VT sequences, including bracketed paste delimiters. */
    DWORD raw = (mode->saved | ENABLE_EXTENDED_FLAGS | ENABLE_VIRTUAL_TERMINAL_INPUT) &
                ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_QUICK_EDIT_MODE |
                  ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT);
    if (!signals)
        raw &= ~ENABLE_PROCESSED_INPUT;
    if (!SetConsoleMode(mode->input, raw))
        goto error;
    fflush(stdout);
    mode->output_mode = _setmode(_fileno(stdout), _O_BINARY);
    if (mode->output_mode < 0) {
        int saved_errno = errno;
        SetConsoleMode(mode->input, mode->saved);
        free(mode);
        errno = saved_errno;
        return NULL;
    }
    return mode;
error:
    win_error_set_errno(GetLastError());
    free(mode);
    return NULL;
#else
    if (tcgetattr(STDIN_FILENO, &mode->saved) < 0)
        goto error;
    struct termios raw = mode->saved;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN);
    if (!signals)
        raw.c_lflag &= ~ISIG;
    raw.c_iflag &= ~(IXON | ICRNL | INPCK | ISTRIP | BRKINT);
    raw.c_oflag &= ~OPOST;
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSADRAIN, &raw) < 0)
        goto error;
    return mode;
error:
    free(mode);
    return NULL;
#endif
}

void tty_raw_leave(struct tty_mode *mode)
{
    if (!mode)
        return;
#ifdef _WIN32
    SetConsoleMode(mode->input, mode->saved);
    fflush(stdout);
    _setmode(_fileno(stdout), mode->output_mode);
#else
    tcsetattr(STDIN_FILENO, TCSADRAIN, &mode->saved);
#endif
    free(mode);
}

int tty_size(int *columns, int *rows)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        win_error_set_errno(GetLastError());
        return -1;
    }
    *columns = info.srWindow.Right - info.srWindow.Left + 1;
    *rows = info.srWindow.Bottom - info.srWindow.Top + 1;
#else
    struct winsize size;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) < 0)
        return -1;
    *columns = size.ws_col;
    *rows = size.ws_row;
#endif
    return 0;
}

int tty_read_byte(unsigned char *byte, int timeout_ms)
{
#ifdef _WIN32
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD console_mode;
    if (!GetConsoleMode(input, &console_mode)) {
        if (timeout_ms >= 0 && GetFileType(input) == FILE_TYPE_PIPE) {
            ULONGLONG deadline = GetTickCount64() + (DWORD)timeout_ms;
            for (;;) {
                DWORD available;
                if (!PeekNamedPipe(input, NULL, 0, NULL, &available, NULL)) {
                    DWORD error = GetLastError();
                    if (error == ERROR_BROKEN_PIPE)
                        return 0;
                    win_error_set_errno(error);
                    return -1;
                }
                if (available)
                    break;
                if (GetTickCount64() >= deadline)
                    return 0;
                Sleep(1);
            }
        }
        DWORD received;
        if (!ReadFile(input, byte, 1, &received, NULL)) {
            DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE)
                return 0;
            win_error_set_errno(error);
            return -1;
        }
        return received ? 1 : 0;
    }
    ULONGLONG deadline = GetTickCount64() + (timeout_ms < 0 ? 0 : (DWORD)timeout_ms);
    for (;;) {
        if (win_keys_pop(&pending_keys, byte))
            return 1;
        ULONGLONG now = GetTickCount64();
        DWORD remaining = timeout_ms < 0 ? INFINITE : now >= deadline ? 0 : (DWORD)(deadline - now);
        DWORD wait = WaitForSingleObject(input, remaining);
        if (wait == WAIT_TIMEOUT)
            return 0;
        if (wait != WAIT_OBJECT_0) {
            win_error_set_errno(GetLastError());
            return -1;
        }
        INPUT_RECORD event;
        DWORD received;
        if (!ReadConsoleInputW(input, &event, 1, &received)) {
            win_error_set_errno(GetLastError());
            return -1;
        }
        if (received && event.EventType == KEY_EVENT)
            win_keys_feed(&pending_keys, &event.Event.KeyEvent);
        if (timeout_ms >= 0 && GetTickCount64() >= deadline && !pending_keys.repeats)
            return 0;
    }
#else
    struct pollfd input = {.fd = STDIN_FILENO, .events = POLLIN};
    int ready;
    do {
        ready = poll(&input, 1, timeout_ms);
    } while (ready < 0 && errno == EINTR);
    if (ready <= 0)
        return ready;
    ssize_t received;
    do {
        received = read(STDIN_FILENO, byte, 1);
    } while (received < 0 && errno == EINTR);
    return (int)received;
#endif
}

void tty_flush_input(void)
{
#ifdef _WIN32
    memset(&pending_keys, 0, sizeof(pending_keys));
    FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
#else
    tcflush(STDIN_FILENO, TCIFLUSH);
#endif
}
