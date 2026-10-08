/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <string.h>

#include "env.h"
#include "harness.h"
#include "system/tempfiles.h"
#include "tools/bash_output.h"

static void test_oversized_memory_cap_spills_before_drain_limit(void)
{
    /* The drain stops reading at BASH_OUTPUT_DRAIN_LIMIT, so output must spill before that, or a
     * configured cap above it would leave the truncated result nowhere to point. */
    struct bash_output *output = bash_output_create(32L * 1024 * 1024);
    static char chunk[64 * 1024];
    memset(chunk, 'x', sizeof(chunk));
    for (long appended = 0; appended < BASH_OUTPUT_DRAIN_LIMIT; appended += sizeof(chunk))
        bash_output_append(output, chunk, sizeof(chunk));

    char *result = bash_output_finish(output, 0, BASH_STOP_NONE, 0, 0);
    EXPECT(strstr(result, "[output truncated") != NULL);
    EXPECT(strstr(result, "saved to ") != NULL);
    free(result);
    bash_output_destroy(output);
}

static void test_exit_status_suffix(void)
{
#ifdef _WIN32
    int status = 42;
#else
    int status = 42 << 8;
#endif
    char *suffix = bash_output_format_suffix(0, 0, 0, BASH_STOP_NONE, 0, status);
    EXPECT_STR_EQ(suffix, "\n[exit 42]");
    free(suffix);
    suffix = bash_output_format_suffix(0, 0, 0, BASH_STOP_NONE, 0, 0);
    EXPECT_STR_EQ(suffix, "(no output)");
    free(suffix);
}

int main(void)
{
    /* Pin the cap so inherited configuration cannot invalidate truncation fixtures. */
    t_env_set("HAX_TOOL_OUTPUT_CAP", "50k");
    test_oversized_memory_cap_spills_before_drain_limit();
    test_exit_status_suffix();
    tempfiles_cleanup();
    T_REPORT();
}
