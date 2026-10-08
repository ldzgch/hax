/* SPDX-License-Identifier: MIT */
#ifndef HAX_SYSTEM_WIN_UTF8_H
#define HAX_SYSTEM_WIN_UTF8_H

#include <stddef.h>

/* Windows API encoding boundary. Inputs are NUL-terminated, outputs are allocated and owned by
 * the caller. Invalid input returns NULL with errno set to EINVAL (NULL) or EILSEQ (encoding).
 * Available only in native Windows builds. */
wchar_t *win_utf8_to_wide(const char *text);
/* Convert a filesystem path to an absolute, extended-length Windows path. Preserve device names
 *
 * so callers can classify them as special files. */
wchar_t *win_utf8_path_to_wide(const char *path);
char *win_utf8_from_wide(const wchar_t *text);

/* Read a native Windows environment variable as allocated UTF-8. Missing or empty variables
 * return NULL with errno cleared. The result is independent of the CRT's ANSI environment. */
char *win_utf8_getenv(const char *name);

/* Same ownership and missing-variable contract, but preserve an explicitly empty value. */
char *win_utf8_getenv_value(const char *name);

#endif /* HAX_SYSTEM_WIN_UTF8_H */
