/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "system/fs.h"
#include "system/stream_capture.h"

static void test_binary_capture(void)
{
    struct stream_capture capture;
    EXPECT(stream_capture_open(&capture) == 0);
    if (!capture.stream)
        return;
    const char chunk[] = "\r\n\x1a\0\xc3\xa9";
    for (int i = 0; i < 10000; i++)
        EXPECT(fwrite(chunk, 1, sizeof(chunk), capture.stream) == sizeof(chunk));
    char *path = capture.path ? strdup(capture.path) : NULL;
    size_t length = 0;
    char *data = stream_capture_finish(&capture, &length);
    EXPECT(data != NULL);
    EXPECT(length == sizeof(chunk) * 10000);
    if (data && length == sizeof(chunk) * 10000) {
        for (int i = 0; i < 10000; i++)
            EXPECT(memcmp(data + (size_t)i * sizeof(chunk), chunk, sizeof(chunk)) == 0);
        EXPECT(data[length] == '\0');
    }
    free(data);
    if (path)
        EXPECT(fs_entry_exists(path) == 0);
    free(path);
}

static void test_empty_capture(void)
{
    struct stream_capture capture;
    EXPECT(stream_capture_open(&capture) == 0);
    if (!capture.stream)
        return;
    size_t length = 1;
    char *data = stream_capture_finish(&capture, &length);
    EXPECT(data != NULL && length == 0);
    if (data)
        EXPECT_STR_EQ(data, "");
    free(data);
}

int main(void)
{
    test_binary_capture();
    test_empty_capture();
    T_REPORT();
}
