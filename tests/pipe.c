/* SPDX-License-Identifier: MIT */
#include "pipe.h"

#include <stddef.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>

#include "system/rand.h"
#include "system/win_error.h"
#include "system/win_utf8.h"
#else
#include <unistd.h>
#include <sys/stat.h>

#include "harness.h"
#endif

#include "xalloc.h"

struct t_pipe {
    char *path;
#ifdef _WIN32
    HANDLE handle;
#endif
};

struct t_pipe *t_pipe_create(void)
{
    struct t_pipe *pipe = xcalloc(1, sizeof(*pipe));
#ifdef _WIN32
    char id[37];
    gen_uuid_v4(id);
    pipe->path = xasprintf("\\\\.\\pipe\\hax-test-%s", id);
    wchar_t *wide = win_utf8_to_wide(pipe->path);
    if (!wide)
        goto error;
    pipe->handle = CreateNamedPipeW(wide, PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                    PIPE_TYPE_BYTE | PIPE_NOWAIT, 1, 4096, 4096, 0, NULL);
    DWORD error = GetLastError();
    free(wide);
    if (pipe->handle == INVALID_HANDLE_VALUE) {
        win_error_set_errno(error);
        goto error;
    }
#else
    pipe->path = xasprintf("%s/pipe", t_tempdir());
    if (mkfifo(pipe->path, 0600) < 0)
        goto error;
#endif
    return pipe;
error:
    free(pipe->path);
    free(pipe);
    return NULL;
}

const char *t_pipe_path(const struct t_pipe *pipe)
{
    return pipe->path;
}

int t_pipe_exists(const struct t_pipe *pipe)
{
#ifdef _WIN32
    return GetFileType(pipe->handle) == FILE_TYPE_PIPE;
#else
    struct stat info;
    return lstat(pipe->path, &info) == 0 && S_ISFIFO(info.st_mode);
#endif
}

void t_pipe_close(struct t_pipe *pipe)
{
    if (!pipe)
        return;
#ifdef _WIN32
    CloseHandle(pipe->handle);
#else
    unlink(pipe->path);
#endif
    free(pipe->path);
    free(pipe);
}
