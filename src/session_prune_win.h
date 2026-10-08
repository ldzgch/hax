/* SPDX-License-Identifier: MIT */
#ifndef HAX_SESSION_PRUNE_WIN_H
#define HAX_SESSION_PRUNE_WIN_H

#include <windows.h>
#include <time.h>

struct bg_job;

/* Pin a non-reparse directory against rename and reparse changes. Return an owned handle or
 * INVALID_HANDLE_VALUE when unavailable. */
HANDLE session_prune_anchor_win(const char *directory);

/* Best-effort two-level sweep, anchored by directory handles. Active writers and the excluded
 * file are preserved. Return -1 on cancellation, otherwise 0. */
int session_prune_tree_win(const char *directory, time_t cutoff, const char *exclude_path,
                           struct bg_job *job);

#endif /* HAX_SESSION_PRUNE_WIN_H */
