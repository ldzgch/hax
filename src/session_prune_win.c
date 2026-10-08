/* SPDX-License-Identifier: MIT */
#include "session_prune_win.h"

#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>

#include "session_paths.h"
#include "system/bg_job.h"
#include "system/file_lock.h"
#include "system/path.h"
#include "system/win_utf8.h"

static HANDLE open_directory(const char *path, int removable)
{
    wchar_t *wide = win_utf8_to_wide(path);
    if (!wide)
        return INVALID_HANDLE_VALUE;
    /* Deny directory rename/deletion and reparse-point changes during the anchored walk. */
    HANDLE handle =
        CreateFileW(wide, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | (removable ? DELETE : 0),
                    FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    free(wide);
    if (handle == INVALID_HANDLE_VALUE)
        return handle;
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(handle, &info) ||
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        CloseHandle(handle);
        return INVALID_HANDLE_VALUE;
    }
    return handle;
}

static HANDLE find_first(const char *directory, WIN32_FIND_DATAW *data)
{
    char *pattern = path_join(directory, "*");
    wchar_t *wide = win_utf8_to_wide(pattern);
    free(pattern);
    if (!wide)
        return INVALID_HANDLE_VALUE;
    HANDLE search = FindFirstFileW(wide, data);
    free(wide);
    return search;
}

HANDLE session_prune_anchor_win(const char *directory)
{
    return open_directory(directory, 0);
}

static int same_file(const BY_HANDLE_FILE_INFORMATION *left,
                     const BY_HANDLE_FILE_INFORMATION *right)
{
    return left->dwVolumeSerialNumber == right->dwVolumeSerialNumber &&
           left->nFileIndexHigh == right->nFileIndexHigh &&
           left->nFileIndexLow == right->nFileIndexLow;
}

static int old_regular(HANDLE handle, time_t cutoff, BY_HANDLE_FILE_INFORMATION *info)
{
    if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, info) ||
        (info->dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        !info->nNumberOfLinks)
        return 0;
    uint64_t ticks = ((uint64_t)info->ftLastWriteTime.dwHighDateTime << 32) |
                     info->ftLastWriteTime.dwLowDateTime;
    int64_t modified = (int64_t)(ticks / UINT64_C(10000000)) - INT64_C(11644473600);
    return modified < cutoff;
}

static void prune_file(const char *path, time_t cutoff, const BY_HANDLE_FILE_INFORMATION *excluded)
{
    wchar_t *wide = win_utf8_to_wide(path);
    if (!wide)
        return;
    /* Deletion targets this handle's object. Denying share-delete also prevents it being renamed
     * outside the pinned project bucket between validation and disposition. */
    HANDLE handle = CreateFileW(wide, GENERIC_READ | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    free(wide);
    if (handle == INVALID_HANDLE_VALUE)
        return;
    BY_HANDLE_FILE_INFORMATION observed;
    if (!old_regular(handle, cutoff, &observed) || (excluded && same_file(&observed, excluded))) {
        CloseHandle(handle);
        return;
    }
    int fd = _open_osfhandle((intptr_t)handle, _O_RDONLY | _O_BINARY | _O_NOINHERIT);
    if (fd < 0) {
        CloseHandle(handle);
        return;
    }
    BY_HANDLE_FILE_INFORMATION locked;
    if (file_lock_fd(fd, 1, 1) == 0 && old_regular(handle, cutoff, &locked) &&
        same_file(&observed, &locked)) {
        FILE_DISPOSITION_INFO disposition = {.DeleteFile = TRUE};
        (void)SetFileInformationByHandle(handle, FileDispositionInfo, &disposition,
                                         sizeof(disposition));
    }
    _close(fd);
}

static int prune_project(const char *directory, time_t cutoff,
                         const BY_HANDLE_FILE_INFORMATION *excluded, struct bg_job *job)
{
    HANDLE project = open_directory(directory, 1);
    if (project == INVALID_HANDLE_VALUE)
        return 0;
    int result = 0;
    WIN32_FIND_DATAW data;
    HANDLE search = find_first(directory, &data);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (bg_job_cancel_requested(job)) {
                result = -1;
                break;
            }
            if (data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
                continue;
            char *name = win_utf8_from_wide(data.cFileName);
            if (!name)
                continue;
            if (session_path_is_standard(name)) {
                char *path = path_join(directory, name);
                prune_file(path, cutoff, excluded);
                free(path);
            }
            free(name);
        } while (FindNextFileW(search, &data));
        FindClose(search);
    }
    if (!result) {
        /* Windows directory disposition, like rmdir, rejects nonempty buckets. */
        FILE_DISPOSITION_INFO disposition = {.DeleteFile = TRUE};
        (void)SetFileInformationByHandle(project, FileDispositionInfo, &disposition,
                                         sizeof(disposition));
    }
    CloseHandle(project);
    return result;
}

int session_prune_tree_win(const char *directory, time_t cutoff, const char *exclude_path,
                           struct bg_job *job)
{
    HANDLE root = open_directory(directory, 0);
    if (root == INVALID_HANDLE_VALUE)
        return 0;
    BY_HANDLE_FILE_INFORMATION excluded_info;
    BY_HANDLE_FILE_INFORMATION *excluded = NULL;
    if (exclude_path) {
        wchar_t *wide = win_utf8_to_wide(exclude_path);
        HANDLE handle = wide ? CreateFileW(wide, FILE_READ_ATTRIBUTES,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                           NULL, OPEN_EXISTING, 0, NULL)
                             : INVALID_HANDLE_VALUE;
        free(wide);
        if (handle != INVALID_HANDLE_VALUE) {
            if (GetFileInformationByHandle(handle, &excluded_info))
                excluded = &excluded_info;
            CloseHandle(handle);
        }
    }
    int result = 0;
    WIN32_FIND_DATAW data;
    HANDLE search = find_first(directory, &data);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (bg_job_cancel_requested(job)) {
                result = -1;
                break;
            }
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0)
                continue;
            char *name = win_utf8_from_wide(data.cFileName);
            if (!name)
                continue;
            char *path = path_join(directory, name);
            free(name);
            result = prune_project(path, cutoff, excluded, job);
            free(path);
            if (result < 0)
                break;
        } while (FindNextFileW(search, &data));
        FindClose(search);
    }
    CloseHandle(root);
    return result;
}
