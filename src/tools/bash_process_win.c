/* SPDX-License-Identifier: MIT */
#include <windows.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "xalloc.h"
#include "system/win_error.h"
#include "system/win_process.h"
#include "tools/bash_env.h"
#include "tools/bash_process.h"
#include "tools/bash_shell.h"

#define SHELL_TABLE_SIZE 128

/* Control handlers run on a separate thread. Retraction holds the same lock so a handler
 * cannot use a job handle after its owner closes it. */
static SRWLOCK shells_lock = SRWLOCK_INIT;
struct shell_entry {
    intptr_t token;
    struct win_process *process;
};

static struct shell_entry shells[SHELL_TABLE_SIZE];
static intptr_t next_token = 1;

static intptr_t publish_process(struct win_process *process)
{
    intptr_t token = 0;
    AcquireSRWLockExclusive(&shells_lock);
    if (next_token < INTPTR_MAX) {
        for (size_t i = 0; i < SHELL_TABLE_SIZE; i++) {
            if (!shells[i].process) {
                token = next_token++;
                shells[i] = (struct shell_entry){.token = token, .process = process};
                break;
            }
        }
    }
    ReleaseSRWLockExclusive(&shells_lock);
    if (!token)
        errno = ENOSPC;
    return token;
}

/* Borrowed only while holding shells_lock, or until the foreground owner waits. */
static struct win_process *find_process(intptr_t token)
{
    for (size_t i = 0; i < SHELL_TABLE_SIZE; i++)
        if (shells[i].token == token && shells[i].process)
            return shells[i].process;
    errno = ECHILD;
    return NULL;
}

static void retract_process(intptr_t token)
{
    AcquireSRWLockExclusive(&shells_lock);
    for (size_t i = 0; i < SHELL_TABLE_SIZE; i++) {
        if (shells[i].token == token) {
            shells[i] = (struct shell_entry){0};
            break;
        }
    }
    ReleaseSRWLockExclusive(&shells_lock);
}

void bash_shell_pgids_kill(void)
{
    AcquireSRWLockShared(&shells_lock);
    for (size_t i = 0; i < SHELL_TABLE_SIZE; i++)
        if (shells[i].process)
            win_process_terminate(shells[i].process);
    ReleaseSRWLockShared(&shells_lock);
}

void bash_signal_process_tree(intptr_t pid, int signal_number)
{
    (void)signal_number;
    /* Git Bash has no native console signal channel; stop the whole owned job immediately. */
    AcquireSRWLockShared(&shells_lock);
    struct win_process *process = find_process(pid);
    if (process)
        win_process_terminate(process);
    ReleaseSRWLockShared(&shells_lock);
}

int bash_process_exit_seen(intptr_t pid, int *exit_seen)
{
    DWORD code;
    AcquireSRWLockShared(&shells_lock);
    struct win_process *process = find_process(pid);
    int result = process ? win_process_wait(process, 0, &code) : -1;
    ReleaseSRWLockShared(&shells_lock);
    if (result == 1)
        *exit_seen = 1;
    return result < 0 ? -1 : 0;
}

int bash_process_wait(intptr_t pid, int *status)
{
    AcquireSRWLockShared(&shells_lock);
    struct win_process *process = find_process(pid);
    ReleaseSRWLockShared(&shells_lock);
    if (!process)
        return -1;
    DWORD code = 0;
    int result = win_process_wait(process, INFINITE, &code);
    int saved_errno = errno;
    retract_process(pid);
    win_process_close(process);
    free(process);
    *status = (int)code;
    errno = saved_errno;
    return result == 1 ? 0 : -1;
}

char *bash_start_shell(const char *command, struct shell_process *process)
{
    char **envp = bash_build_child_env();
    if (!envp)
        return xasprintf("environment: %s", strerror(errno));
    wchar_t *environment = win_process_environment((const char *const *)envp);
    free(envp);
    if (!environment)
        return xasprintf("environment: %s", strerror(errno));
    char *shell = bash_resolve_shell();
    if (!shell) {
        free(environment);
        return xasprintf("shell: %s", strerror(errno));
    }

    HANDLE reader, writer;
    char *error = NULL;
    if (!CreatePipe(&reader, &writer, NULL, 0)) {
        win_error_set_errno(GetLastError());
        error = xasprintf("pipe: %s", strerror(errno));
        goto free_shell;
    }
    int output_fd = _open_osfhandle((intptr_t)reader, _O_RDONLY | _O_BINARY | _O_NOINHERIT);
    if (output_fd < 0) {
        error = xasprintf("pipe: %s", strerror(errno));
        CloseHandle(reader);
        CloseHandle(writer);
        goto free_shell;
    }
    struct win_process *child = xcalloc(1, sizeof(*child));
    char *script = xasprintf("export PATH=\"/mingw64/bin:/usr/bin:$PATH\"; %s", command);
    const char *argv[] = {shell, "--noprofile", "--norc", "-c", script, NULL};
    int result = win_process_start_env(child, argv, NULL, writer, writer, 0, environment);
    free(script);
    CloseHandle(writer);
    if (result < 0) {
        error = xasprintf("spawn: %s", strerror(errno));
        _close(output_fd);
        free(child);
        goto free_shell;
    }
    intptr_t token = publish_process(child);
    if (!token) {
        error = xasprintf("process registry: %s", strerror(errno));
        win_process_close(child);
        free(child);
        _close(output_fd);
        goto free_shell;
    }
    process->pid = token;
    process->output_fd = output_fd;
free_shell:
    free(shell);
    free(environment);
    return error;
}
