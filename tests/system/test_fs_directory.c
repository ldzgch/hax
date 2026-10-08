/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "harness.h"
#include "system/fs.h"
#include "system/path.h"

struct entries {
    int count;
    int unicode;
    int hidden;
};

static const char UNICODE_NAME[] = "\xe6\x96\x87-\xf0\x9f\x90\xb1";

static void record_entry(const char *name, void *ctx)
{
    struct entries *entries = ctx;
    entries->count++;
    entries->unicode += strcmp(name, UNICODE_NAME) == 0;
    entries->hidden += strcmp(name, ".hidden") == 0;
    /* Callbacks may change errno without changing enumeration's result. */
    errno = EINVAL;
}

static void test_listing(void)
{
    char *dir = path_join(t_tempdir(), UNICODE_NAME);
    EXPECT(fs_mkdir_p(dir) == 0);
    struct entries entries = {0};
    EXPECT(fs_list_directory(dir, record_entry, &entries) == 0);
    EXPECT(entries.count == 0);
    char *unicode = path_join(dir, UNICODE_NAME);
    char *hidden = path_join(dir, ".hidden");
    EXPECT(fs_mkdir_p(unicode) == 0);
    EXPECT(fs_write_atomic(hidden, "", 0, 0) == 0);
    EXPECT(fs_list_directory(dir, record_entry, &entries) == 0);
    EXPECT(entries.count == 2 && entries.unicode == 1 && entries.hidden == 1);
    EXPECT(fs_list_directory(hidden, record_entry, &entries) == -1 && errno == ENOTDIR);
    char *missing = path_join(dir, "missing");
    EXPECT(fs_list_directory(missing, record_entry, &entries) == -1 && errno == ENOENT);
    EXPECT(entries.count == 2);
    free(missing);
    free(hidden);
    free(unicode);
    free(dir);
}

static void test_identity(void)
{
    const char *dir = t_tempdir();
    char *first = path_join(dir, UNICODE_NAME);
    char *second = path_join(dir, "second");
    EXPECT(fs_mkdir_p(first) == 0);
    EXPECT(fs_mkdir_p(second) == 0);
    EXPECT(fs_same_file(first, first) == 1);
    EXPECT(fs_same_file(first, second) == 0);
    char *file = path_join(first, "file");
    EXPECT(fs_write_atomic(file, "body", 4, 0) == 0);
    EXPECT(fs_same_file(file, file) == 1);
    EXPECT(fs_same_file(file, first) == 0);
    char *missing = path_join(dir, "missing");
    EXPECT(fs_same_file(first, missing) == -1 && errno == ENOENT);
    char *alias = path_join(dir, "alias");
    int linked = t_symlink(first, alias, 1) == 0;
    int link_errno = errno;
    if (linked)
        EXPECT(fs_same_file(first, alias) == 1);
    free(alias);
    free(missing);
    free(file);
    free(second);
    free(first);
    if (!linked) {
        if (link_errno == EPERM)
            T_SKIP("symlink creation requires Developer Mode or symlink privilege");
        FAIL("cannot create symlink: %s", strerror(link_errno));
    }
}

int main(void)
{
    test_listing();
    test_identity();
    T_REPORT();
}
