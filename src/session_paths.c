/* SPDX-License-Identifier: MIT */
#include "session_paths.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "xalloc.h"
#include "system/path.h"

static int is_uuid(const char *value, size_t length)
{
    if (length != 36)
        return 0;
    for (size_t i = 0; i < length; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (value[i] != '-')
                return 0;
        } else if (!isxdigit((unsigned char)value[i])) {
            return 0;
        }
    }
    return 1;
}

static int has_session_timestamp(const char *value)
{
    static const char shape[] = "dddd-dd-ddTdd-dd-ddZ";
    for (size_t i = 0; i < sizeof(shape) - 1; i++) {
        if (shape[i] == 'd') {
            if (!isdigit((unsigned char)value[i]))
                return 0;
        } else if (value[i] != shape[i]) {
            return 0;
        }
    }
    return 1;
}

/* Validate the whole basename so pruning cannot claim unrelated UUID-suffixed JSONL files. */
char *session_id_from_path(const char *path)
{
    if (!path)
        return NULL;
    const char *basename = path;
    for (const char *cursor = path; *cursor; cursor++) {
        if (path_is_separator(*cursor))
            basename = cursor + 1;
    }
    if (strlen(basename) != 63 || !has_session_timestamp(basename) || basename[20] != '_' ||
        strcmp(basename + 57, ".jsonl") != 0 || !is_uuid(basename + 21, 36))
        return NULL;
    char *id = xmalloc(37);
    memcpy(id, basename + 21, 36);
    id[36] = '\0';
    return id;
}

int session_path_is_standard(const char *path)
{
    char *id = session_id_from_path(path);
    int standard = id != NULL;
    free(id);
    return standard;
}
