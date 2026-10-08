/* SPDX-License-Identifier: MIT */
#include "system/os.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>

#include "system/win_error.h"
#include "system/win_utf8.h"
#else
#include <sys/utsname.h>
#endif

#include "buf.h"
#include "xalloc.h"
#include "system/fs.h"

#define OS_RELEASE_FILE_CAP (64u * 1024u)

static char *os_release_decode_value(const char *value, size_t len)
{
    while (len > 0 && (value[len - 1] == '\r' || value[len - 1] == ' ' || value[len - 1] == '\t'))
        len--;
    while (len > 0 && (*value == ' ' || *value == '\t')) {
        value++;
        len--;
    }

    char quote = 0;
    if (len >= 2 && (value[0] == '\'' || value[0] == '"') && value[len - 1] == value[0]) {
        quote = value[0];
        value++;
        len -= 2;
    }

    /* Decode os-release quoting without evaluating expansions or other shell syntax. */
    struct buf out;
    buf_init(&out);
    for (size_t i = 0; i < len; i++) {
        if (value[i] == '\\' && quote != '\'' && i + 1 < len)
            i++;
        buf_append(&out, value + i, 1);
    }
    if (out.len == 0) {
        buf_free(&out);
        return NULL;
    }
    return buf_steal(&out);
}

static char *os_release_value(const char *path, const char *key)
{
    size_t len = 0;
    int truncated = 0;
    char *data = fs_read_file_capped(path, OS_RELEASE_FILE_CAP, &len, &truncated);
    if (!data || truncated) {
        free(data);
        return NULL;
    }

    size_t key_len = strlen(key);
    size_t off = 0;
    char *value = NULL;
    while (off < len) {
        size_t line_len = 0;
        while (off + line_len < len && data[off + line_len] != '\n')
            line_len++;
        const char *line = data + off;
        if (line_len > key_len && memcmp(line, key, key_len) == 0 && line[key_len] == '=') {
            free(value);
            value = os_release_decode_value(line + key_len + 1, line_len - key_len - 1);
        }
        off += line_len + (off + line_len < len ? 1 : 0);
    }
    free(data);
    return value;
}

char *os_release_name(const char *path)
{
    char *pretty = os_release_value(path, "PRETTY_NAME");
    if (pretty)
        return pretty;

    char *name = os_release_value(path, "NAME");
    if (!name)
        return NULL;
    char *version = os_release_value(path, "VERSION");
    if (!version)
        return name;
    char *combined = xasprintf("%s %s", name, version);
    free(name);
    free(version);
    return combined;
}

char *os_release_name_with_fallback(const char *primary_path, const char *fallback_path)
{
#ifdef _WIN32
    wchar_t *wide = win_utf8_to_wide(primary_path);
    if (!wide)
        return NULL;
    DWORD attributes = GetFileAttributesW(wide);
    DWORD error = GetLastError();
    free(wide);
    if (attributes != INVALID_FILE_ATTRIBUTES)
        return os_release_name(primary_path);
    win_error_set_errno(error);
#else
    struct stat st;
    if (stat(primary_path, &st) == 0)
        return os_release_name(primary_path);
#endif
    if (errno != ENOENT && errno != ENOTDIR)
        return NULL;
    return os_release_name(fallback_path);
}

#ifndef _WIN32
static char *linux_distribution_name(void)
{
    return os_release_name_with_fallback("/etc/os-release", "/usr/lib/os-release");
}
#endif

char *os_description(void)
{
#ifdef _WIN32
    SYSTEM_INFO info;
    GetNativeSystemInfo(&info);
    const char *architecture;
    switch (info.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64:
        architecture = "x64";
        break;
    case PROCESSOR_ARCHITECTURE_ARM64:
        architecture = "ARM64";
        break;
    case PROCESSOR_ARCHITECTURE_INTEL:
        architecture = "x86";
        break;
    default:
        architecture = "unknown architecture";
        break;
    }
    return xasprintf("Windows (%s)", architecture);
#else
    struct utsname u;
    if (uname(&u) != 0)
        return xstrdup("unknown");

    if (strcmp(u.sysname, "Linux") == 0) {
        char *distribution = linux_distribution_name();
        if (distribution) {
            char *description = xasprintf("%s (Linux %s)", distribution, u.release);
            free(distribution);
            return description;
        }
    }
    if (strcmp(u.sysname, "Darwin") == 0)
        return xasprintf("macOS (Darwin %s)", u.release);
    return xasprintf("%s %s", u.sysname, u.release);
#endif
}
