/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include "harness.h"
#include "xalloc.h"
#include "system/fs.h"
#include "system/line_reader.h"

static void test_binary_lines_and_growth(void)
{
    char *path = xasprintf("%s/lines", t_tempdir());
    int fd = fs_open_private(path, 1);
    free(path);
    FILE *stream = fd >= 0 ? fdopen(fd, "w+b") : NULL;
    EXPECT(stream != NULL);
    if (!stream) {
        if (fd >= 0)
            close(fd);
        return;
    }
    const char first[] = {'a', '\0', (char)0xff, '\r', '\n'};
    EXPECT(fwrite(first, 1, sizeof(first), stream) == sizeof(first));
    for (int i = 0; i < 2048; i++)
        EXPECT(fputc('x', stream) != EOF);
    EXPECT(fputc('\n', stream) != EOF);
    EXPECT(fputs("tail", stream) >= 0);
    rewind(stream);
    char *line = NULL;
    size_t capacity = 0;
    EXPECT(line_reader_get(stream, &line, &capacity) == sizeof(first));
    EXPECT_MEM_EQ(line, sizeof(first), first, sizeof(first));
    EXPECT(line[sizeof(first)] == '\0');
    EXPECT(line_reader_get(stream, &line, &capacity) == 2049);
    EXPECT(capacity >= 2050);
    for (int i = 0; i < 2048; i++)
        EXPECT(line[i] == 'x');
    EXPECT(line[2048] == '\n' && line[2049] == '\0');
    EXPECT(line_reader_get(stream, &line, &capacity) == 4);
    EXPECT_STR_EQ(line, "tail");
    EXPECT(line_reader_get(stream, &line, &capacity) == -1);
    EXPECT(feof(stream));
    free(line);
    fclose(stream);
}

int main(void)
{
    test_binary_lines_and_growth();
    errno = 0;
    EXPECT(line_reader_get(NULL, NULL, NULL) == -1 && errno == EINVAL);
    T_REPORT();
}
