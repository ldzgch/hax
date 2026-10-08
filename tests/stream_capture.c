/* SPDX-License-Identifier: MIT */
#include "stream_capture.h"

#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "xalloc.h"
#ifdef _WIN32
#include "system/fs.h"
#endif

FILE *t_stream_capture_open(struct t_stream_capture *capture)
{
    memset(capture, 0, sizeof(*capture));
    if (stream_capture_open(&capture->sink) < 0)
        return NULL;
    return capture->sink.stream;
}

const char *t_stream_capture_read(struct t_stream_capture *capture)
{
    EXPECT(fflush(capture->sink.stream) == 0);
    free(capture->snapshot);
#ifdef _WIN32
    capture->snapshot = fs_read_file(capture->sink.path, NULL);
#else
    capture->snapshot = xstrdup(capture->sink.data ? capture->sink.data : "");
#endif
    EXPECT(capture->snapshot != NULL);
    return capture->snapshot ? capture->snapshot : "";
}

void t_stream_capture_close(struct t_stream_capture *capture)
{
    free(stream_capture_finish(&capture->sink, NULL));
    free(capture->snapshot);
    capture->snapshot = NULL;
}
