/* SPDX-License-Identifier: MIT */
#include "text/windows_argv.h"

#include <errno.h>

#include "buf.h"

static void append_slashes(struct buf *command, size_t count)
{
    while (count--)
        buf_append_str(command, "\\");
}

static void append_argument(struct buf *command, const char *argument)
{
    buf_append_str(command, "\"");
    const char *cursor = argument;
    for (;;) {
        size_t slashes = 0;
        while (*cursor == '\\') {
            slashes++;
            cursor++;
        }
        if (*cursor == '"') {
            append_slashes(command, slashes * 2 + 1);
            buf_append(command, cursor++, 1);
        } else if (!*cursor) {
            append_slashes(command, slashes * 2);
            break;
        } else {
            append_slashes(command, slashes);
            buf_append(command, cursor++, 1);
        }
    }
    buf_append_str(command, "\"");
}

char *windows_argv_encode(const char *const *argv)
{
    if (!argv || !argv[0] || !*argv[0]) {
        errno = EINVAL;
        return NULL;
    }
    struct buf command;
    buf_init(&command);
    for (size_t i = 0; argv[i]; i++) {
        if (i)
            buf_append_str(&command, " ");
        append_argument(&command, argv[i]);
    }
    return buf_steal(&command);
}
