/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include "harness.h"
#include "xalloc.h"
#include "tools/bash_fixtures.h"

int process_is_gone(int pid)
{
    /* kill(0) and kill(-1) would probe the process group and every process. */
    if (pid <= 0)
        return 0;
    time_t start = time(NULL);
    while (time(NULL) - start < 10) {
        /* ESRCH on Linux, EPERM on Darwin. */
        if (kill(pid, 0) < 0)
            return 1;
        struct timespec ts = {.tv_sec = 0, .tv_nsec = 5 * 1000000L};
        nanosleep(&ts, NULL);
    }
    kill(pid, SIGKILL);
    return 0;
}

char *gate_create(void)
{
    char *path = xasprintf("%s/gate", t_tempdir());
    EXPECT(mkfifo(path, 0600) == 0);
    return path;
}

void gate_release(const char *path)
{
    int fd = -1;
    time_t start = time(NULL);
    while (time(NULL) - start < 10) {
        fd = open(path, O_WRONLY | O_NONBLOCK);
        if (fd >= 0 || errno != ENXIO)
            break;
        struct timespec ts = {.tv_sec = 0, .tv_nsec = 3 * 1000000L};
        nanosleep(&ts, NULL);
    }
    EXPECT(fd >= 0);
    if (fd >= 0) {
        EXPECT(write(fd, "\n", 1) == 1);
        close(fd);
    }
}
