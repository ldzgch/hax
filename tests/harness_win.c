/* SPDX-License-Identifier: MIT */
#include <windows.h>
#include <errno.h>
#include <stdlib.h>
#include <wchar.h>

#include "harness.h"
#include "xalloc.h"
#include "system/path.h"
#include "system/rand.h"
#include "system/win_security.h"
#include "system/win_utf8.h"

static char **tempdirs;
static size_t tempdir_count;

/* Reparse points are removed as entries, never traversed. Only roots created by t_tempdir are
 * registered for cleanup; the user-provided temporary base itself is never removed. */
static int remove_tree(const wchar_t *path)
{
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        if (!(attributes & FILE_ATTRIBUTE_READONLY))
            return DeleteFileW(path) ? 0 : -1;
        /* Clearing a read-only attribute also changes hard links outside this scratch tree.

         * * Delete this entry without changing the shared metadata. */
        HANDLE file =
            CreateFileW(path, DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        if (file == INVALID_HANDLE_VALUE)
            return -1;
        FILE_DISPOSITION_INFO_EX disposition = {
            .Flags = FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS |
                     FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE};
        /* Older MinGW headers incorrectly gate FileDispositionInfoEx on an NTDDI-sized
         *
         * _WIN32_WINNT. Its Windows ABI value is 21, available on our Windows 10 target. */
        int removed = SetFileInformationByHandle(file, (FILE_INFO_BY_HANDLE_CLASS)21, &disposition,
                                                 sizeof(disposition));
        CloseHandle(file);
        return removed ? 0 : -1;
    }
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
        return RemoveDirectoryW(path) ? 0 : -1;

    size_t length = wcslen(path);
    wchar_t *pattern = xcalloc(length + 3, sizeof(*pattern));
    memcpy(pattern, path, length * sizeof(*pattern));
    memcpy(pattern + length, L"\\*", 3 * sizeof(*pattern));
    WIN32_FIND_DATAW entry;
    HANDLE listing = FindFirstFileW(pattern, &entry);
    free(pattern);
    int failed = 0;
    if (listing != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(entry.cFileName, L".") == 0 || wcscmp(entry.cFileName, L"..") == 0)
                continue;
            size_t name_length = wcslen(entry.cFileName);
            wchar_t *child = xcalloc(length + name_length + 2, sizeof(*child));
            memcpy(child, path, length * sizeof(*child));
            child[length] = L'\\';
            memcpy(child + length + 1, entry.cFileName, (name_length + 1) * sizeof(*child));
            if (remove_tree(child) < 0)
                failed = 1;
            free(child);
        } while (FindNextFileW(listing, &entry));
        if (GetLastError() != ERROR_NO_MORE_FILES)
            failed = 1;
        FindClose(listing);
    } else if (GetLastError() != ERROR_FILE_NOT_FOUND) {
        failed = 1;
    }
    return !failed && RemoveDirectoryW(path) ? 0 : -1;
}

static void tempdir_cleanup(void)
{
    for (size_t i = 0; i < tempdir_count; i++) {
        wchar_t *wide = win_utf8_path_to_wide(tempdirs[i]);
        if (!wide || remove_tree(wide) < 0)
            fprintf(stderr, "t_tempdir: failed to remove %s\n", tempdirs[i]);
        free(wide);
        free(tempdirs[i]);
    }
    free(tempdirs);
}

char *t_tempdir(void)
{
    wchar_t base[32768];
    DWORD length = GetTempPathW(sizeof(base) / sizeof(*base), base);
    if (!length || length >= sizeof(base) / sizeof(*base))
        abort();
    char *utf8_base = win_utf8_from_wide(base);
    if (!utf8_base)
        abort();
    struct win_private_security security;
    if (win_private_security_init(&security) < 0)
        abort();
    char *directory = NULL;
    for (int attempt = 0; attempt < 100; attempt++) {
        char uuid[37];
        gen_uuid_v4(uuid);
        char *name = xasprintf("hax_test_%s", uuid);
        char *candidate = path_join(utf8_base, name);
        free(name);
        wchar_t *wide = win_utf8_path_to_wide(candidate);
        if (!wide)
            abort();
        int created = CreateDirectoryW(wide, &security.attributes);
        DWORD error = GetLastError();
        free(wide);
        if (created) {
            directory = candidate;
            break;
        }
        free(candidate);
        if (error != ERROR_ALREADY_EXISTS)
            abort();
    }
    win_private_security_free(&security);
    free(utf8_base);
    if (!directory || !path_is_absolute(directory))
        abort();
    for (char *cursor = directory; *cursor; cursor++)
        if (*cursor == '\\')
            *cursor = '/';
    if (!tempdir_count)
        atexit(tempdir_cleanup);
    tempdirs = xrealloc(tempdirs, (tempdir_count + 1) * sizeof(*tempdirs));
    tempdirs[tempdir_count++] = directory;
    return directory;
}
