/* SPDX-License-Identifier: MIT */
#ifndef HAX_TESTS_STREAM_CAPTURE_H
#define HAX_TESTS_STREAM_CAPTURE_H

#include "system/stream_capture.h"

struct t_stream_capture {
    struct stream_capture sink;
    char *snapshot;
};

/* Open a portable binary rendering sink. Return its borrowed stream, or NULL on failure. */
FILE *t_stream_capture_open(struct t_stream_capture *capture);
/* Flush the sink and return a borrowed snapshot valid until the next read or close. */
const char *t_stream_capture_read(struct t_stream_capture *capture);
/* Close the sink and free snapshots. */
void t_stream_capture_close(struct t_stream_capture *capture);

#endif /* HAX_TESTS_STREAM_CAPTURE_H */
