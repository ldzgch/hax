/* SPDX-License-Identifier: MIT */
#include <windows.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <winioctl.h>

#include "xalloc.h"
#include "system/fs.h"
#include "system/path.h"
#include "system/rand.h"
#include "system/win_error.h"
#include "system/win_security.h"
#include "system/win_utf8.h"
#include "text/diff.h"

int fs_list_directory(const char *path, void (*visit)(const char *name, void *ctx), void *ctx)
{
    DWORD attributes;
    wchar_t *directory = win_utf8_path_to_wide(path);
    if (!directory)
        return -1;
    attributes = GetFileAttributesW(directory);
    DWORD error = GetLastError();
    free(directory);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        win_error_set_errno(error);
        return -1;
    }
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        errno = ENOTDIR;
        return -1;
    }
    char *pattern = path_join(path, "*");
    wchar_t *native = win_utf8_path_to_wide(pattern);
    free(pattern);
    if (!native)
        return -1;
    WIN32_FIND_DATAW entry;
    HANDLE search = FindFirstFileW(native, &entry);
    error = GetLastError();
    free(native);
    if (search == INVALID_HANDLE_VALUE) {
        if (error == ERROR_FILE_NOT_FOUND)
            return 0;
        win_error_set_errno(error);
        return -1;
    }
    do {
        if (wcscmp(entry.cFileName, L".") == 0 || wcscmp(entry.cFileName, L"..") == 0)
            continue;
        char *name = win_utf8_from_wide(entry.cFileName);
        if (!name) {
            int saved_errno = errno;
            FindClose(search);
            errno = saved_errno;
            return -1;
        }
        visit(name, ctx);
        free(name);
    } while (FindNextFileW(search, &entry));
    error = GetLastError();
    FindClose(search);
    if (error == ERROR_NO_MORE_FILES)
        return 0;
    win_error_set_errno(error);
    return -1;
}

static int file_identity(const char *path, BY_HANDLE_FILE_INFORMATION *info)
{
    wchar_t *native = win_utf8_path_to_wide(path);
    if (!native)
        return -1;
    HANDLE file = CreateFileW(native, FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                              OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    DWORD error = GetLastError();
    free(native);
    if (file == INVALID_HANDLE_VALUE) {
        win_error_set_errno(error);
        return -1;
    }
    int result = GetFileInformationByHandle(file, info);
    error = GetLastError();
    CloseHandle(file);
    if (!result)
        win_error_set_errno(error);
    return result ? 0 : -1;
}

int fs_same_file(const char *first, const char *second)
{
    BY_HANDLE_FILE_INFORMATION left, right;
    if (file_identity(first, &left) < 0 || file_identity(second, &right) < 0)
        return -1;
    return left.dwVolumeSerialNumber == right.dwVolumeSerialNumber &&
           left.nFileIndexHigh == right.nFileIndexHigh && left.nFileIndexLow == right.nFileIndexLow;
}

int fs_entry_exists(const char *path)
{
    wchar_t *native = win_utf8_path_to_wide(path);
    if (!native)
        return -1;
    DWORD attributes = GetFileAttributesW(native);
    DWORD error = GetLastError();
    free(native);
    if (attributes != INVALID_FILE_ATTRIBUTES)
        return 1;
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        return 0;
    win_error_set_errno(error);
    return -1;
}

static char *parent_dir(const char *path)
{
    char *parent = xstrdup(path);
    if (path_climb_to_parent(parent))
        return parent;
    free(parent);
    return xstrdup(".");
}

static int mkdir_one(const char *path)
{
    wchar_t *wide = win_utf8_path_to_wide(path);
    if (!wide)
        return -1;
    int result = 0;
    if (!CreateDirectoryW(wide, NULL)) {
        DWORD error = GetLastError();
        DWORD attributes = GetFileAttributesW(wide);
        if (error != ERROR_ALREADY_EXISTS || attributes == INVALID_FILE_ATTRIBUTES ||
            !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            win_error_set_errno(error);
            if (error == ERROR_ALREADY_EXISTS)
                errno = ENOTDIR;
            result = -1;
        }
    }
    free(wide);
    return result;
}

int fs_mkdir_p(const char *path)
{
    if (!path || !*path)
        return 0;
    char *copy = xstrdup(path);
    size_t root = path_root_length(copy);
    size_t length = strlen(copy);
    while (length > root && path_is_separator(copy[length - 1]))
        copy[--length] = 0;
    int result = -1;
    for (size_t i = root; i < length; i++) {
        if (!path_is_separator(copy[i]) || !i)
            continue;
        char separator = copy[i];
        copy[i] = 0;
        result = mkdir_one(copy);
        copy[i] = separator;
        if (result < 0)
            goto out;
    }
    result = mkdir_one(copy);
out:
    free(copy);
    return result;
}

/* User-mode layout of the symlink and mount-point payloads returned by FSCTL_GET_REPARSE_POINT.
 * The SDK exposes the corresponding structure only through its kernel-mode headers. */
struct reparse_data {
    ULONG tag;
    USHORT length;
    USHORT reserved;
    union {
        struct {
            USHORT substitute_offset;
            USHORT substitute_length;
            USHORT print_offset;
            USHORT print_length;
            ULONG flags;
            wchar_t path[1];
        } symlink;
        struct {
            USHORT substitute_offset;
            USHORT substitute_length;
            USHORT print_offset;
            USHORT print_length;
            wchar_t path[1];
        } mount;
    } u;
};

static char *reparse_target(HANDLE handle, int *relative, int *is_link)
{
    struct reparse_data *data = xmalloc(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
    DWORD count;
    char *result = NULL;
    if (!DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT, NULL, 0, data,
                         MAXIMUM_REPARSE_DATA_BUFFER_SIZE, &count, NULL)) {
        win_error_set_errno(GetLastError());
        goto out;
    }
    size_t base;
    size_t offset;
    size_t length;
    if (data->tag == IO_REPARSE_TAG_SYMLINK) {
        base = offsetof(struct reparse_data, u.symlink.path);
        offset = data->u.symlink.substitute_offset;
        length = data->u.symlink.substitute_length;
        *relative = !!(data->u.symlink.flags & SYMLINK_FLAG_RELATIVE);
    } else if (data->tag == IO_REPARSE_TAG_MOUNT_POINT) {
        base = offsetof(struct reparse_data, u.mount.path);
        offset = data->u.mount.substitute_offset;
        length = data->u.mount.substitute_length;
        *relative = 0;
    } else {
        *is_link = 0;
        goto out;
    }
    *is_link = 1;
    if (base > count || offset > count - base || length > count - base - offset ||
        offset % sizeof(wchar_t) || length % sizeof(wchar_t)) {
        errno = EINVAL;
        goto out;
    }
    wchar_t *target = xcalloc(length / sizeof(wchar_t) + 1, sizeof(*target));
    memcpy(target, (char *)data + base + offset, length);
    if (!*relative && wcsncmp(target, L"\\??\\UNC\\", 8) == 0) {
        target[6] = L'\\';
        result = win_utf8_from_wide(target + 6);
    } else {
        result = win_utf8_from_wide(!*relative && wcsncmp(target, L"\\??\\", 4) == 0 ? target + 4
                                                                                     : target);
    }
    free(target);
out:
    free(data);
    return result;
}

char *fs_resolve_link_target(const char *path)
{
    char *current = xstrdup(path);
    for (int hop = 0; hop < 32; hop++) {
        wchar_t *wide = win_utf8_path_to_wide(current);
        if (!wide)
            goto error;
        HANDLE handle = CreateFileW(
            wide, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
        free(wide);
        if (handle == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
                return current;
            win_error_set_errno(error);
            goto error;
        }
        if (GetFileType(handle) != FILE_TYPE_DISK) {
            CloseHandle(handle);
            errno = EINVAL;
            goto error;
        }
        BY_HANDLE_FILE_INFORMATION information;
        if (!GetFileInformationByHandle(handle, &information)) {
            DWORD error = GetLastError();
            CloseHandle(handle);
            win_error_set_errno(error);
            goto error;
        }
        if (!(information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            CloseHandle(handle);
            return current;
        }
        int relative = 0;
        int is_link = 1;
        char *target = reparse_target(handle, &relative, &is_link);
        CloseHandle(handle);
        if (!is_link)
            return current;
        if (!target)
            goto error;
        if (relative) {
            char *parent = parent_dir(current);
            char *joined = path_join(parent, target);
            free(parent);
            free(target);
            target = joined;
        }
        free(current);
        current = target;
    }
    errno = ELOOP;
error:
    free(current);
    return NULL;
}

int fs_open_regular(const char *path)
{
    wchar_t *wide = win_utf8_path_to_wide(path);
    if (!wide)
        return -1;
    HANDLE handle =
        CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    free(wide);
    if (handle == INVALID_HANDLE_VALUE) {
        win_error_set_errno(GetLastError());
        return -1;
    }
    BY_HANDLE_FILE_INFORMATION information;
    if (GetFileType(handle) != FILE_TYPE_DISK) {
        CloseHandle(handle);
        errno = EINVAL;
        return -1;
    }
    if (!GetFileInformationByHandle(handle, &information)) {
        DWORD error = GetLastError();
        CloseHandle(handle);
        win_error_set_errno(error);
        return -1;
    }
    if (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        CloseHandle(handle);
        errno = EISDIR;
        return -1;
    }
    int fd = _open_osfhandle((intptr_t)handle, _O_RDONLY | _O_BINARY | _O_NOINHERIT);
    if (fd < 0)
        CloseHandle(handle);
    return fd;
}

int fs_check_regular(const char *path)
{
    int fd = fs_open_regular(path);
    if (fd < 0)
        return -1;
    _close(fd);
    return 0;
}

static wchar_t *stage_write(const char *directory, const char *content, size_t length, int private,
                            int durable)
{
    int saved_errno;
    struct win_private_security security;
    if (private && win_private_security_init(&security) < 0)
        return NULL;
    wchar_t *wide = NULL;
    HANDLE handle = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 100; attempt++) {
        char uuid[37];
        gen_uuid_v4(uuid);
        char *path = xasprintf("%s/.hax-write-%s", directory, uuid);
        wide = win_utf8_path_to_wide(path);
        free(path);
        if (!wide)
            break;
        handle = CreateFileW(wide, GENERIC_WRITE, 0, private ? &security.attributes : NULL,
                             CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (handle != INVALID_HANDLE_VALUE)
            break;
        DWORD error = GetLastError();
        free(wide);
        wide = NULL;
        win_error_set_errno(error);
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
            break;
    }
    if (private)
        win_private_security_free(&security);
    if (handle == INVALID_HANDLE_VALUE)
        return NULL;
    size_t offset = 0;
    while (offset < length) {
        DWORD request = length - offset > INT_MAX ? INT_MAX : (DWORD)(length - offset);
        DWORD written;
        if (!WriteFile(handle, content + offset, request, &written, NULL)) {
            win_error_set_errno(GetLastError());
            goto error;
        }
        if (!written) {
            errno = EIO;
            goto error;
        }
        offset += written;
    }
    if (durable && !FlushFileBuffers(handle)) {
        win_error_set_errno(GetLastError());
        goto error;
    }
    if (!CloseHandle(handle)) {
        win_error_set_errno(GetLastError());
        handle = INVALID_HANDLE_VALUE;
        goto error;
    }
    return wide;
error:
    saved_errno = errno;
    if (handle != INVALID_HANDLE_VALUE)
        CloseHandle(handle);
    DeleteFileW(wide);
    free(wide);
    errno = saved_errno;
    return NULL;
}

int fs_write_atomic(const char *path, const char *body, size_t body_len, int durable)
{
    int saved_errno;
    char *destination = fs_resolve_link_target(path);
    if (!destination)
        return -1;
    wchar_t *wide = win_utf8_path_to_wide(destination);
    char *directory = parent_dir(destination);
    wchar_t *staged = NULL;
    int result = -1;
    if (!wide || fs_mkdir_p(directory) < 0)
        goto out;
    if (fs_check_regular(destination) < 0 && errno != ENOENT)
        goto out;
    staged = stage_write(directory, body, body_len, 1, durable);
    if (!staged)
        goto out;
    DWORD flags = MOVEFILE_REPLACE_EXISTING | (durable ? MOVEFILE_WRITE_THROUGH : 0);
    if (!MoveFileExW(staged, wide, flags)) {
        win_error_set_errno(GetLastError());
        goto out;
    }
    result = 0;
out:
    saved_errno = errno;
    if (result < 0 && staged)
        DeleteFileW(staged);
    free(staged);
    free(directory);
    free(wide);
    free(destination);
    errno = saved_errno;
    return result;
}

char *fs_write_with_diff(const char *path, const char *content, size_t content_len, char **error,
                         int *was_created)
{
    *error = NULL;
    if (was_created)
        *was_created = 0;
    char *destination = fs_resolve_link_target(path);
    char *old_content = NULL;
    char *directory = NULL;
    char *diff = NULL;
    wchar_t *wide = NULL;
    wchar_t *staged = NULL;
    int existed = 0;
    if (!destination)
        goto error;
    size_t old_len = 0;
    old_content = fs_read_file(destination, &old_len);
    existed = old_content != NULL;
    if (!old_content && errno != ENOENT)
        goto error;
    int absolute = path_is_absolute(path);
    char *old_label = !existed   ? xstrdup("/dev/null")
                      : absolute ? xstrdup(path)
                                 : xasprintf("a/%s", path);
    char *new_label = absolute ? xstrdup(path) : xasprintf("b/%s", path);
    diff = make_unified_diff(existed ? old_content : "", old_len, content, content_len, old_label,
                             new_label);
    if (!existed && !*diff) {
        free(diff);
        diff = xasprintf("--- /dev/null\n+++ %s\n", new_label);
    }
    free(new_label);
    free(old_label);
    if (existed && !*diff)
        goto out;
    directory = parent_dir(destination);
    wide = win_utf8_path_to_wide(destination);
    if (!wide || fs_mkdir_p(directory) < 0)
        goto error;
    staged = stage_write(directory, content, content_len, 0, 1);
    if (!staged)
        goto error;
    /* ReplaceFile preserves the destination's ACL and metadata. Private atomic writes instead
     * move their protected temporary file so the replacement receives the new private ACL. */
    if (!(existed ? ReplaceFileW(wide, staged, NULL, 0, NULL, NULL)
                  : MoveFileExW(staged, wide, MOVEFILE_WRITE_THROUGH))) {
        win_error_set_errno(GetLastError());
        goto error;
    }
    if (was_created)
        *was_created = !existed;
    goto out;
error:
    if (errno == EINVAL || errno == EISDIR)
        *error = xasprintf("%s exists but is not a regular file", path);
    else
        *error = xasprintf("writing %s: %s", path, strerror(errno));
    if (staged)
        DeleteFileW(staged);
    free(diff);
    diff = NULL;
out:
    free(staged);
    free(wide);
    free(directory);
    free(old_content);
    free(destination);
    return diff;
}

static int has_executable_extension(const char *name, const char *extensions)
{
    size_t name_len = strlen(name);
    for (const char *extension = extensions; *extension;) {
        const char *end = strchr(extension, ';');
        size_t count = end ? (size_t)(end - extension) : strlen(extension);
        if (count > 1 && extension[0] == '.' && name_len >= count &&
            _strnicmp(name + name_len - count, extension, count) == 0)
            return 1;
        if (!end)
            break;
        extension = end + 1;
    }
    return 0;
}

static char *executable_candidate(const char *name, const char *extensions)
{
    if (has_executable_extension(name, extensions))
        return fs_check_regular(name) == 0 ? xstrdup(name) : NULL;
    const char *extension = extensions;
    while (*extension) {
        const char *end = strchr(extension, ';');
        size_t count = end ? (size_t)(end - extension) : strlen(extension);
        if (count > 1 && extension[0] == '.' && strcspn(extension, "/\\:") >= count) {
            char *candidate = xasprintf("%s%.*s", name, (int)count, extension);
            if (fs_check_regular(candidate) == 0)
                return candidate;
            free(candidate);
        }
        if (!end)
            break;
        extension = end + 1;
    }
    return NULL;
}

char *fs_which(const char *name)
{
    if (!name || !*name)
        return NULL;
    char *extensions = win_utf8_getenv("PATHEXT");
    if (!extensions)
        extensions = xstrdup(".COM;.EXE;.BAT;.CMD");
    char *result = NULL;
    if (strpbrk(name, "/\\") || strchr(name, ':')) {
        result = executable_candidate(name, extensions);
        free(extensions);
        return result;
    }
    char *path = win_utf8_getenv("PATH");
    for (const char *entry = path; entry && *entry;) {
        const char *end = strchr(entry, ';');
        size_t count = end ? (size_t)(end - entry) : strlen(entry);
        if (count > 1 && entry[0] == '"' && entry[count - 1] == '"') {
            entry++;
            count -= 2;
        }
        char *directory = xmalloc(count + 1);
        memcpy(directory, entry, count);
        directory[count] = 0;
        /* Root-relative and drive-relative entries depend on the caller's current drive/cwd. */
        if (path_root_length(directory) > 1) {
            char *candidate = path_join(directory, name);
            result = executable_candidate(candidate, extensions);
            free(candidate);
        }
        free(directory);
        if (result || !end)
            break;
        entry = end + 1;
    }
    free(path);
    free(extensions);
    return result;
}
