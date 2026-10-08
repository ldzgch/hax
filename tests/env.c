/* SPDX-License-Identifier: MIT */
#include "env.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>

#include "system/win_utf8.h"
#endif

void t_env_set(const char *name, const char *value)
{
#ifdef _WIN32
    int result = _putenv_s(name, value);
    wchar_t *wide_name = win_utf8_to_wide(name);
    wchar_t *wide_value = win_utf8_to_wide(value);
    if (wide_name && wide_value && !SetEnvironmentVariableW(wide_name, wide_value))
        result = -1;
    free(wide_name);
    free(wide_value);
#else
    int result = setenv(name, value, 1);
#endif
    if (result) {
        fprintf(stderr, "t_env_set: cannot set %s\n", name);
        abort();
    }
}

void t_env_unset(const char *name)
{
#ifdef _WIN32
    int result = _putenv_s(name, "");
    wchar_t *wide_name = win_utf8_to_wide(name);
    if (!wide_name || !SetEnvironmentVariableW(wide_name, NULL))
        result = -1;
    free(wide_name);
#else
    int result = unsetenv(name);
#endif
    if (result) {
        fprintf(stderr, "t_env_unset: cannot unset %s\n", name);
        abort();
    }
}
