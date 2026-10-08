/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <sys/stat.h>
#endif

#include "harness.h"
#include "session_storage.h"
#include "xalloc.h"
#include "system/file_lock.h"
#include "system/fs.h"
#ifdef _WIN32
#include "win_files.h"
#endif

static void expect_file(const char *path, const char *expected, size_t length)
{
    size_t actual_len = 0;
    char *actual = fs_read_file(path, &actual_len);
    EXPECT(actual != NULL);
    if (actual) {
        EXPECT_MEM_EQ(actual, actual_len, expected, length);
        free(actual);
    }
}

static void test_writer_and_prune_lock(void)
{
    char *path = xasprintf("%s/session-\xc3\xa9.jsonl", t_tempdir());
    errno = 0;
    EXPECT(session_storage_open(path, 1) == NULL && errno == ENOENT);
    FILE *writer = session_storage_open(path, 0);
    EXPECT(writer != NULL);
    if (!writer)
        goto out;
    int prune = fs_open_private(path, 0);
    EXPECT(prune >= 0);
    if (prune >= 0) {
        errno = 0;
        EXPECT(file_lock_fd(prune, 1, 1) == -1 && errno == EWOULDBLOCK);
    }
    const char body[] = "{\"text\":\"\xc3\xa9\"}\n";
    EXPECT(fwrite(body, 1, sizeof(body) - 1, writer) == sizeof(body) - 1);
    expect_file(path, body, sizeof(body) - 1);
    EXPECT(session_storage_touch(path) == 0);
    FILE *second = session_storage_open(path, 1);
    EXPECT(second != NULL);
    if (second) {
        EXPECT(fputs("{}\n", second) >= 0);
        EXPECT(fclose(second) == 0);
    }
    EXPECT(fclose(writer) == 0);
    if (prune >= 0) {
        EXPECT(file_lock_fd(prune, 1, 1) == 0);
        close(prune);
    }
    const char expected[] = "{\"text\":\"\xc3\xa9\"}\n{}\n";
    expect_file(path, expected, sizeof(expected) - 1);
#ifdef _WIN32
    t_win_expect_private_acl(path);
#endif
out:
    free(path);
}

static void test_partial_record_and_truncate(void)
{
    char *path = xasprintf("%s/partial.jsonl", t_tempdir());
    EXPECT(fs_write_atomic(path, "partial", 7, 0) == 0);
    FILE *writer = session_storage_open(path, 1);
    EXPECT(writer != NULL);
    if (writer) {
        EXPECT(fputs("{}\n", writer) >= 0);
        EXPECT(fclose(writer) == 0);
        expect_file(path, "partial\n{}\n", 11);
    }
    writer = session_storage_open(path, 0);
    EXPECT(writer != NULL);
    if (writer) {
        const char binary[] = {'\r', '\n', 0x1a, 0, (char)0xff};
        EXPECT(fwrite(binary, 1, sizeof(binary), writer) == sizeof(binary));
        EXPECT(fclose(writer) == 0);
        expect_file(path, binary, sizeof(binary));
    }
    EXPECT(session_storage_open(t_tempdir(), 0) == NULL);
    EXPECT(session_storage_touch(t_tempdir()) == -1);
    free(path);
}

struct listing_result {
    int count;
    int64_t mtime;
    long mtime_nsec;
};

static void collect_file(const char *name, int64_t mtime, long mtime_nsec, void *userdata)
{
    struct listing_result *result = userdata;
    EXPECT_STR_EQ(name, "session-\xc3\xa9.jsonl");
    result->count++;
    result->mtime = mtime;
    result->mtime_nsec = mtime_nsec;
}

static void test_listing_unicode_and_timestamp(void)
{
    const char *directory = t_tempdir();
    char *path = xasprintf("%s/session-\xc3\xa9.jsonl", directory);
    EXPECT(fs_write_atomic(path, "{}\n", 3, 0) == 0);
    int fd = fs_open_private(path, 0);
    EXPECT(fd >= 0);
    if (fd < 0)
        goto out;
#ifdef _WIN32
    uint64_t ticks = (UINT64_C(2200000000) + UINT64_C(11644473600)) * UINT64_C(10000000) + 1234567;
    FILETIME timestamp = {.dwLowDateTime = (DWORD)ticks, .dwHighDateTime = (DWORD)(ticks >> 32)};
    EXPECT(SetFileTime((HANDLE)_get_osfhandle(fd), NULL, NULL, &timestamp));
#else
    struct timespec times[2] = {{.tv_sec = 2200000000, .tv_nsec = 123456700},
                                {.tv_sec = 2200000000, .tv_nsec = 123456700}};
    EXPECT(futimens(fd, times) == 0);
#endif
    close(fd);
    char *child = xasprintf("%s/not-a-file", directory);
    EXPECT(fs_mkdir_p(child) == 0);
    free(child);
    struct listing_result result = {0};
    EXPECT(session_storage_list(directory, collect_file, &result) == 0);
    EXPECT(result.count == 1);
    EXPECT(result.mtime == INT64_C(2200000000));
    EXPECT(result.mtime_nsec == 123456700);
out:
    free(path);
}

static void test_marker_lock_lifetime(void)
{
    char *path = xasprintf("%s/.prune", t_tempdir());
    int first = session_storage_open_marker(path);
    EXPECT(first >= 0);
    if (first < 0)
        goto out;
    errno = 0;
    EXPECT(session_storage_open_marker(path) == -1 && errno == EWOULDBLOCK);
    EXPECT(write(first, "1", 1) == 1);
    close(first);
    int next = session_storage_open_marker(path);
    EXPECT(next >= 0);
    if (next >= 0)
        close(next);
    expect_file(path, "1", 1);
#ifdef _WIN32
    t_win_expect_private_acl(path);
#endif
    EXPECT(session_storage_open_marker(t_tempdir()) == -1);
out:
    free(path);
}

int main(void)
{
    test_writer_and_prune_lock();
    test_partial_record_and_truncate();
    test_listing_unicode_and_timestamp();
    test_marker_lock_lifetime();
    T_REPORT();
}
