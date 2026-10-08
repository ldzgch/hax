/* SPDX-License-Identifier: MIT */
#include "system/fd.h"

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>

#include "system/win_error.h"
#else
#include <poll.h>
#include <unistd.h>
#endif

int fd_write_all(int fd, const void *data, size_t length)
{
    const char *cursor = data;
    while (length > 0) {
#ifdef _WIN32
        unsigned request = length > INT_MAX ? INT_MAX : (unsigned)length;
        int written = _write(fd, cursor, request);
#else
        size_t request = length > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : length;
        ssize_t written = write(fd, cursor, request);
#endif
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (written == 0) {
            errno = EIO;
            return -1;
        }
        cursor += written;
        length -= (size_t)written;
    }
    return 0;
}

ptrdiff_t fd_read_at(int fd, void *data, size_t length, int64_t offset)
{
    if (offset < 0 || (!data && length)) {
        errno = EINVAL;
        return -1;
    }
#ifdef _WIN32
    intptr_t native = _get_osfhandle(fd);
    if (native == -1)
        return -1;
    HANDLE reader =
        ReOpenFile((HANDLE)native, GENERIC_READ,
                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_FLAG_OVERLAPPED);
    if (reader == INVALID_HANDLE_VALUE) {
        win_error_set_errno(GetLastError());
        return -1;
    }
    OVERLAPPED position = {.Offset = (DWORD)offset, .OffsetHigh = (DWORD)((uint64_t)offset >> 32)};
    DWORD count = 0;
    DWORD request = length > INT_MAX ? INT_MAX : (DWORD)length;
    int success = ReadFile(reader, data, request, &count, &position);
    DWORD error = success ? ERROR_SUCCESS : GetLastError();
    if (error == ERROR_IO_PENDING) {
        success = GetOverlappedResult(reader, &position, &count, TRUE);
        error = success ? ERROR_SUCCESS : GetLastError();
    }
    CloseHandle(reader);
    if (error == ERROR_HANDLE_EOF)
        return 0;
    if (!success) {
        win_error_set_errno(error);
        return -1;
    }
    return (ptrdiff_t)count;
#else
    if ((int64_t)(off_t)offset != offset) {
        errno = EOVERFLOW;
        return -1;
    }
    size_t request = length > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : length;
    return pread(fd, data, request, (off_t)offset);
#endif
}

int fd_pipe_wait_readable(int fd, int timeout_ms)
{
#ifdef _WIN32
    intptr_t native = _get_osfhandle(fd);
    if (native == -1)
        return -1;
    HANDLE pipe = (HANDLE)native;
    if (GetFileType(pipe) != FILE_TYPE_PIPE) {
        errno = EINVAL;
        return -1;
    }
    ULONGLONG started = GetTickCount64();
    for (;;) {
        DWORD available;
        if (!PeekNamedPipe(pipe, NULL, 0, NULL, &available, NULL)) {
            DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED)
                return 1;
            win_error_set_errno(error);
            return -1;
        }
        if (available)
            return 1;
        ULONGLONG elapsed = GetTickCount64() - started;
        if (timeout_ms >= 0 && elapsed >= (ULONGLONG)timeout_ms)
            return 0;
        DWORD pause = 10;
        if (timeout_ms >= 0 && (ULONGLONG)timeout_ms - elapsed < pause)
            pause = (DWORD)((ULONGLONG)timeout_ms - elapsed);
        Sleep(pause);
    }
#else
    struct pollfd descriptor = {.fd = fd, .events = POLLIN};
    return poll(&descriptor, 1, timeout_ms);
#endif
}
