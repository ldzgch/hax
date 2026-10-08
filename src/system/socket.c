/* SPDX-License-Identifier: MIT */
#include "system/socket.h"

#include <errno.h>
#include <limits.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#endif

#ifdef _WIN32
static void socket_error(int error)
{
    switch (error) {
    case WSAEINTR:
        errno = EINTR;
        break;
    case WSAEWOULDBLOCK:
        errno = EWOULDBLOCK;
        break;
    case WSAEADDRINUSE:
        errno = EADDRINUSE;
        break;
    case WSAEADDRNOTAVAIL:
        errno = EADDRNOTAVAIL;
        break;
    case WSAEACCES:
        errno = EACCES;
        break;
    case WSAECONNREFUSED:
        errno = ECONNREFUSED;
        break;
    case WSAECONNRESET:
        errno = ECONNRESET;
        break;
    case WSAECONNABORTED:
        errno = ECONNABORTED;
        break;
    case WSAETIMEDOUT:
        errno = ETIMEDOUT;
        break;
    case WSAEAFNOSUPPORT:
        errno = EAFNOSUPPORT;
        break;
    case WSAENOTSOCK:
        errno = EBADF;
        break;
    case WSAEINVAL:
        errno = EINVAL;
        break;
    case WSAEMFILE:
        errno = EMFILE;
        break;
    case WSAENOBUFS:
        errno = ENOMEM;
        break;
    default:
        errno = EIO;
        break;
    }
}
#endif

int socket_init(void)
{
#ifdef _WIN32
    WSADATA data;
    int error = WSAStartup(MAKEWORD(2, 2), &data);
    if (error) {
        socket_error(error);
        return -1;
    }
#endif
    return 0;
}

void socket_cleanup(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

void socket_close(intptr_t handle)
{
    if (handle == -1)
        return;
#ifdef _WIN32
    closesocket((SOCKET)handle);
#else
    close((int)handle);
#endif
}

static int set_noninherit(intptr_t handle)
{
#ifdef _WIN32
    if (!SetHandleInformation((HANDLE)handle, HANDLE_FLAG_INHERIT, 0)) {
        errno = EIO;
        return -1;
    }
    return 0;
#else
    int flags = fcntl((int)handle, F_GETFD);
    return flags < 0 ? -1 : fcntl((int)handle, F_SETFD, flags | FD_CLOEXEC);
#endif
}

static intptr_t open_socket(enum socket_family family)
{
    if (family != SOCKET_IPV4 && family != SOCKET_IPV6) {
        errno = EINVAL;
        return -1;
    }
    int native_family = family == SOCKET_IPV4 ? AF_INET : AF_INET6;
#ifdef _WIN32
    SOCKET handle =
        WSASocketW(native_family, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_NO_HANDLE_INHERIT);
    if (handle == INVALID_SOCKET) {
        socket_error(WSAGetLastError());
        return -1;
    }
#else
    int handle = socket(native_family, SOCK_STREAM, 0);
    if (handle < 0)
        return -1;
#endif
    if (set_noninherit((intptr_t)handle) < 0) {
        int saved_errno = errno;
        socket_close((intptr_t)handle);
        errno = saved_errno;
        return -1;
    }
    return (intptr_t)handle;
}

static int loopback_address(enum socket_family family, int port, struct sockaddr_storage *storage)
{
    memset(storage, 0, sizeof(*storage));
    if (family == SOCKET_IPV4) {
        struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons((uint16_t)port)};
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        memcpy(storage, &address, sizeof(address));
        return sizeof(address);
    }
    struct sockaddr_in6 address = {.sin6_family = AF_INET6, .sin6_port = htons((uint16_t)port)};
    address.sin6_addr = in6addr_loopback;
    memcpy(storage, &address, sizeof(address));
    return sizeof(address);
}

intptr_t socket_loopback_listen(enum socket_family family, int port)
{
    if (port < 0 || port > 65535) {
        errno = EINVAL;
        return -1;
    }
    intptr_t handle = open_socket(family);
    int saved_errno;
    if (handle == -1)
        return -1;
    int one = 1;
#ifdef _WIN32
    int option = SO_EXCLUSIVEADDRUSE;
#else
    int option = SO_REUSEADDR;
#endif
    if (setsockopt(handle, SOL_SOCKET, option, (const char *)&one, sizeof(one)) != 0)
        goto fail;
    if (family == SOCKET_IPV6 &&
        setsockopt(handle, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&one, sizeof(one)) != 0)
        goto fail;
    struct sockaddr_storage address;
    int size = loopback_address(family, port, &address);
    if (bind(handle, (struct sockaddr *)&address, size) != 0) {
#ifdef _WIN32
        int error = WSAGetLastError();
        /* An existing exclusive bind is reported as access denied by Winsock. */
        socket_error(error == WSAEACCES ? WSAEADDRINUSE : error);
        goto close_handle;
#else
        goto fail;
#endif
    }
    if (listen(handle, 8) != 0)
        goto fail;
    return handle;
fail:
#ifdef _WIN32
    socket_error(WSAGetLastError());
close_handle:
#endif
    saved_errno = errno;
    socket_close(handle);
    errno = saved_errno;
    return -1;
}

intptr_t socket_loopback_connect(enum socket_family family, int port, int timeout_ms)
{
    if (port <= 0 || port > 65535 || timeout_ms <= 0) {
        errno = EINVAL;
        return -1;
    }
    intptr_t handle = open_socket(family);
    int saved_errno;
    if (handle == -1)
        return -1;
    if (socket_set_nonblocking(handle, 1) < 0)
        goto fail;
    struct sockaddr_storage address;
    int size = loopback_address(family, port, &address);
    int connected = connect(handle, (struct sockaddr *)&address, size);
#ifdef _WIN32
    if (connected < 0)
        socket_error(WSAGetLastError());
#endif
    if (connected < 0 && (errno == EINPROGRESS || errno == EWOULDBLOCK)) {
#ifdef _WIN32
        WSAPOLLFD descriptor = {.fd = (SOCKET)handle, .events = POLLOUT};
        int ready = WSAPoll(&descriptor, 1, timeout_ms);
        if (ready < 0)
            socket_error(WSAGetLastError());
        int length = sizeof(int);
#else
        struct pollfd descriptor = {.fd = (int)handle, .events = POLLOUT};
        int ready = poll(&descriptor, 1, timeout_ms);
        socklen_t length = sizeof(int);
#endif
        if (ready == 0)
            errno = ETIMEDOUT;
        if (ready <= 0)
            goto fail;
        int error;
        if (getsockopt(handle, SOL_SOCKET, SO_ERROR, (char *)&error, &length) != 0) {
#ifdef _WIN32
            socket_error(WSAGetLastError());
#endif
            goto fail;
        }
        if (error) {
#ifdef _WIN32
            socket_error(error);
#else
            errno = error;
#endif
            goto fail;
        }
        connected = 0;
    }
    if (connected == 0 && socket_set_nonblocking(handle, 0) == 0)
        return handle;
fail:
    saved_errno = errno;
    socket_close(handle);
    errno = saved_errno;
    return -1;
}

intptr_t socket_accept(intptr_t listener)
{
#ifdef _WIN32
    SOCKET handle = accept((SOCKET)listener, NULL, NULL);
    if (handle == INVALID_SOCKET) {
        socket_error(WSAGetLastError());
        return -1;
    }
#else
    int handle = accept((int)listener, NULL, NULL);
    if (handle < 0)
        return -1;
#endif
    if (set_noninherit((intptr_t)handle) < 0) {
        int saved_errno = errno;
        socket_close((intptr_t)handle);
        errno = saved_errno;
        return -1;
    }
    return (intptr_t)handle;
}

int socket_bound_port(intptr_t handle)
{
    struct sockaddr_storage storage;
#ifdef _WIN32
    int length = sizeof(storage);
#else
    socklen_t length = sizeof(storage);
#endif
    if (getsockname(handle, (struct sockaddr *)&storage, &length) != 0) {
#ifdef _WIN32
        socket_error(WSAGetLastError());
#endif
        return -1;
    }
    if (storage.ss_family == AF_INET)
        return ntohs(((struct sockaddr_in *)&storage)->sin_port);
    if (storage.ss_family == AF_INET6)
        return ntohs(((struct sockaddr_in6 *)&storage)->sin6_port);
    errno = EAFNOSUPPORT;
    return -1;
}

int socket_set_nonblocking(intptr_t handle, int enabled)
{
#ifdef _WIN32
    u_long mode = !!enabled;
    if (ioctlsocket((SOCKET)handle, FIONBIO, &mode) != 0) {
        socket_error(WSAGetLastError());
        return -1;
    }
    return 0;
#else
    int flags = fcntl((int)handle, F_GETFL);
    return flags < 0
               ? -1
               : fcntl((int)handle, F_SETFL, enabled ? flags | O_NONBLOCK : flags & ~O_NONBLOCK);
#endif
}

int socket_set_send_timeout(intptr_t handle, int timeout_ms)
{
#ifdef _WIN32
    DWORD timeout = (DWORD)timeout_ms;
#else
    struct timeval timeout = {.tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000};
#ifdef SO_NOSIGPIPE
    int one = 1;
    if (setsockopt(handle, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)) < 0)
        return -1;
#endif
#endif
    int result =
        setsockopt(handle, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout));
#ifdef _WIN32
    if (result != 0)
        socket_error(WSAGetLastError());
#endif
    return result;
}

int socket_wait_readable(const intptr_t *handles, size_t count, int timeout_ms,
                         uint32_t *ready_mask)
{
    if (!handles || !ready_mask || !count || count > 32) {
        errno = EINVAL;
        return -1;
    }
    *ready_mask = 0;
#ifdef _WIN32
    WSAPOLLFD descriptors[32];
#else
    struct pollfd descriptors[32];
#endif
    memset(descriptors, 0, sizeof(descriptors));
    for (size_t i = 0; i < count; i++) {
        descriptors[i].fd = handles[i];
        descriptors[i].events = POLLIN;
    }
#ifdef _WIN32
    int result = WSAPoll(descriptors, (ULONG)count, timeout_ms);
    if (result < 0)
        socket_error(WSAGetLastError());
#else
    int result = poll(descriptors, (nfds_t)count, timeout_ms);
#endif
    if (result > 0)
        for (size_t i = 0; i < count; i++)
            if (descriptors[i].revents)
                *ready_mask |= UINT32_C(1) << i;
    return result;
}

ptrdiff_t socket_send(intptr_t handle, const void *bytes, size_t length)
{
    int count = length > INT_MAX ? INT_MAX : (int)length;
#ifdef MSG_NOSIGNAL
    int flags = MSG_NOSIGNAL;
#else
    int flags = 0;
#endif
    ptrdiff_t result = send(handle, bytes, count, flags);
#ifdef _WIN32
    if (result < 0)
        socket_error(WSAGetLastError());
#endif
    return result;
}

ptrdiff_t socket_recv(intptr_t handle, void *bytes, size_t length)
{
    int count = length > INT_MAX ? INT_MAX : (int)length;
    ptrdiff_t result = recv(handle, bytes, count, 0);
#ifdef _WIN32
    if (result < 0)
        socket_error(WSAGetLastError());
#endif
    return result;
}
