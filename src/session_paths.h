/* SPDX-License-Identifier: MIT */
#ifndef HAX_SESSION_PATHS_H
#define HAX_SESSION_PATHS_H

/* Return an owned UUID from a canonical session basename, or NULL for unrelated names. Native
 * Windows separators are accepted. */
char *session_id_from_path(const char *path);

/* True only for hax's complete timestamp-and-UUID session basename. */
int session_path_is_standard(const char *path);

#endif /* HAX_SESSION_PATHS_H */
