/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <string.h>

#include "env.h"
#include "harness.h"
#ifdef _WIN32
#include "system/win_utf8.h"
#endif

static char *read_path(void)
{
#ifdef _WIN32
    return win_utf8_getenv_value("PATH");
#else
    const char *current = getenv("PATH");
    char *saved = NULL;
    if (current) {
        saved = strdup(current);
        if (!saved)
            abort();
    }
    return saved;
#endif
}

char *t_path_replace(const char *value)
{
    char *saved = read_path();
    if (value)
        t_env_set("PATH", value);
    else
        t_env_unset("PATH");
    return saved;
}

char *t_path_prepend(const char *dir)
{
    char *current = read_path();
    if (!current || !*current) {
        free(current);
        return t_path_replace(dir);
    }
    size_t len = strlen(dir) + 1 + strlen(current) + 1;
    char *combined = malloc(len);
    if (!combined)
        abort();
#ifdef _WIN32
    snprintf(combined, len, "%s;%s", dir, current);
#else
    snprintf(combined, len, "%s:%s", dir, current);
#endif
    free(current);
    char *saved = t_path_replace(combined);
    free(combined);
    return saved;
}

void t_path_restore(char *saved)
{
    if (saved)
        t_env_set("PATH", saved);
    else
        t_env_unset("PATH");
    free(saved);
}
