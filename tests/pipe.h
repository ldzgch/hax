/* SPDX-License-Identifier: MIT */
#ifndef HAX_TESTS_PIPE_H
#define HAX_TESTS_PIPE_H

struct t_pipe;

/* Create an owned writer-less POSIX FIFO or Windows named pipe for special-file rejection tests.
 * Return NULL with errno on failure. The returned path remains borrowed until close. */
struct t_pipe *t_pipe_create(void);
const char *t_pipe_path(const struct t_pipe *pipe);
int t_pipe_exists(const struct t_pipe *pipe);
void t_pipe_close(struct t_pipe *pipe);

#endif /* HAX_TESTS_PIPE_H */
