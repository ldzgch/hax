/* SPDX-License-Identifier: MIT */
#include "files.h"

#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <io.h>

#include "win_files.h"
#include "system/fs.h"
#include "system/path.h"
#include "system/win_error.h"
#include "system/win_utf8.h"
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

int t_chdir(const char *path)
{
#ifdef _WIN32
    wchar_t *wide = win_utf8_to_wide(path);
    if (!wide)
        return -1;
    int result = _wchdir(wide);
    free(wide);
    return result;
#else
    return chdir(path);
#endif
}

FILE *t_tmpfile(void)
{
#ifdef _WIN32
    char *path = path_join(t_tempdir(), "capture.bin");
    int fd = fs_open_private(path, 1);
    free(path);
    if (fd < 0)
        return NULL;
    FILE *stream = _fdopen(fd, "w+b");
    if (!stream)
        _close(fd);
    return stream;
#else
    return tmpfile();
#endif
}

int t_file_set_mtime(const char *path, int64_t seconds)
{
#ifdef _WIN32
    wchar_t *wide = win_utf8_path_to_wide(path);
    if (!wide)
        return -1;
    HANDLE file = CreateFileW(wide, FILE_WRITE_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                              OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    free(wide);
    if (file == INVALID_HANDLE_VALUE) {
        win_error_set_errno(GetLastError());
        return -1;
    }
    uint64_t ticks = (uint64_t)(seconds + INT64_C(11644473600)) * UINT64_C(10000000);
    FILETIME stamp = {.dwLowDateTime = (DWORD)ticks, .dwHighDateTime = (DWORD)(ticks >> 32)};
    int result = SetFileTime(file, NULL, NULL, &stamp);
    DWORD error = GetLastError();
    CloseHandle(file);
    if (!result)
        win_error_set_errno(error);
    return result ? 0 : -1;
#else
    struct timespec stamps[2] = {{.tv_nsec = UTIME_OMIT}, {.tv_sec = (time_t)seconds}};
    return utimensat(AT_FDCWD, path, stamps, 0);
#endif
}

int t_mkdir(const char *path, unsigned mode)
{
#ifdef _WIN32
    (void)mode;
    wchar_t *wide = win_utf8_path_to_wide(path);
    if (!wide)
        return -1;
    int result = CreateDirectoryW(wide, NULL);
    DWORD error = GetLastError();
    free(wide);
    if (!result)
        win_error_set_errno(error);
    return result ? 0 : -1;
#else
    return mkdir(path, (mode_t)mode);
#endif
}

int t_symlink(const char *target, const char *path, int directory)
{
#ifdef _WIN32
    wchar_t *wide_target = win_utf8_to_wide(target);
    wchar_t *wide_path = win_utf8_path_to_wide(path);
    if (!wide_target || !wide_path) {
        free(wide_target);
        free(wide_path);
        return -1;
    }
    DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE |
                  (directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0);
    int result = CreateSymbolicLinkW(wide_path, wide_target, flags);
    DWORD error = GetLastError();
    free(wide_path);
    free(wide_target);
    if (!result) {
        if (error == ERROR_PRIVILEGE_NOT_HELD)
            errno = EPERM;
        else
            win_error_set_errno(error);
    }
    return result ? 0 : -1;
#else
    (void)directory;
    return symlink(target, path);
#endif
}

#ifdef _WIN32
static HANDLE inspect(const char *path, DWORD flags)
{
    wchar_t *wide = win_utf8_path_to_wide(path);
    if (!wide)
        return INVALID_HANDLE_VALUE;
    HANDLE file = CreateFileW(wide, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                              OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | flags, NULL);
    free(wide);
    return file;
}
#endif

int t_file_is_symlink(const char *path)
{
#ifdef _WIN32
    HANDLE file = inspect(path, FILE_FLAG_OPEN_REPARSE_POINT);
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    FILE_ATTRIBUTE_TAG_INFO info;
    int result = GetFileInformationByHandleEx(file, FileAttributeTagInfo, &info, sizeof(info)) &&
                 info.ReparseTag == IO_REPARSE_TAG_SYMLINK;
    CloseHandle(file);
    return result;
#else
    struct stat info;
    return lstat(path, &info) == 0 && S_ISLNK(info.st_mode);
#endif
}

int t_file_is_regular(const char *path)
{
#ifdef _WIN32
    HANDLE file = inspect(path, 0);
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    BY_HANDLE_FILE_INFORMATION info;
    int result = GetFileType(file) == FILE_TYPE_DISK && GetFileInformationByHandle(file, &info) &&
                 !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    CloseHandle(file);
    return result;
#else
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
#endif
}

void t_expect_private_file(const char *path)
{
#ifdef _WIN32
    t_win_expect_private_acl(path);
#else
    struct stat info;
    EXPECT(stat(path, &info) == 0 && (info.st_mode & 0777) == 0600);
#endif
}
