/* SPDX-License-Identifier: MIT */
#ifndef HAX_TESTS_PROCESS_H
#define HAX_TESTS_PROCESS_H

struct t_process;

/* Resolve this test executable to an owned absolute UTF-8 path before changing directories. */
char *t_program_path(const char *argv0);
/* Start argv directly with inherited stdout/stderr and no interactive stdin. Return NULL on
 * failure. The caller must close every successfully started process, including after waiting. */
struct t_process *t_process_start(const char *const *argv);
/* Return the exit code (128 + signal on POSIX), or -1 with errno on error. On timeout, terminate
 * and reap the child and return -1 with ETIMEDOUT. */
int t_process_wait(struct t_process *process, int timeout_ms);
/* Stop and reap a running child, then release its ownership. NULL is a no-op. */
void t_process_close(struct t_process *process);

#endif /* HAX_TESTS_PROCESS_H */
