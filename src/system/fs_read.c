/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include "system/fs.h"
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

#include "buf.h"

int fs_file_size(const char *path, uint64_t *size)
{
    int fd = fs_open_regular(path);
    if (fd < 0)
        return -1;
#ifdef _WIN32
    long long length = _filelengthi64(fd);
    int result = length < 0 ? -1 : 0;
#else
    struct stat info;
    int result = fstat(fd, &info);
    off_t length = result == 0 ? info.st_size : 0;
#endif
    int saved_errno = errno;
#ifdef _WIN32
    _close(fd);
#else
    close(fd);
#endif
    if (result < 0) {
        errno = saved_errno;
        return -1;
    }
    *size = (uint64_t)length;
    return 0;
}

static ptrdiff_t read_retry(int fd, void *data, size_t length)
{
    ptrdiff_t bytes_read;
    do {
#ifdef _WIN32
        bytes_read = _read(fd, data, (unsigned)length);
#else
        bytes_read = read(fd, data, length);
#endif
    } while (bytes_read < 0 && errno == EINTR);
    return bytes_read;
}

char *fs_read_file(const char *path, size_t *out_len)
{
    return fs_read_file_capped(path, SIZE_MAX, out_len, NULL);
}

char *fs_read_file_capped(const char *path, size_t cap, size_t *out_len, int *out_truncated)
{
    int saved_errno;
    int truncated = 0;
    int fd = fs_open_regular(path);
    if (fd < 0)
        return NULL;

    struct buf contents;
    buf_init(&contents);
    char chunk[8192];
    while (contents.len < cap) {
        size_t remaining = cap - contents.len;
        size_t request = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        ptrdiff_t bytes_read = read_retry(fd, chunk, request);
        if (bytes_read < 0)
            goto error;
        if (bytes_read == 0)
            break;
        buf_append(&contents, chunk, (size_t)bytes_read);
    }

    if (contents.len == cap) {
        char extra;
        ptrdiff_t bytes_read = read_retry(fd, &extra, 1);
        if (bytes_read < 0)
            goto error;
        truncated = bytes_read > 0;
    }

#ifdef _WIN32
    _close(fd);
#else
    close(fd);
#endif
    if (out_len)
        *out_len = contents.len;
    if (out_truncated)
        *out_truncated = truncated;
    return buf_steal(&contents);

error:
    saved_errno = errno;
    buf_free(&contents);
#ifdef _WIN32
    _close(fd);
#else
    close(fd);
#endif
    errno = saved_errno;
    return NULL;
}
