/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdlib.h>

#include "harness.h"
#include "text/windows_argv.h"

int main(void)
{
    const char *argv[] = {"program name.exe", "",         "space value", "quote\"value", "tail\\",
                          "slash\\\"quote",   "\xc3\xa9", NULL};
    char *line = windows_argv_encode(argv);
    EXPECT(line != NULL);
    if (line) {
        EXPECT_STR_EQ(line, "\"program name.exe\" \"\" \"space value\" \"quote\\\"value\" "
                            "\"tail\\\\\" \"slash\\\\\\\"quote\" \"\xc3\xa9\"");
        free(line);
    }
    errno = 0;
    EXPECT(windows_argv_encode(NULL) == NULL && errno == EINVAL);
    const char *empty[] = {"", NULL};
    EXPECT(windows_argv_encode(empty) == NULL);
    T_REPORT();
}
