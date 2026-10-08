/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#endif

#include "config.h"
#include "harness.h"
#include "xalloc.h"
#include "system/keepawake.h"

#ifdef _WIN32
static void expect_inhibition(int inhibited)
{
    EXECUTION_STATE previous = SetThreadExecutionState(ES_CONTINUOUS);
    EXPECT(previous != 0);
    EXPECT(!!(previous & ES_SYSTEM_REQUIRED) == inhibited);
    if (previous)
        EXPECT(SetThreadExecutionState(previous) != 0);
}
#endif

static void expect_released(void)
{
#ifdef _WIN32
    expect_inhibition(0);
#else
    int status;
    errno = 0;
    pid_t child = waitpid(-1, &status, WNOHANG);
    EXPECT(child == -1 && errno == ECHILD);
#endif
}

static void test_release_without_acquire(void)
{
    keepawake_release();
    expect_released();
}

static void test_acquire_release_cycle(void)
{
    keepawake_acquire();
#ifdef _WIN32
    expect_inhibition(1);
#endif
    keepawake_release();
    expect_released();
}

static void test_double_acquire(void)
{
    keepawake_acquire();
    keepawake_acquire();
#ifdef _WIN32
    expect_inhibition(1);
#endif
    keepawake_release();
    expect_released();
}

static void test_disabled_is_noop(void)
{
    config_set_override("keep_awake", "0");
    keepawake_acquire();
    expect_released();
    keepawake_release();
    config_set_override("keep_awake", "1");
}

#ifndef _WIN32
static void test_sleep_not_resolved_via_path(void)
{
    char *dir = t_tempdir();
    char *fake_sleep = xasprintf("%s/sleep", dir);
    char *marker = xasprintf("%s/ran", dir);

    FILE *file = fopen(fake_sleep, "w");
    EXPECT(file != NULL);
    if (!file)
        goto out;
    fprintf(file, "#!/bin/sh\ntouch '%s'\n", marker);
    fclose(file);
    chmod(fake_sleep, 0755);

    char *saved_path = t_path_prepend(dir);

    keepawake_acquire();
    int fake_ran = 0;
    for (int i = 0; i < 50 && !fake_ran; i++) {
        fake_ran = access(marker, F_OK) == 0;
        if (!fake_ran) {
            struct timespec delay = {0, 10 * 1000000};
            nanosleep(&delay, NULL);
        }
    }
    keepawake_release();

    EXPECT(!fake_ran);
    t_path_restore(saved_path);

out:
    free(marker);
    free(fake_sleep);
}

#endif

int main(void)
{
    config_set_override("keep_awake", "1");
    test_release_without_acquire();
    test_acquire_release_cycle();
    test_double_acquire();
    test_disabled_is_noop();
#ifndef _WIN32
    test_sleep_not_resolved_via_path();
#endif
    T_REPORT();
}
