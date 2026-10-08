/* SPDX-License-Identifier: MIT */
#ifndef HAX_TOOLS_BASH_SHELL_H
#define HAX_TOOLS_BASH_SHELL_H

/* Resolve bash.shell, then the platform default. POSIX tries PATH bash, /bin/bash, then /bin/sh;
 * Windows discovers Git Bash and excludes the WSL launcher. Return an owned path, or NULL with
 * errno when Windows has no usable shell. */
char *bash_resolve_shell(void);

#endif /* HAX_TOOLS_BASH_SHELL_H */
