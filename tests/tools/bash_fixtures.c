/* SPDX-License-Identifier: MIT */
#include "tools/bash_fixtures.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buf.h"
#include "harness.h"
#include "tool.h"
#include "xalloc.h"
#include "system/clock.h"
#include "system/fs.h"

char *call_bash_background(const char *escaped_command)
{
    char *args = xasprintf("{\"command\":\"%s\",\"background\":true}", escaped_command);
    char *out = TOOL_BASH.run(args, NULL);
    free(args);
    return out;
}

char *extract_task_id(const char *result)
{
    const char *needle = "task t";
    const char *start = strstr(result, needle);
    if (!start)
        return NULL;
    start += strlen(needle) - 1; /* keep the 't' */
    size_t len = 1;
    while (start[len] >= '0' && start[len] <= '9')
        len++;
    if (len == 1)
        return NULL;
    char *id = xmalloc(len + 1);
    memcpy(id, start, len);
    id[len] = '\0';
    return id;
}

char *wait_for_id(const char *id, int timeout_seconds)
{
    char *args;
    if (timeout_seconds > 0)
        args = xasprintf("{\"id\":\"%s\",\"timeout_seconds\":%d}", id, timeout_seconds);
    else
        args = xasprintf("{\"id\":\"%s\"}", id);
    char *out = TOOL_TASK_WAIT.run(args, NULL);
    free(args);
    return out;
}

void expect_task_stopped(const char *result)
{
#ifdef _WIN32
    EXPECT(strstr(result, "finished (exit 1)") != NULL);
#else
    EXPECT(strstr(result, "killed (signal ") != NULL);
#endif
}

char *kill_id(const char *id)
{
    char *args = xasprintf("{\"id\":\"%s\",\"kill\":true}", id);
    char *out = TOOL_TASK_WAIT.run(args, NULL);
    free(args);
    return out;
}

int await_pid_file(const char *path)
{
    long deadline = monotonic_ms() + 10000;
    while (monotonic_ms() < deadline) {
        int pid = -1;
        char *contents = fs_read_file(path, NULL);
        if (contents) {
            if (sscanf(contents, "%d", &pid) != 1)
                pid = -1;
            free(contents);
        }
        if (pid > 0)
            return pid;
        clock_sleep_ms(5);
    }
    return -1;
}

void append_display(const char *bytes, size_t len, void *data)
{
    struct display_capture *capture = data;
    buf_append(&capture->buf, bytes, len);
    if (capture->release_gate && strstr(capture->buf.data, capture->release_on)) {
        gate_release(capture->release_gate);
        capture->release_gate = NULL;
    }
}
