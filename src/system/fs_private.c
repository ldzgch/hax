/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "system/fs.h"
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <stdint.h>
#include <stdlib.h>

#include "system/win_error.h"
#include "system/win_security.h"
#include "system/win_utf8.h"
#else
#include <sys/stat.h>
#endif

int fs_open_private(const char *path, int exclusive)
{
#ifdef _WIN32
    wchar_t *wide = win_utf8_path_to_wide(path);
    if (!wide)
        return -1;
    struct win_private_security security;
    if (win_private_security_init(&security) < 0) {
        free(wide);
        return -1;
    }
    HANDLE handle = CreateFileW(
        wide, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        &security.attributes, exclusive ? CREATE_NEW : OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD error = GetLastError();
    int created = handle != INVALID_HANDLE_VALUE && (exclusive || error != ERROR_ALREADY_EXISTS);
    win_private_security_free(&security);
    if (handle == INVALID_HANDLE_VALUE) {
        free(wide);
        win_error_set_errno(error);
        return -1;
    }
    if (GetFileType(handle) != FILE_TYPE_DISK) {
        CloseHandle(handle);
        free(wide);
        errno = EINVAL;
        return -1;
    }
    int fd = _open_osfhandle((intptr_t)handle, _O_RDWR | _O_BINARY | _O_NOINHERIT);
    if (fd < 0) {
        int saved_errno = errno;
        CloseHandle(handle);
        if (created)
            DeleteFileW(wide);
        errno = saved_errno;
    }
    free(wide);
    return fd;
#else
    return open(path, O_CREAT | O_RDWR | O_CLOEXEC | O_NONBLOCK | (exclusive ? O_EXCL : 0), 0600);
#endif
}

FILE *fs_fopen_write(const char *path)
{
    int saved_errno;
    int fd = fs_open_private(path, 0);
    if (fd < 0)
        return NULL;
#ifndef _WIN32
    struct stat info;
    if (fstat(fd, &info) < 0)
        goto error;
    if (!S_ISREG(info.st_mode)) {
        errno = EINVAL;
        goto error;
    }
#endif
    if (ftruncate(fd, 0) < 0)
        goto error;
    FILE *stream = fdopen(fd, "wb");
    if (stream)
        return stream;
error:
    saved_errno = errno;
    close(fd);
    errno = saved_errno;
    return NULL;
}
