/* SPDX-License-Identifier: MIT */
#ifndef HAX_SYSTEM_STREAM_CAPTURE_H
#define HAX_SYSTEM_STREAM_CAPTURE_H

#include <stddef.h>
#include <stdio.h>

struct stream_capture {
    FILE *stream;
    char *data;
    size_t length;
    char *path;
};

/* Open a binary stdio sink. Windows uses a private temporary file; POSIX buffers in memory.
 * Return 0, or -1 with errno. The capture must be finished before opening it again. */
int stream_capture_open(struct stream_capture *capture);

/* Close the sink and return owned NUL-terminated bytes, preserving embedded NULs. Optional
 * length excludes the terminator. On failure, return NULL with errno. Always releases the sink. */
char *stream_capture_finish(struct stream_capture *capture, size_t *length);

#endif /* HAX_SYSTEM_STREAM_CAPTURE_H */
