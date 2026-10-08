/* SPDX-License-Identifier: MIT */
#include <windows.h>
#include <errno.h>
#include <io.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "harness.h"
#include "session_prune_win.h"
#include "session_storage.h"
#include "system/bg_job.h"
#include "system/fs.h"
#include "system/path.h"
#include "system/win_utf8.h"

static const char NAME[] = "2020-01-01T00-00-00Z_12345678-1234-1234-1234-123456789abc.jsonl";

static char *create_session(const char *root, const char *bucket, int old)
{
    char *directory = path_join(root, bucket);
    char *path = path_join(directory, NAME);
    free(directory);
    EXPECT(fs_write_atomic(path, "{}\n", 3, 0) == 0);
    if (old) {
        int fd = fs_open_private(path, 0);
        EXPECT(fd >= 0);
        if (fd >= 0) {
            uint64_t ticks =
                ((uint64_t)time(NULL) + UINT64_C(11644473600) - 3 * 86400) * UINT64_C(10000000);
            FILETIME stamp = {.dwLowDateTime = (DWORD)ticks,
                              .dwHighDateTime = (DWORD)(ticks >> 32)};
            EXPECT(SetFileTime((HANDLE)_get_osfhandle(fd), NULL, NULL, &stamp));
            _close(fd);
        }
    }
    return path;
}

static void test_prune_preserves_active_and_excluded(void)
{
    const char *root = t_tempdir();
    char *old = create_session(root, "old-\xc3\xa9", 1);
    char *fresh = create_session(root, "fresh", 0);
    char *excluded = create_session(root, "excluded", 1);
    char *active = create_session(root, "active", 1);
    FILE *writer = session_storage_open(active, 1);
    EXPECT(writer != NULL);
    char *unrelated = path_join(root, "unrelated/not-a-session.jsonl");
    EXPECT(fs_write_atomic(unrelated, "keep", 4, 0) == 0);
    char *empty = path_join(root, "empty");
    EXPECT(fs_mkdir_p(empty) == 0);
    char *alias = malloc(strlen(excluded) + 1);
    EXPECT(alias != NULL);
    if (!alias)
        goto out;
    strcpy(alias, excluded);
    for (char *cursor = alias; *cursor; cursor++)
        if (*cursor == '/')
            *cursor = '\\';
    EXPECT(session_prune_tree_win(root, time(NULL) - 86400, alias, NULL) == 0);
    EXPECT(fs_check_regular(old) == -1 && errno == ENOENT);
    EXPECT(fs_check_regular(fresh) == 0);
    EXPECT(fs_check_regular(excluded) == 0);
    EXPECT(fs_check_regular(active) == 0);
    EXPECT(fs_check_regular(unrelated) == 0);
    wchar_t *wide_empty = win_utf8_to_wide(empty);
    EXPECT(wide_empty != NULL);
    if (wide_empty) {
        EXPECT(GetFileAttributesW(wide_empty) == INVALID_FILE_ATTRIBUTES);
        free(wide_empty);
    }
    if (writer) {
        EXPECT(fclose(writer) == 0);
        writer = NULL;
        EXPECT(session_prune_tree_win(root, time(NULL) - 86400, NULL, NULL) == 0);
        EXPECT(fs_check_regular(active) == -1 && errno == ENOENT);
        EXPECT(fs_check_regular(excluded) == -1 && errno == ENOENT);
    }
out:
    if (writer)
        fclose(writer);
    free(alias);
    free(empty);
    free(unrelated);
    free(active);
    free(excluded);
    free(fresh);
    free(old);
}

struct cancel_args {
    const char *root;
    int result;
};

static void cancelled_worker(struct bg_job *job, void *userdata)
{
    struct cancel_args *args = userdata;
    bg_job_cancel(job);
    args->result = session_prune_tree_win(args->root, time(NULL), NULL, job);
}

static void test_cancelled_walk(void)
{
    const char *root = t_tempdir();
    char *path = create_session(root, "project", 1);
    struct cancel_args args = {.root = root};
    struct bg_job *job = bg_job_spawn(cancelled_worker, &args);
    EXPECT(job != NULL);
    if (job) {
        bg_job_join(job);
        EXPECT(args.result == -1);
        EXPECT(fs_check_regular(path) == 0);
    }
    free(path);
}

static void test_directory_anchor(void)
{
    const char *directory = t_tempdir();
    HANDLE anchor = session_prune_anchor_win(directory);
    EXPECT(anchor != INVALID_HANDLE_VALUE);
    if (anchor == INVALID_HANDLE_VALUE)
        return;
    wchar_t *wide = win_utf8_to_wide(directory);
    EXPECT(wide != NULL);
    if (wide) {
        HANDLE rename_access =
            CreateFileW(wide, DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        EXPECT(rename_access == INVALID_HANDLE_VALUE);
        if (rename_access != INVALID_HANDLE_VALUE)
            CloseHandle(rename_access);
        else
            EXPECT(GetLastError() == ERROR_SHARING_VIOLATION);
        free(wide);
    }
    char *marker = path_join(directory, ".prune");
    int fd = session_storage_open_marker(marker);
    EXPECT(fd >= 0);
    if (fd >= 0)
        _close(fd);
    free(marker);
    CloseHandle(anchor);
}

int main(void)
{
    test_prune_preserves_active_and_excluded();
    test_cancelled_walk();
    test_directory_anchor();
    T_REPORT();
}
