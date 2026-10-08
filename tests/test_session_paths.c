/* SPDX-License-Identifier: MIT */
#include <stdlib.h>

#include "harness.h"
#include "session_paths.h"

int main(void)
{
    const char name[] = "2020-01-01T00-00-00Z_12345678-1234-1234-1234-123456789abc.jsonl";
    EXPECT(session_path_is_standard(name));
    EXPECT(session_path_is_standard(
        "/tmp/2020-01-01T00-00-00Z_12345678-1234-1234-1234-123456789abc.jsonl"));
#ifdef _WIN32
    EXPECT(session_path_is_standard(
        "C:\\sessions\\2020-01-01T00-00-00Z_12345678-1234-1234-1234-123456789abc.jsonl"));
#endif
    EXPECT(!session_path_is_standard(NULL));
    EXPECT(!session_path_is_standard(""));
    EXPECT(!session_path_is_standard("unrelated_12345678-1234-1234-1234-123456789abc.jsonl"));
    EXPECT(!session_path_is_standard(
        "2020-01-01T00-00-00Z_12345678-1234-1234-1234-123456789abg.jsonl"));
    EXPECT(!session_path_is_standard(
        "2020-01-01T00-00-00Z_12345678-1234-1234-1234-123456789abc.jsonl.old"));
    char *id = session_id_from_path(name);
    EXPECT(id != NULL);
    if (id) {
        EXPECT_STR_EQ(id, "12345678-1234-1234-1234-123456789abc");
        free(id);
    }
    T_REPORT();
}
