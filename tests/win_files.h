/* SPDX-License-Identifier: MIT */
#ifndef HAX_TESTS_WIN_FILES_H
#define HAX_TESTS_WIN_FILES_H

#include <stddef.h>

/* Assert file bytes using native APIs, independently of fs_read_file. */
void t_win_expect_file_bytes(const char *path, const void *expected, size_t length);

/* Assert an owner-only, non-inherited protected DACL on a file or directory. */
void t_win_expect_private_acl(const char *path);

#endif /* HAX_TESTS_WIN_FILES_H */
