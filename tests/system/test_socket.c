/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdint.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#endif

#include "harness.h"
#include "system/socket.h"

static void expect_noninherit(intptr_t handle)
{
#ifdef _WIN32
    DWORD flags = HANDLE_FLAG_INHERIT;
    EXPECT(GetHandleInformation((HANDLE)handle, &flags));
    EXPECT(!(flags & HANDLE_FLAG_INHERIT));
#else
    EXPECT(fcntl((int)handle, F_GETFD) & FD_CLOEXEC);
#endif
}

static void test_loopback_transfer(void)
{
    intptr_t listener = socket_loopback_listen(SOCKET_IPV4, 0);
    EXPECT(listener != -1);
    if (listener == -1)
        return;
    expect_noninherit(listener);
    int port = socket_bound_port(listener);
    EXPECT(port > 0);
    intptr_t client = socket_loopback_connect(SOCKET_IPV4, port, 2000);
    EXPECT(client != -1);
    if (client == -1)
        goto close_listener;
    expect_noninherit(client);
    uint32_t mask = 0;
    EXPECT(socket_wait_readable(&listener, 1, 2000, &mask) == 1 && mask == 1);
    intptr_t peer = socket_accept(listener);
    EXPECT(peer != -1);
    if (peer == -1)
        goto close_client;
    expect_noninherit(peer);
    EXPECT(socket_set_send_timeout(client, 2000) == 0);
    EXPECT(socket_set_nonblocking(peer, 1) == 0);
    char output[16];
    EXPECT(socket_recv(peer, output, sizeof(output)) == -1 && errno == EWOULDBLOCK);
    EXPECT(socket_set_nonblocking(peer, 0) == 0);
    const char bytes[] = "\r\n\x1a\0\xff";
    EXPECT(socket_send(client, bytes, sizeof(bytes)) == sizeof(bytes));
    intptr_t handles[] = {listener, peer};
    EXPECT(socket_wait_readable(handles, 2, 2000, &mask) == 1 && mask == 2);
    size_t received = 0;
    while (received < sizeof(bytes)) {
        ptrdiff_t count = socket_recv(peer, output + received, sizeof(bytes) - received);
        EXPECT(count > 0);
        if (count <= 0)
            break;
        received += (size_t)count;
    }
    EXPECT_MEM_EQ(output, received, bytes, sizeof(bytes));
    socket_close(peer);
    EXPECT(socket_wait_readable(&client, 1, 2000, &mask) == 1 && mask == 1);
    EXPECT(socket_recv(client, output, sizeof(output)) == 0);
close_client:
    socket_close(client);
close_listener:
    socket_close(listener);
}

int main(void)
{
    int initialized = socket_init();
    EXPECT(initialized == 0);
    if (initialized == 0) {
        test_loopback_transfer();
        EXPECT(socket_loopback_listen(SOCKET_IPV4, -1) == -1 && errno == EINVAL);
        EXPECT(socket_loopback_connect(SOCKET_IPV4, 0, 2000) == -1 && errno == EINVAL);
        uint32_t mask;
        EXPECT(socket_wait_readable(NULL, 0, 0, &mask) == -1 && errno == EINVAL);
        socket_cleanup();
    }
    T_REPORT();
}
