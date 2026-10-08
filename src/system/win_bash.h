/* SPDX-License-Identifier: MIT */
#ifndef HAX_SYSTEM_WIN_BASH_H
#define HAX_SYSTEM_WIN_BASH_H

/* Resolve native Git Bash using git on PATH, Bash on PATH, or standard installation locations.
 * Excludes Windows' WSL launcher. Returns an allocated UTF-8 path, or NULL with errno ENOENT. */
char *win_bash_path(void);

#endif /* HAX_SYSTEM_WIN_BASH_H */
