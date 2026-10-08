/* SPDX-License-Identifier: MIT */
#include "system/path.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "buf.h"
#include "xalloc.h"

#ifdef _WIN32
#include <direct.h>

#include "system/win_utf8.h"
#endif

char *path_cwd(void)
{
#ifdef _WIN32
    wchar_t *wide = _wgetcwd(NULL, 0);
    if (!wide)
        return NULL;
    char *utf8 = win_utf8_from_wide(wide);
    free(wide);
    return utf8;
#else
    return getcwd(NULL, 0);
#endif
}

int path_is_separator(char c)
{
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

#ifdef _WIN32
static int ascii_drive_letter(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
#endif

size_t path_root_length(const char *path)
{
    if (!path || !*path)
        return 0;
#ifdef _WIN32
    size_t drive_offset = 0;
    if (path_is_separator(path[0]) && path_is_separator(path[1])) {
        if (path[2] == '?' && path_is_separator(path[3])) {
            drive_offset = 4;
            if (strncmp(path + 4, "UNC\\", 4) == 0 || strncmp(path + 4, "UNC/", 4) == 0)
                drive_offset = 8;
        }
        if (drive_offset != 4) {
            size_t pos = drive_offset ? drive_offset : 2;
            size_t server = pos;
            while (path[pos] && !path_is_separator(path[pos]))
                pos++;
            if (pos == server || !path[pos])
                return 0;
            size_t share = ++pos;
            while (path[pos] && !path_is_separator(path[pos]))
                pos++;
            return pos > share ? pos : 0;
        }
    }
    const char *drive = path + drive_offset;
    if (ascii_drive_letter(drive[0]) && drive[1] == ':' && path_is_separator(drive[2]))
        return drive_offset + 3;
    if (drive_offset)
        return 0;
#endif
    return path_is_separator(path[0]) ? 1 : 0;
}

int path_is_absolute(const char *path)
{
    return path_root_length(path) != 0;
}

static int path_prefix_equal(const char *a, const char *b, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (!a[i] || !b[i])
            return 0;
        if (a[i] == b[i] || (path_is_separator(a[i]) && path_is_separator(b[i])))
            continue;
#ifdef _WIN32
        /* Windows may enable case-sensitive directories; fold only the drive designator. */
        if (i == 0 && ascii_drive_letter(a[i]) && ascii_drive_letter(b[i]) && a[1] == ':' &&
            b[1] == ':' && (a[i] | 32) == (b[i] | 32))
            continue;
#endif
        return 0;
    }
    return 1;
}

static char *environment_value(const char *name)
{
#ifdef _WIN32
    return win_utf8_getenv(name);
#else
    return xstrdup(getenv(name));
#endif
}

char *path_home(void)
{
    char *home = environment_value("HOME");
#ifdef _WIN32
    if (!home || !*home) {
        free(home);
        home = environment_value("USERPROFILE");
    }
#endif
    if (home && !*home) {
        free(home);
        home = NULL;
    }
    return home;
}

static size_t path_len_without_trailing_slashes(const char *path)
{
    size_t len = strlen(path);

    size_t root_len = path_root_length(path);
    while (len > root_len && path_is_separator(path[len - 1]))
        len--;
    return len;
}

char *path_join(const char *base, const char *suffix)
{
    size_t base_len = path_len_without_trailing_slashes(base);
    while (path_is_separator(*suffix))
        suffix++;

    int base_ends_separator = base_len > 0 && path_is_separator(base[base_len - 1]);
    struct buf joined;
    buf_init(&joined);
    buf_append(&joined, base, base_len);
    if (!base_ends_separator)
        buf_append_str(&joined, "/");
    buf_append_str(&joined, suffix);
    return buf_steal(&joined);
}

char *path_expand_home(const char *path)
{
    if (!path)
        return NULL;
    if (path[0] != '~' || (path[1] != '\0' && !path_is_separator(path[1])))
        return xstrdup(path);

    char *home = path_home();
    if (!home || !*home) {
        free(home);
        return xstrdup(path);
    }
    if (path[1] == '\0')
        return home;
    char *result = path_join(home, path + 2);
    free(home);
    return result;
}

char *path_collapse_home(const char *path)
{
    if (!path)
        return NULL;

    char *home = path_home();
    char *result = NULL;
    if (home && *home) {
        size_t home_len = path_len_without_trailing_slashes(home);
        if (path_prefix_equal(path, home, home_len)) {
            if (path[home_len] == '\0')
                result = xstrdup("~");
            else if (home_len > 0 && path_is_separator(home[home_len - 1]))
                result = xasprintf("~/%s", path + home_len);
            else if (path_is_separator(path[home_len]))
                result = xasprintf("~%s", path + home_len);
        }
    }
    free(home);
    return result ? result : xstrdup(path);
}

static int path_has_parent_component(const char *path)
{
    const char *component = path;

    while (*component) {
        while (path_is_separator(*component))
            component++;
        const char *end = component;
        while (*end && !path_is_separator(*end))
            end++;
        size_t len = (size_t)(end - component);
        if (len == 2 && component[0] == '.' && component[1] == '.')
            return 1;
        if (!*end)
            break;
        component = end + 1;
    }
    return 0;
}

char *path_relativize(const char *path, const char *cwd)
{
    if (!path_is_absolute(path) || !path_is_absolute(cwd))
        return NULL;
    if (path_has_parent_component(path))
        return NULL;

    size_t cwd_len = path_len_without_trailing_slashes(cwd);
    const char *relative;
    if (!path_prefix_equal(path, cwd, cwd_len))
        return NULL;
    if (cwd_len > 0 && path_is_separator(cwd[cwd_len - 1])) {
        relative = path + cwd_len;
    } else {
        if (!path_is_separator(path[cwd_len]))
            return NULL;
        relative = path + cwd_len + 1;
    }

    while (path_is_separator(*relative))
        relative++;
    return *relative ? xstrdup(relative) : NULL;
}

int path_climb_to_parent(char *dir)
{
    size_t root_len = path_root_length(dir);
    size_t len = path_len_without_trailing_slashes(dir);
    if (len <= root_len)
        return 0;
    size_t parent_len = len;
    while (parent_len > root_len && !path_is_separator(dir[parent_len - 1]))
        parent_len--;
    if (!parent_len)
        return 0;
    if (parent_len > root_len)
        parent_len--;
    dir[parent_len] = '\0';
    return 1;
}

static char *xdg_hax_path(const char *env_name, const char *home_relative,
                          const char *relative_path)
{
    char *xdg_base = environment_value(env_name);
    char *result = NULL;
    if (xdg_base && *xdg_base)
        result = xasprintf("%s/hax/%s", xdg_base, relative_path);
    free(xdg_base);
    if (result)
        return result;
#ifdef _WIN32
    int is_config = strcmp(env_name, "XDG_CONFIG_HOME") == 0;
    char *windows_base = environment_value(is_config ? "APPDATA" : "LOCALAPPDATA");
    if (windows_base && *windows_base) {
        const char *kind = is_config                                 ? ""
                           : strcmp(env_name, "XDG_STATE_HOME") == 0 ? "state/"
                                                                     : "cache/";
        result = xasprintf("%s/hax/%s%s", windows_base, kind, relative_path);
    }
    free(windows_base);
    if (result)
        return result;
#endif
    char *home = path_home();
    if (home && *home)
        result = xasprintf("%s/%s/hax/%s", home, home_relative, relative_path);
    free(home);
    return result;
}

char *xdg_hax_config_path(const char *relative_path)
{
    return xdg_hax_path("XDG_CONFIG_HOME", ".config", relative_path);
}

char *xdg_hax_state_path(const char *relative_path)
{
    return xdg_hax_path("XDG_STATE_HOME", ".local/state", relative_path);
}

char *xdg_hax_cache_path(const char *relative_path)
{
    return xdg_hax_path("XDG_CACHE_HOME", ".cache", relative_path);
}
