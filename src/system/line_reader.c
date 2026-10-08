/* SPDX-License-Identifier: MIT */
#include "system/line_reader.h"

#include <errno.h>
#include <stdint.h>

#include "xalloc.h"

ptrdiff_t line_reader_get(FILE *stream, char **line, size_t *capacity)
{
    if (!stream || !line || !capacity) {
        errno = EINVAL;
        return -1;
    }
    if (!*line)
        *capacity = 0;
    size_t length = 0;
    int byte;
    while ((byte = fgetc(stream)) != EOF) {
        if (length >= PTRDIFF_MAX - 1) {
            errno = EOVERFLOW;
            return -1;
        }
        if (length + 1 >= *capacity) {
            size_t next = *capacity ? *capacity : 256;
            if (next <= (size_t)PTRDIFF_MAX / 2)
                next *= 2;
            else
                next = PTRDIFF_MAX;
            *line = xrealloc(*line, next);
            *capacity = next;
        }
        (*line)[length++] = (char)byte;
        if (byte == '\n')
            break;
    }
    if (!length)
        return -1;
    (*line)[length] = '\0';
    return (ptrdiff_t)length;
}
