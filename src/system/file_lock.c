/* SPDX-License-Identifier: MIT */
#include "system/file_lock.h"

#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <stdint.h>

#include "system/win_error.h"
#else
#include <sys/file.h>
#endif

int file_lock_fd(int fd, int exclusive, int nonblocking)
{
#ifdef _WIN32
    intptr_t native = _get_osfhandle(fd);
    if (native == -1)
        return -1;
    DWORD flags =
        (exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0) | (nonblocking ? LOCKFILE_FAIL_IMMEDIATELY : 0);
    /* POSIX flock is advisory. Lock a reserved byte beyond usable file data so Windows' mandatory
     * shared locks still permit cooperating session writers to append. */
    OVERLAPPED position = {.Offset = MAXDWORD, .OffsetHigh = INT32_MAX};
    if (LockFileEx((HANDLE)native, flags, 0, 1, 0, &position))
        return 0;
    DWORD error = GetLastError();
    if (error == ERROR_LOCK_VIOLATION)
        errno = EWOULDBLOCK;
    else
        win_error_set_errno(error);
    return -1;
#else
    int operation = (exclusive ? LOCK_EX : LOCK_SH) | (nonblocking ? LOCK_NB : 0);
    int result;
    do {
        result = flock(fd, operation);
    } while (result < 0 && errno == EINTR);
    return result;
#endif
}

int file_unlock_fd(int fd)
{
#ifdef _WIN32
    intptr_t native = _get_osfhandle(fd);
    if (native == -1)
        return -1;
    OVERLAPPED position = {.Offset = MAXDWORD, .OffsetHigh = INT32_MAX};
    if (UnlockFileEx((HANDLE)native, 0, 1, 0, &position))
        return 0;
    win_error_set_errno(GetLastError());
    return -1;
#else
    int result;
    do {
        result = flock(fd, LOCK_UN);
    } while (result < 0 && errno == EINTR);
    return result;
#endif
}
