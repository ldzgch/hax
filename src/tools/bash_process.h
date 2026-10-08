/* SPDX-License-Identifier: MIT */
#ifndef HAX_TOOLS_BASH_PROCESS_H
#define HAX_TOOLS_BASH_PROCESS_H

#include <stdint.h>

#include "tool.h"

/* Run the command and return owned model-facing output, streaming display bytes through `ctx`
 * (NULL disables display). When background is set, the command detaches into a task after the
 * configured yield window instead of the timeout. `name` (pre-validated by task_name_error, or
 * NULL) becomes the task id if the command detaches. A model-only annotation ending the output
 * is reported via ctx->output_hidden_tail. */
char *bash_run_command(const char *command, long timeout_ms, int background, const char *name,
                       struct tool_run_ctx *ctx);

struct shell_process {
    intptr_t pid; /* POSIX pid or owned native process token */
    int output_fd;
};

/* Start a shell with merged binary stdout/stderr and closed stdin. Return an allocated error,
 * or NULL after publishing the owned process for fatal cleanup. */
char *bash_start_shell(const char *command, struct shell_process *process);

/* Wait for the leader and release its process resources after retracting fatal cleanup.
 * Returns 0 on success or -1 with errno; status uses native platform encoding. */
int bash_process_wait(intptr_t pid, int *status);

/* Stop an owned tree. POSIX sends signal_number to the group, falling back to its unreaped
 * leader. Windows terminates the owned job immediately regardless of signal_number. */
void bash_signal_process_tree(intptr_t pid, int signal_number);

/* Observe the leader without releasing its ownership. Set *exit_seen on exit; return 0 or -1
 * with errno. The process remains signalable until bash_process_wait releases it. */
int bash_process_exit_seen(intptr_t pid, int *exit_seen);

/* Terminate published owned shell trees during fatal cleanup. POSIX is async-signal-safe;
 * Windows serializes the console-control handler with native process release. */
void bash_shell_pgids_kill(void);

#ifndef _WIN32
/* Publish at spawn and retract before reaping, keeping the pid reserved during fatal cleanup.
 * These operations run on the tool-dispatch thread. */
void bash_shell_pgid_publish(intptr_t pid);
void bash_shell_pgid_retract(intptr_t pid);
#endif

#endif /* HAX_TOOLS_BASH_PROCESS_H */
