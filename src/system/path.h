/* SPDX-License-Identifier: MIT */
#ifndef HAX_SYSTEM_PATH_H
#define HAX_SYSTEM_PATH_H

#include <stddef.h>

/* Path transforms do not normalize dot segments. Non-NULL string results are allocated and owned
 * by the caller. path_cwd also queries the filesystem. */

/* Native separators: '/' everywhere, and '\\' on Windows. */
int path_is_separator(char c);

/* Length of a root prefix, or 0 for a relative/invalid path or NULL. Windows recognizes drive and
 * UNC roots, including extended drive and UNC prefixes. A single leading separator is a root
 * relative to the current drive on Windows. */
size_t path_root_length(const char *path);
int path_is_absolute(const char *path);

/* Return an owned current-directory path; Windows converts the native Unicode directory to
 * UTF-8. Return NULL with errno set on failure. */
char *path_cwd(void);

/* Return the allocated home directory, or NULL when unavailable. On Windows use USERPROFILE
 * when HOME is unset or empty. Environment values use UTF-8. */
char *path_home(void);

/* Join non-NULL paths with one separator, trimming trailing slashes from base (except root) and
 * leading slashes from suffix. */
char *path_join(const char *base, const char *suffix);

/* Expand bare `~` and a leading `~/` using non-empty HOME, falling back to USERPROFILE on Windows.
 * Windows also accepts `~\`. Other inputs are copied unchanged; NULL returns NULL. */
char *path_expand_home(const char *path);

/* Replace a leading, component-aligned home directory with `~`, using the same environment lookup
 * as path_expand_home. Other inputs are copied unchanged; NULL returns NULL. */
char *path_collapse_home(const char *path);

/* Return the portion of absolute path lexically beneath absolute cwd. Returns NULL for invalid or
 * unrelated paths, equality, and paths containing a `..` component. */
char *path_relativize(const char *path, const char *cwd);

/* Replace absolute `dir` with its parent in place. Returns 0 at the root, leaving it unchanged. */
int path_climb_to_parent(char *dir);

/* Return an allocated `<base>/hax/<relative_path>`, using the named non-empty XDG base or the HOME
 * fallback. Windows prefers APPDATA for config and LOCALAPPDATA/hax/{state,cache} for state/cache
 * when the XDG base is unset. Return NULL when no base is available. */
char *xdg_hax_config_path(const char *relative_path); /* XDG_CONFIG_HOME or HOME/.config */
char *xdg_hax_state_path(const char *relative_path);  /* XDG_STATE_HOME or HOME/.local/state */
char *xdg_hax_cache_path(const char *relative_path);  /* XDG_CACHE_HOME or HOME/.cache */

#endif /* HAX_SYSTEM_PATH_H */
