/* SPDX-License-Identifier: MIT */
/* A raw socket client stands in for libcurl, which never splits a small request across writes. */
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "loopback.h"
#include "system/clock.h"
#include "system/socket.h"

#define REPLY_A "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nConnection: close\r\n\r\nA"
#define REPLY_B "HTTP/1.1 404 Not Found\r\nContent-Length: 1\r\nConnection: close\r\n\r\nB"

static intptr_t connect_loopback(int port)
{
    return socket_loopback_connect(SOCKET_IPV4, port, 3000);
}

static void send_text(intptr_t fd, const char *text)
{
    size_t len = strlen(text);
    size_t written = 0;
    while (written < len) {
        ptrdiff_t result = socket_send(fd, text + written, len - written);
        if (result <= 0)
            break;
        written += (size_t)result;
    }
}

/* Read until the server closes the connection; a reply that never comes fails the test after three
 * seconds instead of hanging it. */
static void read_reply(intptr_t fd, char *reply, size_t capacity)
{
    size_t len = 0;
    while (len < capacity - 1) {
        uint32_t ready;
        if (socket_wait_readable(&fd, 1, 3000, &ready) <= 0)
            break;
        ptrdiff_t bytes_read = socket_recv(fd, reply + len, capacity - len - 1);
        if (bytes_read <= 0)
            break;
        len += (size_t)bytes_read;
    }
    reply[len] = '\0';
}

static void pause_briefly(void)
{
    clock_sleep_ms(20);
}

static void test_body_split_across_writes(void)
{
    struct loopback server = {.response = REPLY_A};
    int port = loopback_start(&server);
    EXPECT(port > 0);
    if (port <= 0)
        return;
    intptr_t fd = connect_loopback(port);
    EXPECT(fd >= 0);
    send_text(fd, "POST /x HTTP/1.1\r\nHost: t\r\nContent-Length: 11\r\n\r\n");
    pause_briefly();
    send_text(fd, "hello");
    pause_briefly();
    send_text(fd, " world");
    char reply[256];
    read_reply(fd, reply, sizeof(reply));
    socket_close(fd);
    loopback_stop(&server);

    EXPECT_STR_EQ(reply, REPLY_A);
    EXPECT(strstr(server.requests[0], "\r\n\r\nhello world") != NULL);
    EXPECT(atomic_load(&server.accepted) == 1);
    EXPECT(atomic_load(&server.served) == 1);
}

static void test_bodiless_request_answered_at_header_end(void)
{
    struct loopback server = {.response = REPLY_A};
    int port = loopback_start(&server);
    EXPECT(port > 0);
    if (port <= 0)
        return;
    intptr_t fd = connect_loopback(port);
    EXPECT(fd >= 0);
    send_text(fd, "GET /x HTTP/1.1\r\nHost: t\r\n\r\n");
    char reply[256];
    read_reply(fd, reply, sizeof(reply));
    socket_close(fd);
    loopback_stop(&server);

    EXPECT_STR_EQ(reply, REPLY_A);
    EXPECT(strncmp(server.requests[0], "GET /x HTTP/1.1\r\n", 17) == 0);
}

static void test_scripted_replies_per_connection(void)
{
    struct loopback server = {.response = REPLY_A, .n_requests = 3};
    server.responses[1] = REPLY_B;
    loopback_reply_ok(&server, 2, "{}");
    int port = loopback_start(&server);
    EXPECT(port > 0);
    if (port <= 0)
        return;

    static const char *const EXPECTED[] = {
        REPLY_A, REPLY_B, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}"};
    for (int i = 0; i < 3; i++) {
        intptr_t fd = connect_loopback(port);
        EXPECT(fd >= 0);
        char request[64];
        snprintf(request, sizeof(request), "GET /%d HTTP/1.1\r\nHost: t\r\n\r\n", i);
        send_text(fd, request);
        char reply[256];
        read_reply(fd, reply, sizeof(reply));
        socket_close(fd);
        EXPECT_STR_EQ(reply, EXPECTED[i]);
    }
    loopback_stop(&server);

    EXPECT(strncmp(server.requests[0], "GET /0 ", 7) == 0);
    EXPECT(strncmp(server.requests[1], "GET /1 ", 7) == 0);
    EXPECT(strncmp(server.requests[2], "GET /2 ", 7) == 0);
    EXPECT(atomic_load(&server.served) == 3);
}

static void test_hold_replies_after_release(void)
{
    struct loopback server = {.response = REPLY_A, .hold = 1};
    int port = loopback_start(&server);
    EXPECT(port > 0);
    if (port <= 0)
        return;
    intptr_t fd = connect_loopback(port);
    EXPECT(fd >= 0);
    send_text(fd, "GET /x HTTP/1.1\r\nHost: t\r\n\r\n");
    uint32_t ready;
    EXPECT(socket_wait_readable(&fd, 1, 50, &ready) == 0);

    loopback_release(&server);
    char reply[256];
    read_reply(fd, reply, sizeof(reply));
    socket_close(fd);
    loopback_stop(&server);
    EXPECT_STR_EQ(reply, REPLY_A);
}

static void test_stopping_one_server_preserves_another(void)
{
    struct loopback first = {0};
    struct loopback second = {.response = REPLY_B};
    int first_port = loopback_listen(&first);
    int second_port = loopback_start(&second);
    EXPECT(first_port > 0);
    EXPECT(second_port > 0);
    loopback_stop(&first);
    loopback_stop(&first);
    if (second_port <= 0)
        return;

    intptr_t fd = connect_loopback(second_port);
    EXPECT(fd >= 0);
    send_text(fd, "GET /x HTTP/1.1\r\nHost: t\r\n\r\n");
    char reply[256];
    read_reply(fd, reply, sizeof(reply));
    socket_close(fd);
    loopback_stop(&second);
    EXPECT_STR_EQ(reply, REPLY_B);
}

int main(void)
{
    test_body_split_across_writes();
    test_bodiless_request_answered_at_header_end();
    test_scripted_replies_per_connection();
    test_hold_replies_after_release();
    test_stopping_one_server_preserves_another();
    T_REPORT();
}
