/* SPDX-License-Identifier: MIT */
#include "session_storage.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
/* The Windows API headers depend on this umbrella header's target declarations. */
#include <windows.h> // IWYU pragma: keep
#include <aclapi.h>
#include <fileapi.h>
#include <io.h>
#include <minwinbase.h>
#include <minwindef.h>
#include <wchar.h>
#include <winerror.h>
#include <winnt.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "xalloc.h"
#include "system/file_lock.h"
#include "system/fs.h"
#include "system/path.h"
#ifdef _WIN32
#include "system/win_error.h"
#include "system/win_security.h"
#include "system/win_utf8.h"

static HANDLE open_native(const char *path, DWORD access, DWORD creation, int private)
{
    wchar_t *wide = win_utf8_to_wide(path);
    if (!wide)
        return INVALID_HANDLE_VALUE;
    struct win_private_security security;
    if (private && win_private_security_init(&security) < 0) {
        free(wide);
        return INVALID_HANDLE_VALUE;
    }
    HANDLE handle = CreateFileW(
        wide, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        private ? &security.attributes : NULL, creation, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    DWORD error = GetLastError();
    free(wide);
    if (handle != INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION info;
        if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &info) ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
            error = ERROR_INVALID_PARAMETER;
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
        } else if (private) {
            error = SetSecurityInfo(handle, SE_FILE_OBJECT,
                                    DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                    NULL, NULL, security.acl, NULL);
            if (error != ERROR_SUCCESS) {
                CloseHandle(handle);
                handle = INVALID_HANDLE_VALUE;
            }
        }
    }
    if (private)
        win_private_security_free(&security);
    if (handle == INVALID_HANDLE_VALUE)
        win_error_set_errno(error);
    return handle;
}

static int native_linked(HANDLE handle)
{
    FILE_STANDARD_INFO info;
    if (!GetFileInformationByHandleEx(handle, FileStandardInfo, &info, sizeof(info))) {
        win_error_set_errno(GetLastError());
        return 0;
    }
    if (info.DeletePending || !info.NumberOfLinks) {
        errno = ENOENT;
        return 0;
    }
    return 1;
}
#endif

FILE *session_storage_open(const char *path, int append)
{
    int saved_errno;
#ifdef _WIN32
    DWORD access = GENERIC_READ | WRITE_DAC | FILE_WRITE_ATTRIBUTES |
                   (append ? FILE_APPEND_DATA : GENERIC_WRITE);
    HANDLE handle = open_native(path, access, append ? OPEN_EXISTING : OPEN_ALWAYS, 1);
    if (handle == INVALID_HANDLE_VALUE)
        return NULL;
    int fd = _open_osfhandle((intptr_t)handle,
                             _O_RDWR | _O_BINARY | _O_NOINHERIT | (append ? _O_APPEND : 0));
    if (fd < 0) {
        CloseHandle(handle);
        return NULL;
    }
    if (file_lock_fd(fd, 0, 0) < 0 || !native_linked(handle))
        goto error;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(handle, &size)) {
        win_error_set_errno(GetLastError());
        goto error;
    }
    if (append && size.QuadPart > 0) {
        LARGE_INTEGER last = {.QuadPart = size.QuadPart - 1};
        char byte;
        DWORD count;
        if (!SetFilePointerEx(handle, last, NULL, FILE_BEGIN) ||
            !ReadFile(handle, &byte, 1, &count, NULL)) {
            win_error_set_errno(GetLastError());
            goto error;
        }
        if (count != 1) {
            errno = EIO;
            goto error;
        }
        if (byte != '\n') {
            if (!WriteFile(handle, "\n", 1, &count, NULL)) {
                win_error_set_errno(GetLastError());
                goto error;
            }
            if (count != 1) {
                errno = EIO;
                goto error;
            }
        }
    } else if (!append) {
        LARGE_INTEGER start = {0};
        if (!SetFilePointerEx(handle, start, NULL, FILE_BEGIN) || !SetEndOfFile(handle)) {
            win_error_set_errno(GetLastError());
            goto error;
        }
    }
#else
    int flags = O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK | O_RDWR;
    flags |= append ? O_APPEND : O_CREAT;
    int fd = open(path, flags, 0600);
    if (fd < 0)
        return NULL;
    struct stat info;
    if (fstat(fd, &info) != 0)
        goto error;
    if (!S_ISREG(info.st_mode) || !info.st_nlink) {
        errno = EINVAL;
        goto error;
    }
    (void)fchmod(fd, 0600);
    (void)file_lock_fd(fd, 0, 0);
    if (fstat(fd, &info) != 0)
        goto error;
    if (!info.st_nlink) {
        errno = ENOENT;
        goto error;
    }
    if (append && info.st_size > 0) {
        char last;
        if (pread(fd, &last, 1, info.st_size - 1) != 1 || (last != '\n' && write(fd, "\n", 1) != 1))
            goto error;
    } else if (!append && ftruncate(fd, 0) < 0) {
        goto error;
    }
#endif
    FILE *file = fdopen(fd, append ? "ab" : "wb");
    if (!file)
        goto error;
#ifdef _WIN32
    /* The Windows CRT treats _IOLBF as full buffering; readers must see completed records. */
    setvbuf(file, NULL, _IONBF, 0);
#else
    setvbuf(file, NULL, _IOLBF, 0);
#endif
    return file;
error:
    saved_errno = errno;
    close(fd);
    errno = saved_errno;
    return NULL;
}

int session_storage_open_marker(const char *path)
{
#ifdef _WIN32
    HANDLE handle = open_native(path, GENERIC_READ | GENERIC_WRITE | WRITE_DAC, OPEN_ALWAYS, 1);
    if (handle == INVALID_HANDLE_VALUE)
        return -1;
    int fd = _open_osfhandle((intptr_t)handle, _O_RDWR | _O_BINARY | _O_NOINHERIT);
    if (fd < 0) {
        CloseHandle(handle);
        return -1;
    }
#else
    int fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (fd < 0)
        return -1;
    struct stat info;
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) {
        close(fd);
        errno = EINVAL;
        return -1;
    }
    (void)fchmod(fd, 0600);
#endif
    if (file_lock_fd(fd, 1, 1) < 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    return fd;
}

int session_storage_touch(const char *path)
{
#ifdef _WIN32
    HANDLE handle = open_native(path, GENERIC_READ | FILE_WRITE_ATTRIBUTES, OPEN_EXISTING, 0);
    if (handle == INVALID_HANDLE_VALUE)
        return -1;
    int fd = _open_osfhandle((intptr_t)handle, _O_RDONLY | _O_BINARY | _O_NOINHERIT);
    if (fd < 0) {
        CloseHandle(handle);
        return -1;
    }
    int result = -1;
    if (file_lock_fd(fd, 0, 0) == 0 && native_linked(handle)) {
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        if (SetFileTime(handle, NULL, &now, &now))
            result = 0;
        else
            win_error_set_errno(GetLastError());
    }
#else
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        return -1;
    (void)file_lock_fd(fd, 0, 0);
    struct stat info;
    int result = -1;
    if (fstat(fd, &info) == 0) {
        if (!S_ISREG(info.st_mode))
            errno = EINVAL;
        else if (!info.st_nlink)
            errno = ENOENT;
        else
            result = futimens(fd, NULL);
    }
#endif
    int saved_errno = errno;
    close(fd);
    errno = saved_errno;
    return result;
}

int session_storage_list(const char *directory, session_storage_visit_fn visit, void *userdata)
{
    if (!directory || !visit) {
        errno = EINVAL;
        return -1;
    }
#ifdef _WIN32
    char *pattern = path_join(directory, "*");
    wchar_t *wide = win_utf8_to_wide(pattern);
    free(pattern);
    if (!wide)
        return -1;
    WIN32_FIND_DATAW data;
    HANDLE search = FindFirstFileW(wide, &data);
    free(wide);
    if (search == INVALID_HANDLE_VALUE) {
        win_error_set_errno(GetLastError());
        return -1;
    }
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        char *name = win_utf8_from_wide(data.cFileName);
        if (!name)
            continue;
        char *path = path_join(directory, name);
        int fd = fs_open_regular(path);
        free(path);
        if (fd >= 0) {
            FILETIME modified;
            /* The CRT exposes its native handle as intptr_t. */
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
            HANDLE handle = (HANDLE)_get_osfhandle(fd);
            if (GetFileTime(handle, NULL, NULL, &modified)) {
                uint64_t ticks = ((uint64_t)modified.dwHighDateTime << 32) | modified.dwLowDateTime;
                int64_t seconds = (int64_t)(ticks / UINT64_C(10000000)) - INT64_C(11644473600);
                long nanos = (long)(ticks % UINT64_C(10000000)) * 100;
                visit(name, seconds, nanos, userdata);
            }
            close(fd);
        }
        free(name);
    } while (FindNextFileW(search, &data));
    DWORD error = GetLastError();
    FindClose(search);
    if (error != ERROR_NO_MORE_FILES) {
        win_error_set_errno(error);
        return -1;
    }
#else
    DIR *stream = opendir(directory);
    if (!stream)
        return -1;
    struct dirent *entry;
    int error = 0;
    for (;;) {
        errno = 0;
        entry = readdir(stream);
        if (!entry) {
            error = errno;
            break;
        }
        char *path = path_join(directory, entry->d_name);
        struct stat info;
        if (stat(path, &info) == 0 && S_ISREG(info.st_mode)) {
#ifdef __APPLE__
            long nanos = info.st_mtimespec.tv_nsec;
#else
            long nanos = info.st_mtim.tv_nsec;
#endif
            visit(entry->d_name, (int64_t)info.st_mtime, nanos, userdata);
        }
        free(path);
    }
    closedir(stream);
    if (error) {
        errno = error;
        return -1;
    }
#endif
    return 0;
}
