/* SPDX-License-Identifier: MIT */
#ifndef HAX_SYSTEM_WIN_PROCESS_H
#define HAX_SYSTEM_WIN_PROCESS_H

#include <windows.h>

struct win_process {
    HANDLE handle;
    HANDLE job; /* NULL for detached children */
    DWORD id;
};

/* Encode a complete UTF-8 environment vector as an owned, sorted UTF-16 block for CreateProcess.
 * Reject malformed assignments, invalid UTF-8, and duplicate names (case-insensitive). An empty
 * vector produces the required double NUL. Return NULL with errno on failure. */
wchar_t *win_process_environment(const char *const *entries);

/* Launch UTF-8 argv directly, duplicating only the supplied standard handles into the child.
 * NULL standard handles select NUL. An owned child is assigned to a kill-on-close job before
 * execution begins; detached children may outlive the caller. Returns -1 with errno on failure
 * and leaves result zeroed. The caller closes result with win_process_close(). */
int win_process_start(struct win_process *result, const char *const *argv, HANDLE input,
                      HANDLE output, HANDLE error, int detached);

/* Same launch/ownership contract, with a caller-owned UTF-16 environment block. It must be
 * case-insensitively sorted and end with two NULs; NULL inherits the native parent environment.
 * The block is borrowed only during this call and may be released when it returns. */
int win_process_start_env(struct win_process *result, const char *const *argv, HANDLE input,
                          HANDLE output, HANDLE error, int detached, const wchar_t *environment);

/* Wait up to timeout_ms (INFINITE is allowed). Returns 1 and fills exit_code when exited, 0 on
 * timeout, or -1 with errno on failure. This does not release the process or kill it on timeout. */
int win_process_wait(const struct win_process *process, DWORD timeout_ms, DWORD *exit_code);

/* Stop the owned process tree. Detached processes are not terminated by this helper. */
void win_process_terminate(const struct win_process *process);

/* Release all owned handles and terminate any remaining owned descendants. Zeroes process. */
void win_process_close(struct win_process *process);

#endif /* HAX_SYSTEM_WIN_PROCESS_H */
