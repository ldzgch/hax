/* SPDX-License-Identifier: MIT */
#ifndef HAX_SESSION_STORAGE_H
#define HAX_SESSION_STORAGE_H

#include <stdint.h>
#include <stdio.h>

/* Open a private session writer and retain a shared prune lock until fclose. Append requires an
 * existing file and separates a partial crash record with a newline. Completed newline-delimited
 * records are visible to readers without an explicit flush. Return NULL on failure. */
FILE *session_storage_open(const char *path, int append);

/* Refresh a regular session's timestamp while holding a shared prune lock. Reject final symlinks
 * and deleted files. Return 0 on success, or -1 with errno set. */
int session_storage_touch(const char *path);

/* Open/create a private pruning marker without following its final symlink and acquire an
 * exclusive nonblocking lock. The caller owns the descriptor and its lock until close. */
int session_storage_open_marker(const char *path);

/* Enumerate regular files with Unicode names and full filesystem timestamp precision. Each name
 * is borrowed for the callback duration. Return 0 on success, or -1 with errno on failure. */
typedef void (*session_storage_visit_fn)(const char *name, int64_t mtime, long mtime_nsec,
                                         void *userdata);
int session_storage_list(const char *directory, session_storage_visit_fn visit, void *userdata);

#endif /* HAX_SESSION_STORAGE_H */
