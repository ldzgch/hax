/* SPDX-License-Identifier: MIT */
#include <windows.h>
#include <errno.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>

#include "env.h"
#include "files.h"
#include "harness.h"
#include "win_files.h"
#include "xalloc.h"
#include "system/fs.h"
#include "system/path.h"
#include "system/win_utf8.h"

static void test_atomic_unicode_binary_and_private_acl(void)
{
    char *path = path_join(t_tempdir(), "Jos\xc3\xa9/\xe6\x96\x87.bin");
    const char first[] = {'a', '\r', '\n', '\0', '\x1a', '\xff'};
    EXPECT(fs_write_atomic(path, first, sizeof(first), 1) == 0);
    t_win_expect_file_bytes(path, first, sizeof(first));
    t_win_expect_private_acl(path);
    const char second[] = {'b', '\0', '\r', '\n'};
    EXPECT(fs_write_atomic(path, second, sizeof(second), 0) == 0);
    t_win_expect_file_bytes(path, second, sizeof(second));
    t_win_expect_private_acl(path);
    size_t length = 0;
    char *bytes = fs_read_file(path, &length);
    EXPECT(bytes != NULL);
    if (bytes) {
        EXPECT_MEM_EQ(bytes, length, second, sizeof(second));
        EXPECT(bytes[length] == 0);
    }
    free(bytes);
    free(path);
}

static void test_capped_reads(void)
{
    char *path = path_join(t_tempdir(), "capped");
    EXPECT(fs_write_atomic(path, "abc\r\ndef", 8, 0) == 0);
    const size_t caps[] = {0, 3, 8, 100};
    for (size_t i = 0; i < sizeof(caps) / sizeof(*caps); i++) {
        size_t length = 99;
        int truncated = -1;
        char *bytes = fs_read_file_capped(path, caps[i], &length, &truncated);
        EXPECT(bytes != NULL);
        if (bytes) {
            size_t expected = caps[i] < 8 ? caps[i] : 8;
            EXPECT_MEM_EQ(bytes, length, "abc\r\ndef", expected);
            EXPECT(truncated == (caps[i] < 8));
            EXPECT(bytes[length] == 0);
        }
        free(bytes);
    }
    free(path);
}

static void test_special_files_and_directories(void)
{
    const char *directory = t_tempdir();
    EXPECT(fs_open_regular(directory) == -1);
    EXPECT(errno == EISDIR);
    EXPECT(fs_open_regular("NUL") == -1);
    EXPECT(errno == EINVAL);
    EXPECT(fs_write_atomic(directory, "x", 1, 0) == -1);
    EXPECT(errno == EISDIR);
    char *file = path_join(directory, "file");
    EXPECT(fs_write_atomic(file, "x", 1, 0) == 0);
    char *child = path_join(file, "child");
    EXPECT(fs_mkdir_p(child) == -1);
    EXPECT(errno == ENOTDIR);
    free(child);
    free(file);
}

static BY_HANDLE_FILE_INFORMATION file_identity(const char *path)
{
    BY_HANDLE_FILE_INFORMATION identity = {0};
    wchar_t *wide = win_utf8_to_wide(path);
    HANDLE file = CreateFileW(wide, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                              OPEN_EXISTING, 0, NULL);
    EXPECT(file != INVALID_HANDLE_VALUE);
    if (file != INVALID_HANDLE_VALUE) {
        EXPECT(GetFileInformationByHandle(file, &identity));
        CloseHandle(file);
    }
    free(wide);
    return identity;
}

static void test_diff_and_unchanged_identity(void)
{
    char *path = path_join(t_tempdir(), "new/sub/file");
    char *error = NULL;
    int created = 0;
    char *diff = fs_write_with_diff(path, "first\n", 6, &error, &created);
    EXPECT(diff != NULL && error == NULL && created);
    EXPECT(diff && strstr(diff, "+first") != NULL);
    free(diff);
    free(error);
    t_win_expect_file_bytes(path, "first\n", 6);
    BY_HANDLE_FILE_INFORMATION before = file_identity(path);
    diff = fs_write_with_diff(path, "first\n", 6, &error, &created);
    EXPECT(diff != NULL && error == NULL && !created);
    if (diff)
        EXPECT_STR_EQ(diff, "");
    free(diff);
    free(error);
    BY_HANDLE_FILE_INFORMATION after = file_identity(path);
    EXPECT(before.dwVolumeSerialNumber == after.dwVolumeSerialNumber);
    EXPECT(before.nFileIndexHigh == after.nFileIndexHigh);
    EXPECT(before.nFileIndexLow == after.nFileIndexLow);
    diff = fs_write_with_diff(path, "second\n", 7, &error, &created);
    EXPECT(diff != NULL && error == NULL && !created);
    EXPECT(diff && strstr(diff, "-first") && strstr(diff, "+second"));
    free(diff);
    free(error);
    t_win_expect_file_bytes(path, "second\n", 7);
    free(path);
}

static void test_existing_acl_preserved_by_diff(void)
{
    char *path = path_join(t_tempdir(), "private");
    EXPECT(fs_write_atomic(path, "first\n", 6, 0) == 0);
    char *error = NULL;
    char *diff = fs_write_with_diff(path, "second\n", 7, &error, NULL);
    EXPECT(diff != NULL && error == NULL);
    free(diff);
    free(error);
    t_win_expect_private_acl(path);
    free(path);
}

static void test_readonly_failure_preserves_destination(void)
{
    const char *directory = t_tempdir();
    char *path = path_join(directory, "readonly");
    EXPECT(fs_write_atomic(path, "original", 8, 0) == 0);
    wchar_t *wide = win_utf8_to_wide(path);
    EXPECT(SetFileAttributesW(wide, FILE_ATTRIBUTE_READONLY));
    EXPECT(fs_write_atomic(path, "replacement", 11, 1) == -1);
    t_win_expect_file_bytes(path, "original", 8);
    char *error = NULL;
    EXPECT(fs_write_with_diff(path, "replacement", 11, &error, NULL) == NULL);
    EXPECT(error != NULL);
    free(error);
    t_win_expect_file_bytes(path, "original", 8);
    char *pattern = path_join(directory, ".hax-write-*");
    wchar_t *wide_pattern = win_utf8_to_wide(pattern);
    WIN32_FIND_DATAW entry;
    HANDLE listing = FindFirstFileW(wide_pattern, &entry);
    EXPECT(listing == INVALID_HANDLE_VALUE);
    if (listing != INVALID_HANDLE_VALUE)
        FindClose(listing);
    free(wide_pattern);
    free(pattern);
    EXPECT(SetFileAttributesW(wide, FILE_ATTRIBUTE_NORMAL));
    free(wide);
    free(path);
}

static void test_symlink_and_dangling_target(void)
{
    const char *directory = t_tempdir();
    char *link = path_join(directory, "link");
    char *target = path_join(directory, "target");
    wchar_t *wide = win_utf8_to_wide(link);
    if (t_symlink("target", link, 0) < 0) {
        EXPECT(errno == EPERM);
        free(wide);
        free(link);
        free(target);
        T_SKIP("Windows symlink creation requires Developer Mode or symlink privilege");
    }
    EXPECT(fs_entry_exists(link) == 1);
    EXPECT(fs_entry_exists(target) == 0);
    EXPECT(fs_write_atomic(link, "through link", 12, 1) == 0);
    t_win_expect_file_bytes(target, "through link", 12);
    EXPECT(GetFileAttributesW(wide) & FILE_ATTRIBUTE_REPARSE_POINT);
    char *error = NULL;
    char *diff = fs_write_with_diff(link, "updated", 7, &error, NULL);
    EXPECT(diff != NULL && error == NULL);
    free(diff);
    free(error);
    t_win_expect_file_bytes(target, "updated", 7);
    EXPECT(GetFileAttributesW(wide) & FILE_ATTRIBUTE_REPARSE_POINT);
    EXPECT(t_file_is_symlink(link));
    EXPECT(t_file_is_regular(link));
    char *directory_link = path_join(directory, "directory-link");
    EXPECT(t_symlink(directory, directory_link, 1) == 0);
    EXPECT(t_file_is_symlink(directory_link));
    EXPECT(!t_file_is_regular(directory_link));
    free(directory_link);
    free(wide);
    free(link);
    free(target);
}

static void test_absolute_link_chain_and_cycle(void)
{
    const char *directory = t_tempdir();
    char *first = path_join(directory, "first");
    char *second = path_join(directory, "second");
    char *target = path_join(directory, "target");
    wchar_t *wide_second = win_utf8_to_wide(second);
    int supported = t_symlink("second", first, 0) == 0;
    if (!supported)
        EXPECT(errno == EPERM);
    if (supported) {
        EXPECT(t_symlink(target, second, 0) == 0);
        EXPECT(fs_write_atomic(first, "chain", 5, 1) == 0);
        t_win_expect_file_bytes(target, "chain", 5);
        EXPECT(DeleteFileW(wide_second));
        EXPECT(t_symlink("first", second, 0) == 0);
        errno = 0;
        EXPECT(fs_resolve_link_target(first) == NULL);
        EXPECT(errno == ELOOP);
    }
    free(wide_second);
    free(target);
    free(second);
    free(first);
    if (!supported)
        T_SKIP("Windows symlink creation requires Developer Mode or symlink privilege");
}

static void test_which_windows_path_and_pathext(void)
{
    char *program = path_join(t_tempdir(), "my program.CMD");
    EXPECT(fs_write_atomic(program, "echo test", 9, 0) == 0);
    char *saved = t_path_replace(program);
    char *directory = xstrdup(program);
    EXPECT(path_climb_to_parent(directory));
    t_env_set("PATH", directory);
    t_env_set("PATHEXT", ".EXE;.CMD");
    char *found = fs_which("my program");
    EXPECT(found != NULL);
    if (found)
        EXPECT(_stricmp(found, program) == 0);
    free(found);
    found = fs_which(program);
    EXPECT(found != NULL);
    if (found)
        EXPECT_STR_EQ(found, program);
    free(found);
    char *shadow = xasprintf("%s.EXE", program);
    EXPECT(fs_write_atomic(shadow, "shadow", 6, 0) == 0);
    found = fs_which(program);
    EXPECT(found != NULL);
    if (found)
        EXPECT_STR_EQ(found, program);
    free(found);
    free(shadow);
    t_env_set("PATH", ";.;C:relative;\\rootrelative");
    EXPECT(fs_which("my program") == NULL);
    EXPECT(fs_which("") == NULL);
    EXPECT(fs_which(NULL) == NULL);
    t_path_restore(saved);
    free(directory);
    free(program);
}

static void test_private_open(void)
{
    char *path = path_join(t_tempdir(), "private-\xc3\xa9");
    int fd = fs_open_private(path, 1);
    EXPECT(fd >= 0);
    if (fd < 0) {
        free(path);
        return;
    }
    DWORD flags = HANDLE_FLAG_INHERIT;
    /* The CRT exposes the native handle as an integer. */
    /* NOLINTNEXTLINE(performance-no-int-to-ptr) */
    EXPECT(GetHandleInformation((HANDLE)_get_osfhandle(fd), &flags));
    EXPECT(!(flags & HANDLE_FLAG_INHERIT));
    const char bytes[] = "\r\n\x1a\0";
    EXPECT(_write(fd, bytes, sizeof(bytes)) == (int)sizeof(bytes));
    _close(fd);
    t_win_expect_private_acl(path);
    t_win_expect_file_bytes(path, bytes, sizeof(bytes));
    errno = 0;
    EXPECT(fs_open_private(path, 1) == -1 && errno == EEXIST);
    fd = fs_open_private(path, 0);
    EXPECT(fd >= 0);
    if (fd >= 0)
        _close(fd);
    t_win_expect_file_bytes(path, bytes, sizeof(bytes));
    EXPECT(fs_open_private("NUL", 0) == -1);
    free(path);
}

static void test_private_append(void)
{
    char *directory = t_tempdir();
    char *path = path_join(directory, "append-\xc3\xa9");
    const char bytes[] = "\r\n\x1a\0";
    EXPECT(fs_append_private(path, bytes, sizeof(bytes)) == 0);
    t_win_expect_private_acl(path);
    EXPECT(fs_append_private(path, "tail", 4) == 0);
    char expected[sizeof(bytes) + 4];
    memcpy(expected, bytes, sizeof(bytes));
    memcpy(expected + sizeof(bytes), "tail", 4);
    t_win_expect_file_bytes(path, expected, sizeof(expected));
    EXPECT(fs_append_private("NUL", bytes, sizeof(bytes)) == -1);
    EXPECT(fs_append_private(directory, bytes, sizeof(bytes)) == -1);
    free(path);
}

static void test_entry_exists(void)
{
    const char *directory = t_tempdir();
    char *path = path_join(directory, "entry-\xc3\xa9");
    EXPECT(fs_entry_exists(directory) == 1);
    EXPECT(fs_entry_exists(path) == 0);
    EXPECT(fs_write_atomic(path, "present", 7, 1) == 0);
    EXPECT(fs_entry_exists(path) == 1);
    char *child = path_join(path, "child");
    EXPECT(fs_entry_exists(child) == 0);
    EXPECT(fs_entry_exists("\xff") == -1);
    free(child);
    free(path);
}

static void test_write_stream_unicode_truncation_and_binary(void)
{
    char *path = path_join(t_tempdir(), "trace-\xc3\xa9.txt");
    FILE *stream = fs_fopen_write(path);
    EXPECT(stream != NULL);
    if (!stream)
        goto out;
    DWORD flags = HANDLE_FLAG_INHERIT;
    /* The CRT exposes the native handle as an integer. */
    /* NOLINTNEXTLINE(performance-no-int-to-ptr) */
    EXPECT(GetHandleInformation((HANDLE)_get_osfhandle(_fileno(stream)), &flags));
    EXPECT(!(flags & HANDLE_FLAG_INHERIT));
    const char bytes[] = {'a', '\r', '\n', '\0', '\x1a'};
    EXPECT(fwrite(bytes, 1, sizeof(bytes), stream) == sizeof(bytes));
    EXPECT(fclose(stream) == 0);
    t_win_expect_file_bytes(path, bytes, sizeof(bytes));
    t_win_expect_private_acl(path);
    stream = fs_fopen_write(path);
    EXPECT(stream != NULL);
    if (stream) {
        EXPECT(fputs("new", stream) >= 0);
        EXPECT(fclose(stream) == 0);
        t_win_expect_file_bytes(path, "new", 3);
    }
    EXPECT(fs_fopen_write(t_tempdir()) == NULL);
    EXPECT(fs_fopen_write("NUL") == NULL);
out:
    free(path);
}

int main(void)
{
    test_write_stream_unicode_truncation_and_binary();
    test_entry_exists();
    test_private_open();
    test_private_append();
    test_atomic_unicode_binary_and_private_acl();
    test_capped_reads();
    test_special_files_and_directories();
    test_diff_and_unchanged_identity();
    test_existing_acl_preserved_by_diff();
    test_readonly_failure_preserves_destination();
    test_symlink_and_dangling_target();
    test_absolute_link_chain_and_cycle();
    test_which_windows_path_and_pathext();
    T_REPORT();
}
