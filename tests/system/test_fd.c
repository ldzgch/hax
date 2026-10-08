/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "harness.h"
#include "system/fd.h"
#include "system/fs.h"
#include "system/path.h"

static void test_write_all(void)
{
    int pipe_fds[2];
#ifdef _WIN32
    int pipe_result = _pipe(pipe_fds, 4096, _O_BINARY | _O_NOINHERIT);
#else
    int pipe_result = pipe(pipe_fds);
#endif
    EXPECT(pipe_result == 0);
    if (pipe_result != 0)
        return;

    const char data[] = "complete write";
    EXPECT(fd_pipe_wait_readable(pipe_fds[0], 0) == 0);
    EXPECT(fd_pipe_wait_readable(pipe_fds[0], 20) == 0);
    EXPECT(fd_write_all(pipe_fds[1], data, sizeof(data) - 1) == 0);
    EXPECT(fd_pipe_wait_readable(pipe_fds[0], 100) == 1);
    close(pipe_fds[1]);

    char result[sizeof(data)] = {0};
    ssize_t bytes_read = read(pipe_fds[0], result, sizeof(result));
    EXPECT(bytes_read == (ssize_t)sizeof(data) - 1);
    if (bytes_read >= 0)
        EXPECT_MEM_EQ(result, (size_t)bytes_read, data, sizeof(data) - 1);
    EXPECT(fd_pipe_wait_readable(pipe_fds[0], 100) == 1);
    EXPECT(read(pipe_fds[0], result, sizeof(result)) == 0);
    close(pipe_fds[0]);
}

static void test_positioned_reads(void)
{
    char *path = path_join(t_tempdir(), "positioned");
    int fd = fs_open_private(path, 1);
    free(path);
    EXPECT(fd >= 0);
    if (fd < 0)
        return;
    const char bytes[] = {'a', '\r', '\n', 0, (char)0xff, 0x1a, 'z'};
    EXPECT(fd_write_all(fd, bytes, sizeof(bytes)) == 0);
    char actual[sizeof(bytes)] = {0};
    EXPECT(fd_read_at(fd, actual, 4, 1) == 4);
    EXPECT_MEM_EQ(actual, 4, bytes + 1, 4);
    EXPECT(lseek(fd, 0, SEEK_CUR) == sizeof(bytes));
    EXPECT(fd_read_at(fd, actual, sizeof(actual), 5) == 2);
    EXPECT(fd_read_at(fd, actual, sizeof(actual), sizeof(bytes)) == 0);
    EXPECT(fd_read_at(fd, actual, 1, INT64_C(4294967297)) == 0);
    EXPECT(lseek(fd, 0, SEEK_CUR) == sizeof(bytes));
    EXPECT(fd_write_all(fd, "!", 1) == 0);
    EXPECT(fd_read_at(fd, actual, 1, sizeof(bytes)) == 1 && actual[0] == '!');
    errno = 0;
    EXPECT(fd_read_at(fd, actual, 1, -1) == -1 && errno == EINVAL);
    close(fd);
}

int main(void)
{
    test_write_all();
    test_positioned_reads();
    T_REPORT();
}
