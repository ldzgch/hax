/* SPDX-License-Identifier: MIT */
#ifndef HAX_SYSTEM_FS_H
#define HAX_SYSTEM_FS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Return 1 if a filesystem entry exists, including a dangling symlink, 0 if absent, or -1
 * with errno for other errors. Inspect the entry without following its symlink target. */
int fs_entry_exists(const char *path);

/* Visit borrowed names in unspecified order, excluding the dot entries ("." and "..").
 * Names are UTF-8 on Windows and raw bytes on POSIX.
 * Return 0 on success, or -1 with errno on failure. */
int fs_list_directory(const char *path, void (*visit)(const char *name, void *ctx), void *ctx);

/* Follow links and compare file identities, including directories.
 * Return 1 for the same object, 0 for different objects, or -1 with errno on failure. */
int fs_same_file(const char *first, const char *second);

/* Create `path` and missing parent directories with mode 0755, subject to the process umask.
 * Existing directories and symlinks to directories are accepted. NULL and empty paths are no-ops.
 * Returns 0 on success and -1 with errno set on failure. */
int fs_mkdir_p(const char *path);

/* Resolve a symlink chain without requiring the final target to exist. Relative targets are
 * resolved from the containing link's directory. Returns an allocated path, or NULL with errno
 * set when resolution fails or exceeds the symlink-hop limit. */
char *fs_resolve_link_target(const char *path);

/* Atomically replace `path` and return an allocated unified diff. Existing file modes are
 * preserved, symlink chains are followed, and missing parent directories are created. Unchanged
 * files retain their inode; changed files are synced before rename.
 * Returns NULL on failure and stores an allocated explanation in `*error`. When non-NULL,
 * `*was_created` reports whether the successful write created a file rather than replacing one. */
char *fs_write_with_diff(const char *path, const char *content, size_t content_len, char **error,
                         int *was_created);

/* Atomically replace `path` with `body`: write to a sibling temporary file and rename it over the
 * destination, resolving symlink chains first so a link is updated through rather than replaced.
 * Missing parent directories are created and the file is private (0600) regardless of the process
 * umask. With `durable`, require file fsync before rename and attempt parent-directory fsync
 * afterward (best-effort; errors are ignored). Returns 0 on success and -1 with errno set on
 * failure; failures before rename leave the destination unchanged. */
int fs_write_atomic(const char *path, const char *body, size_t body_len, int durable);

/* Resolve `name` against PATH and return the first executable regular file as an allocated path.
 * Names containing '/' are checked directly. Empty and relative PATH entries are deliberately
 * ignored so lookup cannot select a program from the process's current directory. */
char *fs_which(const char *name);

/* Return the first whitespace-delimited word of a trusted `sh -c` command line — the program the
 * shell would run — as an allocated string. Returns NULL when that word is absent, or when it
 * contains shell syntax only the shell can evaluate and so cannot be read literally. */
char *fs_shell_head(const char *shell_cmd);

/* Best-effort check that a trusted `sh -c` command line can start: resolve its head like
 * fs_which(). Return 1 when it resolves, or when the head is shell syntax rather than a plain
 * word; 0 otherwise. */
int fs_shell_head_resolves(const char *shell_cmd);

/* Return 0 for a regular file, or -1 with errno set for any other path. */
int fs_check_regular(const char *path);

/* Open a regular file for reading without blocking on special files. The caller owns the returned
 * descriptor. Returns -1 with errno set on failure or when the path is not a regular file. */
int fs_open_regular(const char *path);

/* Report a regular file's byte size without reading it; preserve size on failure.
 * Return -1 with errno, including EINVAL for pipes and EISDIR for directories. */
int fs_file_size(const char *path, uint64_t *size);

/* Report a regular file's modification time in Unix seconds; preserve seconds on failure.
 * Return -1 with errno, including EINVAL for pipes and EISDIR for directories. */
int fs_file_mtime(const char *path, int64_t *seconds);

/* Open a binary read/write file, creating it with private permissions when absent. Existing files
 * retain their permissions and contents. With exclusive, require a new file. The caller owns the
 * non-inheritable descriptor. Returns -1 with errno set on failure. */
int fs_open_private(const char *path, int exclusive);

/* Open a regular file as a binary write stream and truncate its contents. Create absent files
 * privately and preserve existing permissions. The caller owns the non-inheritable stream.
 * Return NULL with errno on failure. */
FILE *fs_fopen_write(const char *path);

/* Append body in one write to a regular file, creating it privately when absent. Reject special
 * files without blocking. Returns 0 for a complete append, or -1 with errno for failure. */
int fs_append_private(const char *path, const char *body, size_t body_len);

/* Read a regular file into an allocated, NUL-terminated buffer. On success, optional `out_len`
 * receives the byte count excluding the terminator. Returns NULL with errno set on failure. */
char *fs_read_file(const char *path, size_t *out_len);

/* Read at most cap bytes from a regular file into an allocated, NUL-terminated buffer. On success,
 * optional outputs report the returned length and whether more data exists. The allocation grows
 * with the bytes read rather than cap. Returns NULL with errno set on failure. */
char *fs_read_file_capped(const char *path, size_t cap, size_t *out_len, int *out_truncated);

#endif /* HAX_SYSTEM_FS_H */
