/* SPDX-License-Identifier: MIT */
#ifndef HAX_TESTS_ENV_H
#define HAX_TESTS_ENV_H

/* Set or unset a process environment variable, aborting on failure. Test binaries isolate these
 * mutations from other tests. */
void t_env_set(const char *name, const char *value);
void t_env_unset(const char *name);

#endif /* HAX_TESTS_ENV_H */
