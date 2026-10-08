/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#ifndef _WIN32
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#endif

#include "harness.h"
#ifndef _WIN32
#include "xalloc.h"
#include "system/browser.h"
#include "system/fs.h"
#endif

#ifndef _WIN32
/* The recording is published by rename, so a poll never observes a partially written file. */
static void write_fake_opener(const char *dir, const char *name, const char *out_path)
{
    char *path = xasprintf("%s/%s", dir, name);
    FILE *script = fopen(path, "w");
    EXPECT(script != NULL);
    if (script) {
        /* Absolute command paths: the test replaces PATH with just the fixture directory, and
         * printf is not a builtin in every /bin/sh (OpenBSD's pdksh). */
        fprintf(script,
                "#!/bin/sh\n/usr/bin/printf '%%s' \"$1\" > '%s.tmp' && /bin/mv '%s.tmp' '%s'\n",
                out_path, out_path, out_path);
        fclose(script);
        EXPECT(chmod(path, 0755) == 0);
    }
    free(path);
}

static void test_hands_url_to_opener(void)
{
    char *dir = t_tempdir();
    char *out_path = xasprintf("%s/url.txt", dir);
    write_fake_opener(dir, "open", out_path);
    write_fake_opener(dir, "xdg-open", out_path);

    char *saved_path = t_path_replace(dir);
    browser_open_url("https://example.test/authorize?x=1&y=2");
    t_path_restore(saved_path);

    /* The opener runs detached, so the recording lands asynchronously. */
    char *recorded = NULL;
    for (int i = 0; i < 300 && !recorded; i++) {
        recorded = fs_read_file(out_path, NULL);
        if (!recorded) {
            struct timespec pause = {.tv_nsec = 10 * 1000 * 1000};
            nanosleep(&pause, NULL);
        }
    }
    EXPECT(recorded != NULL);
    if (recorded)
        EXPECT_STR_EQ(recorded, "https://example.test/authorize?x=1&y=2");
    free(recorded);
    free(out_path);
}
#endif

int main(void)
{
#ifdef _WIN32
    fprintf(stderr,
            "%s:%d: skip: ShellExecuteW handoff requires an interactive Windows shell "
            "to observe\n",
            __FILE__, __LINE__);
    t_skips++;
#else
    test_hands_url_to_opener();
#endif
    T_REPORT();
}
