/* SPDX-License-Identifier: MIT */
#ifndef HAX_TESTS_FILES_H
#define HAX_TESTS_FILES_H

#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include "harness.h"

/* Create one directory, retaining POSIX mode semantics and using UTF-16 paths on Windows. */
int t_mkdir(const char *path, unsigned mode);
/* Change directory using UTF-8 paths on Windows. */
int t_chdir(const char *path);
/* Open a seekable binary scratch stream with a descriptor suitable for dup2. */
FILE *t_tmpfile(void);
/* Set a file's modification time in Unix seconds, preserving its access time. */
int t_file_set_mtime(const char *path, int64_t seconds);
/* Create a file or directory symlink. Return -1 with errno, including EPERM when the native
 * platform requires symlink privileges that this process lacks. */
int t_symlink(const char *target, const char *path, int directory);
int t_file_is_symlink(const char *path);
int t_file_is_regular(const char *path);
/* Assert 0600 on POSIX or the equivalent owner-only protected DACL on Windows. */
void t_expect_private_file(const char *path);

/* Stop a void scenario when creating its link fails; skip only a missing native privilege. */
#define T_SYMLINK(target, path, directory)                                                         \
    do {                                                                                           \
        if (t_symlink(target, path, directory) < 0) {                                              \
            if (errno == EPERM)                                                                    \
                T_SKIP("symlink creation requires Developer Mode or symlink privilege");           \
            FAIL("cannot create symlink: %s", strerror(errno));                                    \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#endif /* HAX_TESTS_FILES_H */
