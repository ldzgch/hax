/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "env.h"
#include "harness.h"
#include "tool.h"
#include "xalloc.h"
#include "system/clock.h"
#include "system/fs.h"
#include "system/path.h"
#include "system/tempfiles.h"
#include "tools/bash_process.h"
#include "tools/task_registry.h"

static void test_foreground_output_and_exit(void)
{
    char *output =
        bash_run_command("printf 'hello'; printf 'error' >&2; exit 7", 5000, 0, NULL, NULL);
    EXPECT_STR_EQ(output, "helloerror\n[exit 7]");
    free(output);
    output = bash_run_command("printf '\\303\\251\\r\\n'", 5000, 0, NULL, NULL);
    EXPECT_STR_EQ(output, "\xc3\xa9\r\n");
    free(output);
    output = bash_run_command("printf 'a\\000b'", 5000, 0, NULL, NULL);
    EXPECT(strstr(output, "binary") != NULL);
    free(output);
}

static void test_background_handoff_and_exit(void)
{
    char *gate = path_join(t_tempdir(), "release");
    char *command = xasprintf("printf start; while [ ! -f '%s' ]; do sleep 0.01; done; "
                              "printf done; exit 7",
                              gate);
    t_env_set("HAX_BASH_TRANSITION_MIN_BYTES", "5");
    char *output = bash_run_command(command, 5000, 1, "worker", NULL);
    EXPECT(strstr(output, "start") != NULL);
    EXPECT(strstr(output, "detached as task worker") != NULL);
    free(output);
    EXPECT(fs_write_atomic(gate, "release", 7, 1) == 0);
    output = task_wait_stream("worker", 5000, 0, NULL, NULL);
    EXPECT(strstr(output, "done") != NULL);
    EXPECT(strstr(output, "exit 7") != NULL);
    EXPECT(task_running_count() == 0);
    free(output);
    task_registry_shutdown();
    free(command);
    free(gate);
    t_env_unset("HAX_BASH_TRANSITION_MIN_BYTES");
}

static void test_timeout_and_shutdown_stop_descendants(void)
{
    const char *directory = t_tempdir();
    char *path = path_join(directory, "survivor");
    char *gate = path_join(directory, "release");
    char *command = xasprintf("(while [ ! -f '%s' ]; do sleep 0.01; done; "
                              "printf survived > '%s') & printf ready; sleep 30",
                              gate, path);
    t_env_set("HAX_BASH_TRANSITION_MIN_BYTES", "5");
    config_set_override("no_tasks", "true");
    char *output = bash_run_command(command, 10, 0, NULL, NULL);
    EXPECT(strstr(output, "timed out") != NULL);
    free(output);
    config_set_override("no_tasks", "false");
    output = bash_run_command(command, 5000, 1, "stopped", NULL);
    EXPECT(strstr(output, "detached as task stopped") != NULL);
    free(output);
    task_registry_shutdown();
    EXPECT(fs_write_atomic(gate, "release", 7, 1) == 0);
    clock_sleep_ms(1500);
    EXPECT(fs_entry_exists(path) == 0);
    t_env_unset("HAX_BASH_TRANSITION_MIN_BYTES");
    free(command);
    free(gate);
    free(path);
}

static void test_fatal_cleanup_and_stale_token(void)
{
    struct shell_process first = {0};
    char *error = bash_start_shell("sleep 30", &first);
    EXPECT(error == NULL);
    if (error) {
        free(error);
        return;
    }
    bash_shell_pgids_kill();
    int status;
    EXPECT(bash_process_wait(first.pid, &status) == 0);
    EXPECT(status != 0);
    close(first.output_fd);

    struct shell_process second = {0};
    error = bash_start_shell("printf intact", &second);
    EXPECT(error == NULL);
    if (error) {
        free(error);
        return;
    }
    errno = 0;
    int seen = 0;
    EXPECT(bash_process_exit_seen(first.pid, &seen) == -1);
    EXPECT(errno == ECHILD && !seen);
    bash_signal_process_tree(first.pid, 9);
    EXPECT(bash_process_wait(second.pid, &status) == 0);
    EXPECT(status == 0);
    close(second.output_fd);
}

int main(void)
{
    config_set_override("bash.background_yield", "10ms");
    config_set_override("bash.timeout_grace", "0ms");
    test_fatal_cleanup_and_stale_token();
    test_foreground_output_and_exit();
    test_background_handoff_and_exit();
    test_timeout_and_shutdown_stop_descendants();
    task_registry_shutdown();
    tempfiles_cleanup();
    config_free();
    T_REPORT();
}
