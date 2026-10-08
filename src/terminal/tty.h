/* SPDX-License-Identifier: MIT */
#ifndef HAX_TERMINAL_TTY_H
#define HAX_TERMINAL_TTY_H

struct tty_mode;

/* Enable native UTF-8 and ANSI console output where needed. Idempotent; restores console settings
 * at process exit. Does not change redirected byte streams. */
void tty_init(void);

/* Restore initialized console output settings without allocation or stdio; safe for native
 * console-control cleanup. tty_init also registers this at process exit. */
void tty_restore_output(void);

/* Enter raw stdin mode, retaining signal-driven Ctrl-C when signals is true. Returns an owned
 * saved state, or NULL with errno on failure. tty_raw_leave restores and frees it; NULL is safe.
 * Owners must not overlap and must leave raw mode before handing the terminal to another reader. */
struct tty_mode *tty_raw_enter(int signals);
void tty_raw_leave(struct tty_mode *mode);

/* Query stdout's visible terminal size. Return 0 on success, or -1 with errno on failure. */
int tty_size(int *columns, int *rows);

/* Consume one input byte: 1 for a byte, 0 for EOF/timeout, or -1 with errno for failure. Negative
 * timeout_ms blocks indefinitely. Windows Unicode console events are encoded as UTF-8/xterm keys.
 * One foreground reader owns stdin; interrupt workers must relinquish it before prompt reads. */
int tty_read_byte(unsigned char *byte, int timeout_ms);

/* Discard queued terminal input, including native key bytes already assembled by this module. */
void tty_flush_input(void);

#endif /* HAX_TERMINAL_TTY_H */
