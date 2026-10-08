/* SPDX-License-Identifier: MIT */
#include "tools/bash_env.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <wchar.h>

#include "system/win_error.h"
#include "system/win_utf8.h"
#endif

#include "xalloc.h"
#include "text/fmt.h"

static char *selection_env[4];
static size_t selection_count;

struct env_override {
    const char *name;
    const char *assignment;
};

static const struct env_override FIXED_OVERRIDES[] = {
    /* Prevent interactive pagers from opening the controlling terminal. */
    {"PAGER", "PAGER=cat"},
    {"GIT_PAGER", "GIT_PAGER=cat"},
    {"MANPAGER", "MANPAGER=cat"},
    {"SYSTEMD_PAGER", "SYSTEMD_PAGER=cat"},
    {"GH_PAGER", "GH_PAGER=cat"},

    /* Fail closed when a command tries to launch an editor. */
    {"GIT_EDITOR", "GIT_EDITOR=false"},
    {"GIT_SEQUENCE_EDITOR", "GIT_SEQUENCE_EDITOR=false"},
    {"VISUAL", "VISUAL=false"},
    {"EDITOR", "EDITOR=false"},

    /* Suppress terminal formatting without NO_COLOR/FORCE_COLOR conflicts. */
    {"TERM", "TERM=dumb"},
    {"COLORTERM", "COLORTERM="},

    /* Prompts cannot be answered; agent-aware tools can select unattended behavior. */
    {"GIT_TERMINAL_PROMPT", "GIT_TERMINAL_PROMPT=0"},
    {"AI_AGENT", "AI_AGENT=hax"},

    /* Keep piped output timely and stable instead of buffered or carriage-return redrawn. */
    {"PYTHONUNBUFFERED", "PYTHONUNBUFFERED=1"},
    {"TQDM_DISABLE", "TQDM_DISABLE=1"},

    /* A nested hax would truncate and then share the parent's live logs. */
    {"HAX_TRACE", "HAX_TRACE="},
    {"HAX_TRANSCRIPT", "HAX_TRANSCRIPT="},
};

void bash_env_set_selection(const char *provider, const char *model, const char *effort)
{
    for (size_t i = 0; i < selection_count; i++)
        free(selection_env[i]);
    selection_count = 0;
    if (!provider || !*provider)
        return;
    selection_env[selection_count++] = xasprintf("HAX_PROVIDER=%s", provider);
    selection_env[selection_count++] = xasprintf("HAX_MODEL=%s", model ? model : "");
    selection_env[selection_count++] = xasprintf("HAX_EFFORT=%s", effort ? effort : "");
    selection_env[selection_count++] = xstrdup("HAX_PRESET=");
}

static int entry_has_name(const char *entry, const char *name)
{
    size_t name_len = strlen(name);
#ifdef _WIN32
    return _strnicmp(entry, name, name_len) == 0 && entry[name_len] == '=';
#else
    return strncmp(entry, name, name_len) == 0 && entry[name_len] == '=';
#endif
}

static int entry_is_overridden(const char *entry, const char *const *dynamic, size_t dynamic_count)
{
    for (size_t i = 0; i < sizeof(FIXED_OVERRIDES) / sizeof(FIXED_OVERRIDES[0]); i++) {
        if (entry_has_name(entry, FIXED_OVERRIDES[i].name))
            return 1;
    }
    for (size_t i = 0; i < dynamic_count; i++) {
        const char *separator = strchr(dynamic[i], '=');
        size_t name_len = (size_t)(separator - dynamic[i]);
#ifdef _WIN32
        int matches = _strnicmp(entry, dynamic[i], name_len) == 0;
#else
        int matches = strncmp(entry, dynamic[i], name_len) == 0;
#endif
        if (matches && entry[name_len] == '=')
            return 1;
    }
    return 0;
}

static const char *subagent_depth_assignment(void)
{
    static char assignment[64];

    if (!assignment[0]) {
        const char *value = getenv("HAX_SUBAGENT_DEPTH");
        int depth = 0;
        /* Malformed inherited depth must fail closed at the recursion cap. */
        if (value && *value && (!parse_int(value, &depth) || depth < 0))
            depth = HAX_SUBAGENT_MAX_DEPTH;
        int child_depth = depth >= HAX_SUBAGENT_MAX_DEPTH ? depth : depth + 1;
        snprintf(assignment, sizeof(assignment), "HAX_SUBAGENT_DEPTH=%d", child_depth);
    }
    return assignment;
}

char **bash_build_child_env(void)
{
#ifdef _WIN32
    wchar_t *native = GetEnvironmentStringsW();
    if (!native) {
        win_error_set_errno(GetLastError());
        return NULL;
    }
    size_t native_count = 0;
    for (const wchar_t *cursor = native; *cursor; cursor += wcslen(cursor) + 1)
        native_count++;
    char **inherited = xcalloc(native_count + 1, sizeof(*inherited));
    size_t converted = 0;
    for (const wchar_t *cursor = native; *cursor; cursor += wcslen(cursor) + 1) {
        inherited[converted] = win_utf8_from_wide(cursor);
        if (!inherited[converted]) {
            int saved_errno = errno;
            for (size_t i = 0; i < converted; i++)
                free(inherited[i]);
            free(inherited);
            FreeEnvironmentStringsW(native);
            errno = saved_errno;
            return NULL;
        }
        converted++;
    }
    FreeEnvironmentStringsW(native);
#else
    extern char **environ;
    char **inherited = environ;
#endif

    size_t inherited_count = 0;
    while (inherited[inherited_count])
        inherited_count++;

    size_t dynamic_count = selection_count + 1;
    const char **dynamic = xmalloc(dynamic_count * sizeof(*dynamic));
    for (size_t i = 0; i < selection_count; i++)
        dynamic[i] = selection_env[i];
    dynamic[selection_count] = subagent_depth_assignment();

    size_t fixed_count = sizeof(FIXED_OVERRIDES) / sizeof(FIXED_OVERRIDES[0]);
    char **child_env =
        xmalloc((inherited_count + fixed_count + dynamic_count + 1) * sizeof(*child_env));
    size_t child_count = 0;
    for (size_t i = 0; inherited[i]; i++) {
        if (!entry_is_overridden(inherited[i], dynamic, dynamic_count))
            child_env[child_count++] = inherited[i];
    }
    for (size_t i = 0; i < fixed_count; i++)
        child_env[child_count++] = (char *)FIXED_OVERRIDES[i].assignment;
    for (size_t i = 0; i < dynamic_count; i++)
        child_env[child_count++] = (char *)dynamic[i];
    child_env[child_count] = NULL;

    /* One allocation owns both the vector and its strings, preserving the caller's free(envp). */
    size_t pointer_bytes = (child_count + 1) * sizeof(*child_env);
    size_t total = pointer_bytes;
    int overflow = 0;
    for (size_t i = 0; i < child_count; i++) {
        size_t length = strlen(child_env[i]) + 1;
        if (length > SIZE_MAX - total) {
            overflow = 1;
            break;
        }
        total += length;
    }
    char **owned = NULL;
    if (!overflow) {
        owned = xmalloc(total);
        char *cursor = (char *)owned + pointer_bytes;
        for (size_t i = 0; i < child_count; i++) {
            size_t length = strlen(child_env[i]) + 1;
            memcpy(cursor, child_env[i], length);
            owned[i] = cursor;
            cursor += length;
        }
        owned[child_count] = NULL;
    }
    free(dynamic);
    free(child_env);
#ifdef _WIN32
    for (size_t i = 0; i < inherited_count; i++)
        free(inherited[i]);
    free(inherited);
#endif
    if (overflow)
        errno = EOVERFLOW;
    return owned;
}
