/* SPDX-License-Identifier: MIT */
#ifndef HAX_TESTS_TERMINAL_WIN_CONSOLE_H
#define HAX_TESTS_TERMINAL_WIN_CONSOLE_H

#include <stddef.h>

struct t_win_console;

/* Launch an absolute UTF-8 executable path in an owned, hidden 110x32 Windows pseudoconsole.
 * Output is drained concurrently so shutdown cannot block behind unread console bytes. */
struct t_win_console *t_win_console_start(const char *const *argv);
int t_win_console_send(struct t_win_console *console, const char *bytes, unsigned length);
/* Encode one key-down event using the Win32 input mode negotiated by the pseudoconsole. */
int t_win_console_key(struct t_win_console *console, unsigned virtual_key, unsigned utf16,
                      unsigned controls);
int t_win_console_expect(struct t_win_console *console, const char *text, int timeout_ms);
/* Capture the current byte position, then wait for text emitted after that position. */
size_t t_win_console_mark(struct t_win_console *console);
int t_win_console_expect_since(struct t_win_console *console, const char *text, int timeout_ms,
                               size_t mark);
/* Return owned ANSI-stripped emitted output, not a reconstructed terminal screen. */
char *t_win_console_output(struct t_win_console *console, size_t mark);
int t_win_console_wait(struct t_win_console *console, int timeout_ms, unsigned long *exit_code);
void t_win_console_close(struct t_win_console *console);

#endif /* HAX_TESTS_TERMINAL_WIN_CONSOLE_H */
