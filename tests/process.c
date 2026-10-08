/* SPDX-License-Identifier: MIT */
#include "process.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>

#include "system/win_process.h"
#include "system/win_utf8.h"
#else
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#include "system/clock.h"
#include "system/spawn.h"
#endif

#include "xalloc.h"

struct t_process {
#ifdef _WIN32
    struct win_process native;
#else
    pid_t pid;
#endif
    int exited;
    int exit_code;
};

char *t_program_path(const char *argv0)
{
#ifdef _WIN32
    (void)argv0;
    wchar_t module[32768];
    DWORD length = GetModuleFileNameW(NULL, module, sizeof(module) / sizeof(*module));
    if (!length || length >= sizeof(module) / sizeof(*module)) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    return win_utf8_from_wide(module);
#else
    return realpath(argv0, NULL);
#endif
}

struct t_process *t_process_start(const char *const *argv)
{
    struct t_process *process = xcalloc(1, sizeof(*process));
    fflush(NULL);
#ifdef _WIN32
    if (win_process_start(&process->native, argv, NULL, (HANDLE)_get_osfhandle(_fileno(stdout)),
                          (HANDLE)_get_osfhandle(_fileno(stderr)), 0) < 0)
        goto error;
#else
    process->pid = spawn_fork();
    if (process->pid < 0)
        goto error;
    if (process->pid == 0) {
        int input = open("/dev/null", O_RDONLY);
        if (input < 0 || dup2(input, STDIN_FILENO) < 0)
            _exit(127);
        if (input != STDIN_FILENO)
            close(input);
        execv(argv[0], (char *const *)argv);
        _exit(127);
    }
#endif
    return process;
error:
    free(process);
    return NULL;
}

int t_process_wait(struct t_process *process, int timeout_ms)
{
    if (process->exited)
        return process->exit_code;
#ifdef _WIN32
    DWORD code;
    int result = win_process_wait(&process->native, (DWORD)timeout_ms, &code);
    if (result <= 0) {
        if (result == 0) {
            win_process_terminate(&process->native);
            win_process_wait(&process->native, INFINITE, &code);
            process->exited = 1;
            process->exit_code = -1;
            errno = ETIMEDOUT;
        }
        return -1;
    }
    process->exit_code = (int)code;
#else
    long deadline = monotonic_ms() + timeout_ms;
    int status;
    for (;;) {
        pid_t result = waitpid(process->pid, &status, WNOHANG);
        if (result == process->pid)
            break;
        if (result < 0 && errno != EINTR)
            return -1;
        if (monotonic_ms() >= deadline) {
            kill(process->pid, SIGKILL);
            spawn_wait_child(process->pid);
            process->exited = 1;
            process->exit_code = -1;
            errno = ETIMEDOUT;
            return -1;
        }
        clock_sleep_ms(5);
    }
    process->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
#endif
    process->exited = 1;
    return process->exit_code;
}

void t_process_close(struct t_process *process)
{
    if (!process)
        return;
#ifdef _WIN32
    win_process_close(&process->native);
#else
    if (!process->exited) {
        kill(process->pid, SIGKILL);
        spawn_wait_child(process->pid);
    }
#endif
    free(process);
}
