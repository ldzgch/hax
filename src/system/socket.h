/* SPDX-License-Identifier: MIT */
#ifndef HAX_SYSTEM_SOCKET_H
#define HAX_SYSTEM_SOCKET_H

#include <stddef.h>
#include <stdint.h>

enum socket_family {
    SOCKET_IPV4,
    SOCKET_IPV6,
};

/* Balance each successful initialization with cleanup after closing all owned sockets. POSIX
 * implementations are no-ops; Windows owns one Winsock startup reference. */
int socket_init(void);
void socket_cleanup(void);

/* TCP loopback sockets are non-inheritable. Handles retain native width; -1 means failure.
 * Listen binds exclusively against active listeners. IPv6 sockets accept IPv6 only. */
intptr_t socket_loopback_listen(enum socket_family family, int port);
intptr_t socket_loopback_connect(enum socket_family family, int port, int timeout_ms);
intptr_t socket_accept(intptr_t listener);
int socket_bound_port(intptr_t handle);
void socket_close(intptr_t handle);
int socket_set_nonblocking(intptr_t handle, int enabled);
int socket_set_send_timeout(intptr_t handle, int timeout_ms);

/* Wait for 1..32 sockets. Readable/closed/error sockets set corresponding bits in ready_mask.
 * Return count, zero on timeout, or -1 with errno. Negative timeout waits indefinitely. */
int socket_wait_readable(const intptr_t *handles, size_t count, int timeout_ms,
                         uint32_t *ready_mask);

/* Transfer at most INT_MAX bytes, returning the byte count or -1 with errno. Sends suppress
 * SIGPIPE where supported. Receives return zero at EOF. */
ptrdiff_t socket_send(intptr_t handle, const void *bytes, size_t length);
ptrdiff_t socket_recv(intptr_t handle, void *bytes, size_t length);

#endif /* HAX_SYSTEM_SOCKET_H */
