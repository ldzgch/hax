/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "harness.h"
#include "pipe.h"
#include "system/fs.h"
#include "system/path.h"

static void test_file_size_unicode_and_binary(void)
{
    char *path = path_join(t_tempdir(), "\xe6\x96\x87-\xc3\xa9.bin");
    const char content[] = {'a', '\r', '\n', '\x1a', '\0', 'b'};
    EXPECT(fs_write_atomic(path, content, sizeof(content), 0) == 0);
    uint64_t size = UINT64_MAX;
    EXPECT(fs_file_size(path, &size) == 0);
    EXPECT(size == sizeof(content));
    EXPECT(fs_write_atomic(path, "", 0, 0) == 0);
    EXPECT(fs_file_size(path, &size) == 0);
    EXPECT(size == 0);
    free(path);
}

static void test_file_size_errors_preserve_output(void)
{
    const char *directory = t_tempdir();
    char *missing = path_join(directory, "missing");
    uint64_t size = UINT64_MAX;
    EXPECT(fs_file_size(missing, &size) == -1 && errno == ENOENT);
    EXPECT(size == UINT64_MAX);
    free(missing);
    EXPECT(fs_file_size(directory, &size) == -1 && errno == EISDIR);
    EXPECT(size == UINT64_MAX);
    struct t_pipe *pipe = t_pipe_create();
    EXPECT(pipe != NULL);
    if (!pipe)
        return;
    EXPECT(fs_file_size(t_pipe_path(pipe), &size) == -1 && errno == EINVAL);
    EXPECT(size == UINT64_MAX);
    EXPECT(t_pipe_exists(pipe));
    t_pipe_close(pipe);
}

int main(void)
{
    test_file_size_unicode_and_binary();
    test_file_size_errors_preserve_output();
    T_REPORT();
}
