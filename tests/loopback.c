/* SPDX-License-Identifier: MIT */
#include "loopback.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "xalloc.h"
#include "system/clock.h"
#include "system/socket.h"

/* Read one request into `request`: the headers, then Content-Length bytes of body. */
static void read_request(intptr_t client_fd, char *request, size_t capacity)
{
    size_t request_len = 0;
    size_t expected_len = 0;
    while (request_len < capacity - 1) {
        ptrdiff_t bytes_read =
            socket_recv(client_fd, request + request_len, capacity - request_len - 1);
        if (bytes_read <= 0)
            break;
        request_len += (size_t)bytes_read;
        request[request_len] = '\0';

        char *header_end = strstr(request, "\r\n\r\n");
        if (header_end && expected_len == 0) {
            const char *length = strstr(request, "Content-Length: ");
            expected_len =
                (size_t)(header_end + 4 - request) + (length ? strtoul(length + 16, NULL, 10) : 0);
        }
        if (expected_len > 0 && request_len >= expected_len)
            break;
    }
}

static void write_all(intptr_t client_fd, const char *text)
{
    size_t len = strlen(text);
    size_t written = 0;
    while (written < len) {
        ptrdiff_t result = socket_send(client_fd, text + written, len - written);
        if (result <= 0)
            break;
        written += (size_t)result;
    }
}

/* Bounded like accept, so a test that never releases fails instead of hanging. */
static void await_release(struct loopback *server)
{
    for (int waited_ms = 0; waited_ms < 10000 && !atomic_load(&server->released); waited_ms++)
        clock_sleep_ms(1);
}

static void *serve_connections(void *user)
{
    struct loopback *server = user;
    int n_requests = server->n_requests > 0 ? server->n_requests : 1;
    for (int i = 0; i < n_requests; i++) {
        uint32_t ready;
        if (socket_wait_readable(&server->listener_fd, 1, 10000, &ready) <= 0)
            return NULL;
        intptr_t client_fd = socket_accept(server->listener_fd);
        if (client_fd < 0)
            return NULL;
        atomic_fetch_add(&server->accepted, 1);

        read_request(client_fd, server->requests[i], sizeof(server->requests[i]));
        if (server->delay_ms > 0)
            clock_sleep_ms(server->delay_ms);
        if (server->hold)
            await_release(server);
        const char *response = server->responses[i] ? server->responses[i] : server->response;
        if (response)
            write_all(client_fd, response);
        socket_close(client_fd);
        atomic_fetch_add(&server->served, 1);
    }
    return NULL;
}

int loopback_listen(struct loopback *server)
{
    server->listener_fd = -1;
    if (socket_init() < 0)
        return -1;
    server->sockets_initialized = 1;
    server->listener_fd = socket_loopback_listen(SOCKET_IPV4, 0);
    if (server->listener_fd < 0)
        goto error;
    int port = socket_bound_port(server->listener_fd);
    if (port > 0)
        return port;
error:
    socket_close(server->listener_fd);
    server->listener_fd = -1;
    socket_cleanup();
    server->sockets_initialized = 0;
    return -1;
}

int loopback_serve(struct loopback *server)
{
    if (pthread_create(&server->thread, NULL, serve_connections, server) != 0) {
        socket_close(server->listener_fd);
        server->listener_fd = -1;
        socket_cleanup();
        server->sockets_initialized = 0;
        return -1;
    }
    server->serving = 1;
    return 0;
}

int loopback_start(struct loopback *server)
{
    int port = loopback_listen(server);
    if (port < 0)
        return -1;
    return loopback_serve(server) == 0 ? port : -1;
}

void loopback_release(struct loopback *server)
{
    atomic_store(&server->released, 1);
}

void loopback_stop(struct loopback *server)
{
    if (server->serving)
        pthread_join(server->thread, NULL);
    server->serving = 0;
    if (server->listener_fd >= 0)
        socket_close(server->listener_fd);
    server->listener_fd = -1;
    if (server->sockets_initialized)
        socket_cleanup();
    server->sockets_initialized = 0;
    for (int i = 0; i < LOOPBACK_MAX_REQUESTS; i++) {
        free(server->owned[i]);
        server->owned[i] = NULL;
    }
}

void loopback_reply_ok(struct loopback *server, int index, const char *body)
{
    free(server->owned[index]);
    server->owned[index] =
        xasprintf("HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
                  strlen(body), body);
    server->responses[index] = server->owned[index];
}
