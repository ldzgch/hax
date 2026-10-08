/* SPDX-License-Identifier: MIT */
#include <windows.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "harness.h"
#include "process.h"
#include "system/fs.h"
#include "system/path.h"
#include "system/win_utf8.h"

static int cleanup_child(const char *mode, const char *report, const char *outside)
{
    const char *root = t_tempdir();
    EXPECT(fs_write_atomic(report, root, strlen(root), 0) == 0);
    char *entry = path_join(root, "\xc3\xa9/\xe6\x96\x87/entry");
    char *directory = path_join(root, "\xc3\xa9/\xe6\x96\x87");
    EXPECT(fs_mkdir_p(directory) == 0);
    free(directory);
    int code = 0;
    if (strcmp(mode, "--symlink-child") == 0) {
        if (t_symlink(outside, entry, 1) < 0) {
            if (errno == EPERM)
                code = 77;
            else
                FAIL("cannot create symlink: %s", strerror(errno));
        }
    } else if (strcmp(mode, "--hardlink-child") == 0) {
        wchar_t *wide_entry = win_utf8_to_wide(entry);
        wchar_t *wide_outside = win_utf8_to_wide(outside);
        EXPECT(CreateHardLinkW(wide_entry, wide_outside, NULL));
        free(wide_outside);
        free(wide_entry);
    } else {
        EXPECT(fs_write_atomic(entry, "readonly", 8, 0) == 0);
        wchar_t *wide = win_utf8_to_wide(entry);
        EXPECT(SetFileAttributesW(wide, FILE_ATTRIBUTE_READONLY));
        free(wide);
    }
    free(entry);
    return t_failures ? 1 : code;
}

static void test_cleanup(const char *program, const char *mode)
{
    const char *parent = t_tempdir();
    char *report = path_join(parent, "child-root.txt");
    char *outside = path_join(parent, "outside");
    const char *target = outside;
    if (strcmp(mode, "--symlink-child") == 0) {
        EXPECT(fs_mkdir_p(outside) == 0);
        target = path_join(outside, "keep");
    }
    EXPECT(fs_write_atomic(target, "keep", 4, 0) == 0);
    wchar_t *wide_target = win_utf8_to_wide(target);
    EXPECT(SetFileAttributesW(wide_target, FILE_ATTRIBUTE_READONLY));
    const char *argv[] = {program, mode, report, outside, NULL};
    struct t_process *child = t_process_start(argv);
    EXPECT(child != NULL);
    int code = child ? t_process_wait(child, 3000) : 1;
    t_process_close(child);
    char *child_root = fs_read_file(report, NULL);
    EXPECT(child_root != NULL);
    if (child_root) {
        EXPECT(fs_entry_exists(child_root) == 0);
        free(child_root);
    }
    EXPECT(fs_entry_exists(parent) == 1);
    DWORD attributes = GetFileAttributesW(wide_target);
    EXPECT(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY));
    char *content = fs_read_file(target, NULL);
    EXPECT(content != NULL);
    if (content)
        EXPECT_STR_EQ(content, "keep");
    free(content);
    free(wide_target);
    if (target != outside)
        free((char *)target);
    free(outside);
    free(report);
    if (code == 77)
        T_SKIP("symlink creation requires Developer Mode or symlink privilege");
    EXPECT(code == 0);
}

int main(int argc, char **argv)
{
    if (argc == 4)
        return cleanup_child(argv[1], argv[2], argv[3]);
    char *program = t_program_path(argv[0]);
    EXPECT(program != NULL);
    if (program) {
        test_cleanup(program, "--readonly-child");
        test_cleanup(program, "--hardlink-child");
        test_cleanup(program, "--symlink-child");
        free(program);
    }
    T_REPORT();
}
