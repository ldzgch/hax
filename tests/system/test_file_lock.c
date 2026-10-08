/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include "harness.h"
#include "xalloc.h"
#include "system/file_lock.h"
#include "system/fs.h"

static void test_independent_descriptors(void)
{
    char *path = xasprintf("%s/lock", t_tempdir());
    int first = fs_open_private(path, 1);
    int second = fs_open_private(path, 0);
    EXPECT(first >= 0 && second >= 0);
    if (first < 0 || second < 0)
        goto out;
    EXPECT(file_lock_fd(first, 1, 0) == 0);
    errno = 0;
    EXPECT(file_lock_fd(second, 1, 1) == -1 && errno == EWOULDBLOCK);
    errno = 0;
    EXPECT(file_lock_fd(second, 0, 1) == -1 && errno == EWOULDBLOCK);
    EXPECT(file_unlock_fd(first) == 0);
    EXPECT(file_lock_fd(first, 0, 0) == 0);
    EXPECT(file_lock_fd(second, 0, 1) == 0);
    EXPECT(write(first, "a", 1) == 1);
    EXPECT(write(second, "b", 1) == 1);
    EXPECT(file_unlock_fd(second) == 0);
    errno = 0;
    EXPECT(file_lock_fd(second, 1, 1) == -1 && errno == EWOULDBLOCK);
    close(first);
    first = -1;
    EXPECT(file_lock_fd(second, 1, 1) == 0);
    EXPECT(file_unlock_fd(second) == 0);
out:
    if (second >= 0)
        close(second);
    if (first >= 0)
        close(first);
    free(path);
}

int main(void)
{
    test_independent_descriptors();
    T_REPORT();
}
