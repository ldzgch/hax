/* SPDX-License-Identifier: MIT */
#include <windows.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "buf.h"
#include "xalloc.h"
#include "system/spawn.h"
#include "system/win_bash.h"
#include "system/win_error.h"
#include "system/win_process.h"

static int start_shell(struct win_process *process, const char *command, HANDLE input,
                       HANDLE output, HANDLE error)
{
    if (!command) {
        errno = EINVAL;
        return -1;
    }
    char *bash = win_bash_path();
    if (!bash)
        return -1;
    /* A non-login Bash retains the caller's working directory and settings. Its Unix helpers
     * still need Git's standard mount paths even when only Git's cmd directory is on PATH. */
    char *script = xasprintf("export PATH=\"/mingw64/bin:/usr/bin:$PATH\"; %s", command);
    const char *argv[] = {bash, "--noprofile", "--norc", "-c", script, NULL};
    int result = win_process_start(process, argv, input, output, error, 0);
    free(script);
    free(bash);
    return result;
}

int spawn_status_success(int status)
{
    return status == 0;
}

int spawn_shell_wait(const char *shell_cmd)
{
    struct win_process process;
    if (start_shell(&process, shell_cmd, GetStdHandle(STD_INPUT_HANDLE),
                    GetStdHandle(STD_OUTPUT_HANDLE), GetStdHandle(STD_ERROR_HANDLE)) < 0)
        return -1;
    DWORD exit_code = 0;
    int result = win_process_wait(&process, INFINITE, &exit_code);
    win_process_close(&process);
    return result == 1 ? (int)exit_code : -1;
}

int spawn_detached(const char *const *argv)
{
    struct win_process process;
    if (win_process_start(&process, argv, NULL, NULL, NULL, 1) < 0)
        return -1;
    win_process_close(&process);
    return 0;
}

static int spawn_pipe_open_mode(struct spawn_pipe *result, const char *command, int reading)
{
    if (!result || !command) {
        if (result)
            memset(result, 0, sizeof(*result));
        errno = EINVAL;
        return -1;
    }
    memset(result, 0, sizeof(*result));
    HANDLE reader, writer;
    if (!CreatePipe(&reader, &writer, NULL, 0)) {
        win_error_set_errno(GetLastError());
        return -1;
    }
    HANDLE parent = reading ? reader : writer;
    HANDLE child = reading ? writer : reader;
    struct win_process *process = xcalloc(1, sizeof(*process));
    int status = start_shell(process, command, reading ? GetStdHandle(STD_INPUT_HANDLE) : child,
                             reading ? child : GetStdHandle(STD_OUTPUT_HANDLE),
                             GetStdHandle(STD_ERROR_HANDLE));
    CloseHandle(child);
    if (status < 0)
        goto close_parent;
    int fd = _open_osfhandle((intptr_t)parent,
                             (reading ? _O_RDONLY : _O_WRONLY) | _O_BINARY | _O_NOINHERIT);
    if (fd < 0)
        goto close_process;
    FILE *stream = _fdopen(fd, reading ? "rb" : "wb");
    if (!stream) {
        int saved_errno = errno;
        _close(fd);
        parent = NULL;
        errno = saved_errno;
        goto close_process;
    }
    result->stream = stream;
    result->process = process;
    return 0;
close_process:
    win_process_terminate(process);
    win_process_close(process);
close_parent: {
    int saved_errno = errno;
    if (parent)
        CloseHandle(parent);
    free(process);
    errno = saved_errno;
}
    return -1;
}

int spawn_pipe_open_write(struct spawn_pipe *pipe, const char *shell_cmd)
{
    return spawn_pipe_open_mode(pipe, shell_cmd, 0);
}

int spawn_pipe_open_read(struct spawn_pipe *pipe, const char *shell_cmd)
{
    return spawn_pipe_open_mode(pipe, shell_cmd, 1);
}

int spawn_pipe_close(struct spawn_pipe *pipe)
{
    if (!pipe || !pipe->stream)
        return 0;
    fclose(pipe->stream);
    DWORD exit_code = 0;
    int status = win_process_wait(pipe->process, INFINITE, &exit_code);
    win_process_close(pipe->process);
    free(pipe->process);
    memset(pipe, 0, sizeof(*pipe));
    return status == 1 ? (int)exit_code : -1;
}

char *spawn_capture_stdout(const char *const *argv, size_t max_bytes, int timeout_ms,
                           size_t *out_len)
{
    if (!argv || !argv[0] || !out_len || timeout_ms <= 0) {
        errno = EINVAL;
        return NULL;
    }
    HANDLE reader, writer;
    if (!CreatePipe(&reader, &writer, NULL, 0)) {
        win_error_set_errno(GetLastError());
        return NULL;
    }
    struct win_process process;
    int launched = win_process_start(&process, argv, NULL, writer, NULL, 0);
    CloseHandle(writer);
    if (launched < 0) {
        int saved_errno = errno;
        CloseHandle(reader);
        errno = saved_errno;
        return NULL;
    }
    ULONGLONG deadline = GetTickCount64() + (DWORD)timeout_ms;
    struct buf output;
    buf_init(&output);
    DWORD exit_code = 1;
    int succeeded = 0;
    for (;;) {
        ULONGLONG now = GetTickCount64();
        if (now >= deadline) {
            errno = ETIMEDOUT;
            break;
        }
        DWORD available;
        if (!PeekNamedPipe(reader, NULL, 0, NULL, &available, NULL)) {
            DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE) {
                succeeded = win_process_wait(&process, (DWORD)(deadline - now), &exit_code) == 1;
                break;
            }
            win_error_set_errno(error);
            break;
        }
        if (!available) {
            Sleep(5);
            continue;
        }
        char chunk[65536];
        DWORD received;
        DWORD count = available < sizeof(chunk) ? available : sizeof(chunk);
        if (!ReadFile(reader, chunk, count, &received, NULL)) {
            win_error_set_errno(GetLastError());
            break;
        }
        if (received > max_bytes - output.len) {
            errno = EFBIG;
            break;
        }
        buf_append(&output, chunk, received);
    }
    int saved_errno = errno;
    CloseHandle(reader);
    if (!succeeded)
        win_process_terminate(&process);
    win_process_close(&process);
    errno = saved_errno;
    if (!succeeded || exit_code || !output.len) {
        buf_free(&output);
        return NULL;
    }
    *out_len = output.len;
    return buf_steal(&output);
}
