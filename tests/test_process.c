/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "process.h"
#include "system/clock.h"
#include "system/path.h"

static void test_wait_preserves_exit_code(const char *program)
{
    const char *argv[] = {program, "--exit-seven", NULL};
    struct t_process *child = t_process_start(argv);
    EXPECT(child != NULL);
    if (!child)
        return;
    EXPECT(t_process_wait(child, 2000) == 7);
    EXPECT(t_process_wait(child, 2000) == 7);
    t_process_close(child);
}

static void test_wait_timeout_reaps_child(const char *program)
{
    const char *argv[] = {program, "--stall", NULL};
    struct t_process *child = t_process_start(argv);
    EXPECT(child != NULL);
    if (!child)
        return;
    long before = monotonic_ms();
    EXPECT(t_process_wait(child, 100) == -1 && errno == ETIMEDOUT);
    EXPECT(monotonic_ms() - before < 2000);
    t_process_close(child);
}

int main(int argc, char **argv)
{
    if (argc == 2) {
        if (strcmp(argv[1], "--exit-seven") == 0)
            return 7;
        if (strcmp(argv[1], "--stall") == 0) {
            clock_sleep_ms(30000);
            return 0;
        }
        return 2;
    }
    char *program = t_program_path(argv[0]);
    EXPECT(program != NULL);
    if (program) {
        EXPECT(path_is_absolute(program));
        test_wait_preserves_exit_code(program);
        test_wait_timeout_reaps_child(program);
        free(program);
    }
    T_REPORT();
}
