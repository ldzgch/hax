/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "files.h"
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

static void test_file_mtime_unicode(void)
{
    char *path = path_join(t_tempdir(), "\xe6\x96\x87-\xc3\xa9.bin");
    EXPECT(fs_write_atomic(path, "", 0, 0) == 0);
    const int64_t dates[] = {INT64_C(1700000000), INT64_C(2200000000)};
    for (size_t i = 0; i < sizeof(dates) / sizeof(dates[0]); i++) {
        EXPECT(t_file_set_mtime(path, dates[i]) == 0);
        int64_t modified = -1;
        EXPECT(fs_file_mtime(path, &modified) == 0);
        EXPECT(modified == dates[i]);
    }
    free(path);
}

static void test_file_mtime_errors_preserve_output(void)
{
    const char *directory = t_tempdir();
    char *missing = path_join(directory, "missing");
    int64_t modified = -1;
    EXPECT(fs_file_mtime(missing, &modified) == -1 && errno == ENOENT);
    EXPECT(modified == -1);
    free(missing);
    EXPECT(fs_file_mtime(directory, &modified) == -1 && errno == EISDIR);
    EXPECT(modified == -1);
    struct t_pipe *pipe = t_pipe_create();
    EXPECT(pipe != NULL);
    if (!pipe)
        return;
    EXPECT(fs_file_mtime(t_pipe_path(pipe), &modified) == -1 && errno == EINVAL);
    EXPECT(modified == -1);
    EXPECT(t_pipe_exists(pipe));
    t_pipe_close(pipe);
}

int main(void)
{
    test_file_size_unicode_and_binary();
    test_file_size_errors_preserve_output();
    test_file_mtime_unicode();
    test_file_mtime_errors_preserve_output();
    T_REPORT();
}
