/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "xalloc.h"
#include "system/clock.h"
#include "system/socket.h"
#include "system/spawn.h"
#include "tools/bash_fixtures.h"

struct gate {
    struct gate *next;
    char *path;
    intptr_t listener;
};

static struct gate *gates;

static void gates_cleanup(void)
{
    while (gates) {
        struct gate *gate = gates;
        gates = gate->next;
        socket_close(gate->listener);
        socket_cleanup();
        free(gate->path);
        free(gate);
    }
}

char *gate_create(void)
{
    if (socket_init() < 0)
        abort();
    intptr_t listener = socket_loopback_listen(SOCKET_IPV4, 0);
    if (listener == -1) {
        socket_cleanup();
        abort();
    }
    int port = socket_bound_port(listener);
    if (port <= 0) {
        socket_close(listener);
        socket_cleanup();
        abort();
    }
    static int cleanup_registered;
    if (!cleanup_registered) {
        atexit(gates_cleanup);
        cleanup_registered = 1;
    }
    struct gate *gate = xcalloc(1, sizeof(*gate));
    gate->listener = listener;
    /* Bash treats this redirection as a TCP connection and blocks read until release. */
    gate->path = xasprintf("/dev/tcp/127.0.0.1/%d", port);
    gate->next = gates;
    gates = gate;
    return xstrdup(gate->path);
}

void gate_release(const char *path)
{
    struct gate *gate = gates;
    while (gate && strcmp(path, gate->path) != 0)
        gate = gate->next;
    EXPECT(gate != NULL);
    if (!gate)
        return;
    uint32_t ready;
    int result = socket_wait_readable(&gate->listener, 1, 10000, &ready);
    EXPECT(result > 0);
    if (result <= 0)
        return;
    intptr_t client = socket_accept(gate->listener);
    EXPECT(client != -1);
    if (client == -1)
        return;
    EXPECT(socket_set_send_timeout(client, 1000) == 0);
    EXPECT(socket_send(client, "\n", 1) == 1);
    socket_close(client);
}

int process_is_gone(int pid)
{
    if (pid <= 0)
        return 0;
    /* Bash reports MSYS pids, which differ from native Windows process ids. */
    char *probe = xasprintf("kill -0 %d 2>/dev/null", pid);
    long deadline = monotonic_ms() + 10000;
    while (monotonic_ms() < deadline) {
        int status = spawn_shell_wait(probe);
        if (status != -1 && !spawn_status_success(status)) {
            free(probe);
            return 1;
        }
        clock_sleep_ms(5);
    }
    free(probe);
    return 0;
}
