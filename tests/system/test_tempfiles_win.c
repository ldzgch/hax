/* SPDX-License-Identifier: MIT */
#include <windows.h>
#include <errno.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "win_files.h"
#include "xalloc.h"
#include "system/fd.h"
#include "system/fs.h"
#include "system/path.h"
#include "system/tempfiles.h"
#include "system/win_utf8.h"

static void set_temp_base(const char *path)
{
    wchar_t *wide = path ? win_utf8_to_wide(path) : NULL;
    EXPECT(SetEnvironmentVariableW(L"TMPDIR", wide));
    free(wide);
}

static int path_exists(const char *path)
{
    wchar_t *wide = win_utf8_to_wide(path);
    DWORD attributes = GetFileAttributesW(wide);
    free(wide);
    return attributes != INVALID_FILE_ATTRIBUTES;
}

static void test_unicode_binary_private_files_and_cleanup(void)
{
    char *base = path_join(t_tempdir(), "Jos\xc3\xa9");
    EXPECT(fs_mkdir_p(base) == 0);
    set_temp_base(base);
    char *path = NULL;
    int fd = tempfile_create("\xe6\x96\x87-", ".bin", &path);
    EXPECT(fd >= 0 && path != NULL);
    if (fd >= 0 && path) {
        EXPECT(strncmp(path, base, strlen(base)) == 0);
        char *directory = xstrdup(path);
        EXPECT(path_climb_to_parent(directory));
        t_win_expect_private_acl(directory);
        t_win_expect_private_acl(path);
        const char bytes[] = {'\r', '\n', '\0', '\x1a', '\xff'};
        EXPECT(fd_write_all(fd, bytes, sizeof(bytes)) == 0);
        _close(fd);
        t_win_expect_file_bytes(path, bytes, sizeof(bytes));
        tempfiles_cleanup();
        EXPECT(!path_exists(path));
        EXPECT(!path_exists(directory));
        EXPECT(path_exists(base));
        free(directory);
    }
    free(path);
    free(base);
    set_temp_base(NULL);
}

static void test_untracked_file_and_retired_directory(void)
{
    set_temp_base(t_tempdir());
    char *kept = NULL;
    int fd = tempfile_create("kept-", "", &kept);
    EXPECT(fd >= 0 && kept != NULL);
    if (fd < 0 || !kept)
        return;
    _close(fd);
    char *directory = xstrdup(kept);
    EXPECT(path_climb_to_parent(directory));
    tempfile_untrack(kept);
    set_temp_base(t_tempdir());
    char *removed = NULL;
    fd = tempfile_create("removed-", "", &removed);
    EXPECT(fd >= 0 && removed != NULL);
    if (fd >= 0)
        _close(fd);
    tempfiles_cleanup();
    EXPECT(path_exists(kept));
    EXPECT(path_exists(directory));
    if (removed)
        EXPECT(!path_exists(removed));
    wchar_t *wide = win_utf8_to_wide(kept);
    EXPECT(DeleteFileW(wide));
    free(wide);
    tempfiles_cleanup();
    EXPECT(!path_exists(directory));
    free(removed);
    free(directory);
    free(kept);
    set_temp_base(NULL);
}

static void test_cleanup_retries_shared_file(void)
{
    set_temp_base(t_tempdir());
    char *path = NULL;
    int fd = tempfile_create("shared-", "", &path);
    EXPECT(fd >= 0 && path != NULL);
    if (fd < 0 || !path)
        return;
    _close(fd);
    wchar_t *wide = win_utf8_to_wide(path);
    HANDLE reader = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    EXPECT(reader != INVALID_HANDLE_VALUE);
    tempfiles_cleanup();
    EXPECT(path_exists(path));
    if (reader != INVALID_HANDLE_VALUE)
        CloseHandle(reader);
    tempfiles_cleanup();
    EXPECT(!path_exists(path));
    free(wide);
    free(path);
    set_temp_base(NULL);
}

static void test_rejects_windows_filename_syntax(void)
{
    const char *invalid[] = {"../", "..\\", "C:", "*", "?", "\"", "<", ">", "|", "\r", "\xff"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        char *path = (char *)"unchanged";
        EXPECT(tempfile_create(invalid[i], "", &path) == -1);
        EXPECT(errno == EINVAL && path == NULL);
        path = (char *)"unchanged";
        EXPECT(tempfile_create("", invalid[i], &path) == -1);
        EXPECT(errno == EINVAL && path == NULL);
    }
}

static void test_native_temp_fallback(void)
{
    set_temp_base(NULL);
    char *path = NULL;
    int fd = tempfile_create("native-", "", &path);
    EXPECT(fd >= 0 && path != NULL && path_is_absolute(path));
    if (fd >= 0)
        _close(fd);
    tempfiles_cleanup();
    if (path)
        EXPECT(!path_exists(path));
    free(path);
}

int main(void)
{
    test_unicode_binary_private_files_and_cleanup();
    test_untracked_file_and_retired_directory();
    test_cleanup_retries_shared_file();
    test_rejects_windows_filename_syntax();
    test_native_temp_fallback();
    T_REPORT();
}
