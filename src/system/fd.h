/* SPDX-License-Identifier: MIT */
#ifndef HAX_SYSTEM_FD_H
#define HAX_SYSTEM_FD_H

#include <stddef.h>
#include <stdint.h>

/* Write exactly length bytes, retrying interrupted and short writes. Returns 0 on success or -1
 * with errno set. */
int fd_write_all(int fd, const void *data, size_t length);

/* Read regular-file bytes at an absolute offset without changing the descriptor's position.
 * Return the byte count, zero at EOF, or -1 with errno. Native Windows uses an independent
 * overlapped handle so concurrent writes through the original descriptor retain their position. */
ptrdiff_t fd_read_at(int fd, void *data, size_t length, int64_t offset);

/* Wait for pipe data, EOF, or an error that the next read can observe. Return 1 when ready, zero
 * on timeout, or -1 with errno. Zero timeout probes; negative timeout waits indefinitely. */
int fd_pipe_wait_readable(int fd, int timeout_ms);

#endif /* HAX_SYSTEM_FD_H */
