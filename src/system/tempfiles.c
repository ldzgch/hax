/* SPDX-License-Identifier: MIT */
#include "system/tempfiles.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
/* The Windows API headers depend on this umbrella header's target declarations. */
#include <windows.h> // IWYU pragma: keep
#include <errhandlingapi.h>
#include <fileapi.h>
#include <minwindef.h>
#include <wchar.h>
#include <winerror.h>
#else
#include <unistd.h>
#endif

#include "xalloc.h"
#include "system/fs.h"
#include "system/path.h"
#ifdef _WIN32
#include "system/rand.h"
#include "system/win_error.h"
#include "system/win_security.h"
#include "system/win_utf8.h"
#endif
#include "text/utf8.h"

#define TEMPFILE_MAX_ATTEMPTS 100

struct path_list {
    char **items;
    size_t count;
    size_t capacity;
};

static struct path_list tracked_files;
static struct path_list retired_dirs;
static char *active_dir;
static char *active_tmpdir;
static unsigned next_file_id;
static int cleanup_registered;

static void path_list_add(struct path_list *list, const char *path)
{
    if (list->count == list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 4;
        list->items = xrealloc(list->items, list->capacity * sizeof(*list->items));
    }
    list->items[list->count++] = xstrdup(path);
}

static void path_list_remove(struct path_list *list, size_t index)
{
    free(list->items[index]);
    list->count--;
    list->items[index] = list->items[list->count];
}

static void forget_active_dir(void)
{
    free(active_dir);
    free(active_tmpdir);
    active_dir = NULL;
    active_tmpdir = NULL;
}

static int remove_path(const char *path, int directory)
{
#ifdef _WIN32
    wchar_t *wide = win_utf8_path_to_wide(path);
    if (!wide)
        return -1;
    int removed = directory ? RemoveDirectoryW(wide) : DeleteFileW(wide);
    DWORD error = GetLastError();
    free(wide);
    if (!removed)
        win_error_set_errno(error);
    return removed ? 0 : -1;
#else
    return directory ? rmdir(path) : unlink(path);
#endif
}

void tempfiles_cleanup(void)
{
    for (size_t i = tracked_files.count; i-- > 0;) {
        if (remove_path(tracked_files.items[i], 0) == 0 || errno == ENOENT)
            path_list_remove(&tracked_files, i);
    }

    for (size_t i = retired_dirs.count; i-- > 0;) {
        if (remove_path(retired_dirs.items[i], 1) == 0 || errno == ENOENT)
            path_list_remove(&retired_dirs, i);
    }

    if (active_dir && (remove_path(active_dir, 1) == 0 || errno == ENOENT))
        forget_active_dir();
}

static void register_cleanup(void)
{
    if (!cleanup_registered) {
        atexit(tempfiles_cleanup);
        cleanup_registered = 1;
    }
}

static void track_file(const char *path)
{
    register_cleanup();
    path_list_add(&tracked_files, path);
}

void tempfile_untrack(const char *path)
{
    for (size_t i = 0; i < tracked_files.count; i++) {
        if (strcmp(tracked_files.items[i], path) == 0) {
            path_list_remove(&tracked_files, i);
            return;
        }
    }
}

static int string_is_valid_utf8(const char *text)
{
    return utf8_is_valid(text, strlen(text));
}

static int name_fragment_is_valid(const char *fragment)
{
    if (!fragment || strchr(fragment, '/') || !string_is_valid_utf8(fragment))
        return 0;
#ifdef _WIN32
    if (strpbrk(fragment, "\\:*?\"<>|"))
        return 0;
    for (const unsigned char *p = (const unsigned char *)fragment; *p; p++)
        if (*p < 32)
            return 0;
#endif
    return 1;
}

static char *temporary_base(void)
{
#ifdef _WIN32
    char *tmpdir = win_utf8_getenv("TMPDIR");
    if (tmpdir && *tmpdir && string_is_valid_utf8(tmpdir))
        return tmpdir;
    free(tmpdir);
    wchar_t base[32768];
    DWORD length = GetTempPathW(sizeof(base) / sizeof(*base), base);
    if (!length) {
        win_error_set_errno(GetLastError());
        return NULL;
    }
    if (length >= sizeof(base) / sizeof(*base)) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    return win_utf8_from_wide(base);
#else
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || !*tmpdir || !string_is_valid_utf8(tmpdir))
        tmpdir = "/tmp";
    return xstrdup(tmpdir);
#endif
}

static char *create_private_directory(const char *tmpdir)
{
#ifdef _WIN32
    struct win_private_security security;
    if (win_private_security_init(&security) < 0)
        return NULL;
    char *directory = NULL;
    for (int attempt = 0; attempt < TEMPFILE_MAX_ATTEMPTS; attempt++) {
        char uuid[37];
        gen_uuid_v4(uuid);
        char *name = xasprintf("hax-%s", uuid);
        char *candidate = path_join(tmpdir, name);
        free(name);
        wchar_t *wide = win_utf8_path_to_wide(candidate);
        if (!wide) {
            free(candidate);
            break;
        }
        int created = CreateDirectoryW(wide, &security.attributes);
        DWORD error = GetLastError();
        free(wide);
        if (created) {
            directory = candidate;
            break;
        }
        free(candidate);
        win_error_set_errno(error);
        if (error != ERROR_ALREADY_EXISTS)
            break;
    }
    win_private_security_free(&security);
    return directory;
#else
    char *template = path_join(tmpdir, "hax-XXXXXX");
    if (!mkdtemp(template)) {
        int saved_errno = errno;
        free(template);
        errno = saved_errno;
        return NULL;
    }
    return template;
#endif
}

static const char *ensure_temp_dir(void)
{
    char *tmpdir = temporary_base();
    if (!tmpdir)
        return NULL;
    if (active_dir && strcmp(active_tmpdir, tmpdir) == 0) {
        free(tmpdir);
        return active_dir;
    }
    char *directory = create_private_directory(tmpdir);
    if (!directory) {
        free(tmpdir);
        return NULL;
    }

    if (active_dir) {
        path_list_add(&retired_dirs, active_dir);
        free(active_dir);
    }
    free(active_tmpdir);
    active_dir = directory;
    active_tmpdir = tmpdir;
    register_cleanup();
    return active_dir;
}

int tempfile_create(const char *prefix, const char *suffix, char **path_out)
{
    *path_out = NULL;
    if (!name_fragment_is_valid(prefix) || !name_fragment_is_valid(suffix)) {
        errno = EINVAL;
        return -1;
    }

    const char *dir = ensure_temp_dir();
    if (!dir)
        return -1;

    /* The private directory supplies the randomness; sequential entry names stay readable. */
    for (int attempt = 0; attempt < TEMPFILE_MAX_ATTEMPTS; attempt++) {
        char *path = xasprintf("%s/%s%u%s", dir, prefix, ++next_file_id, suffix);
        int fd = fs_open_private(path, 1);
        if (fd >= 0) {
            track_file(path);
            *path_out = path;
            return fd;
        }

        int open_errno = errno;
        free(path);
        if (open_errno == ENOENT) {
            /* A temporary-file reaper may remove the cached directory during long sessions. */
            forget_active_dir();
            dir = ensure_temp_dir();
            if (!dir)
                return -1;
            continue;
        }
        if (open_errno != EEXIST) {
            errno = open_errno;
            return -1;
        }
    }

    errno = EEXIST;
    return -1;
}
