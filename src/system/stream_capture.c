/* SPDX-License-Identifier: MIT */
#include "system/stream_capture.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
/* The Windows API headers depend on this umbrella header's target declarations. */
#include <windows.h> // IWYU pragma: keep
#include <fileapi.h>
#include <io.h>
#include <wchar.h>

#include "xalloc.h"
#include "system/tempfiles.h"
#include "system/win_utf8.h"
#endif

#ifdef _WIN32
static void remove_capture_file(struct stream_capture *capture)
{
    wchar_t *path = win_utf8_path_to_wide(capture->path);
    if (path && DeleteFileW(path))
        tempfile_untrack(capture->path);
    free(path);
    free(capture->path);
    capture->path = NULL;
}
#endif

int stream_capture_open(struct stream_capture *capture)
{
    memset(capture, 0, sizeof(*capture));
#ifdef _WIN32
    int fd = tempfile_create("capture-", "", &capture->path);
    if (fd < 0)
        return -1;
    capture->stream = _fdopen(fd, "w+b");
    if (!capture->stream) {
        int saved_errno = errno;
        _close(fd);
        remove_capture_file(capture);
        errno = saved_errno;
        return -1;
    }
#else
    capture->stream = open_memstream(&capture->data, &capture->length);
#endif
    return capture->stream ? 0 : -1;
}

char *stream_capture_finish(struct stream_capture *capture, size_t *length)
{
    int failed = ferror(capture->stream);
#ifdef _WIN32
    if (fflush(capture->stream) != 0)
        failed = 1;
    long long size = _ftelli64(capture->stream);
    if (size < 0 || (unsigned long long)size >= SIZE_MAX ||
        _fseeki64(capture->stream, 0, SEEK_SET) != 0) {
        failed = 1;
    } else if (!failed) {
        capture->length = (size_t)size;
        capture->data = xmalloc(capture->length + 1);
        if (fread(capture->data, 1, capture->length, capture->stream) != capture->length)
            failed = 1;
        capture->data[capture->length] = '\0';
    }
#endif
    if (fclose(capture->stream) != 0)
        failed = 1;
    capture->stream = NULL;
#ifdef _WIN32
    remove_capture_file(capture);
#endif
    char *data = capture->data;
    capture->data = NULL;
    if (failed) {
        free(data);
        errno = EIO;
        return NULL;
    }
    if (length)
        *length = capture->length;
    return data;
}
