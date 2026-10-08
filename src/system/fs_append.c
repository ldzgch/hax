/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <fcntl.h>

#include "system/fs.h"
#ifdef _WIN32
#include <windows.h>
#include <stdlib.h>

#include "system/win_error.h"
#include "system/win_security.h"
#include "system/win_utf8.h"
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

int fs_append_private(const char *path, const char *body, size_t body_len)
{
#ifdef _WIN32
    if (body_len > MAXDWORD) {
        errno = EFBIG;
        return -1;
    }
    wchar_t *wide = win_utf8_path_to_wide(path);
    if (!wide)
        return -1;
    struct win_private_security security;
    if (win_private_security_init(&security) < 0) {
        free(wide);
        return -1;
    }
    HANDLE handle =
        CreateFileW(wide, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    &security.attributes, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD error = GetLastError();
    win_private_security_free(&security);
    free(wide);
    if (handle == INVALID_HANDLE_VALUE) {
        win_error_set_errno(error);
        return -1;
    }
    if (GetFileType(handle) != FILE_TYPE_DISK) {
        CloseHandle(handle);
        errno = EINVAL;
        return -1;
    }
    DWORD written = 0;
    int result = WriteFile(handle, body, (DWORD)body_len, &written, NULL);
    error = GetLastError();
    CloseHandle(handle);
    if (!result) {
        win_error_set_errno(error);
        return -1;
    }
    if (written != body_len) {
        errno = EIO;
        return -1;
    }
    return 0;
#else
    int fd = open(path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC | O_NONBLOCK, 0600);
    if (fd < 0)
        return -1;
    struct stat status;
    int inspected = fstat(fd, &status);
    if (inspected < 0 || !S_ISREG(status.st_mode)) {
        int saved_errno = inspected < 0 ? errno : EINVAL;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    ssize_t written;
    do {
        written = write(fd, body, body_len);
    } while (written < 0 && errno == EINTR);
    int saved_errno = written < 0 ? errno : EIO;
    close(fd);
    if (written < 0 || (size_t)written != body_len) {
        errno = saved_errno;
        return -1;
    }
    return 0;
#endif
}
