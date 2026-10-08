/* SPDX-License-Identifier: MIT */
#include "harness.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Ownership is per pid: a forked child that never calls t_tempdir() must not remove its parent's
 * dirs, and one that does removes only entries from t_tmpdir_first on, since an ancestor created
 * (and removes) the ones before. */
static char **t_tmpdirs;
static size_t t_n_tmpdirs;
static size_t t_tmpdir_first;
static pid_t t_tmpdir_owner;

/* Remove `name` under `dir_fd` recursively without following symlinks. Directories get u+rwx before
 * descending, so fixtures locked down to provoke EACCES don't defeat removal; file modes stay
 * untouched (unlink ignores them), so a hard link in a fixture can't rewrite an outside inode's
 * mode. In-process because spawning rm costs more than most tests do. */
static int remove_tree(int dir_fd, const char *name)
{
    struct stat st;
    if (fstatat(dir_fd, name, &st, AT_SYMLINK_NOFOLLOW) < 0)
        return errno == ENOENT ? 0 : -1;
    if (!S_ISDIR(st.st_mode))
        return unlinkat(dir_fd, name, 0);
    if ((st.st_mode & S_IRWXU) != S_IRWXU && fchmodat(dir_fd, name, st.st_mode | S_IRWXU, 0) < 0)
        return -1;
    int fd = openat(dir_fd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return -1;
    DIR *dir = fdopendir(fd);
    if (!dir) {
        close(fd);
        return -1;
    }
    int failed = 0;
    size_t removed;
    /* Unlinking while reading may make readdir skip entries on some filesystems, so rescan until a
     * pass finds nothing left to remove. */
    do {
        removed = 0;
        rewinddir(dir);
        struct dirent *entry;
        while ((entry = readdir(dir))) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            if (remove_tree(dirfd(dir), entry->d_name) < 0)
                failed = 1;
            else
                removed++;
        }
    } while (!failed && removed > 0);
    closedir(dir);
    if (failed)
        return -1;
    return unlinkat(dir_fd, name, AT_REMOVEDIR);
}

static void tempdir_cleanup(void)
{
    if (getpid() != t_tmpdir_owner)
        return;
    for (size_t i = 0; i < t_n_tmpdirs; i++) {
        if (i >= t_tmpdir_first && remove_tree(AT_FDCWD, t_tmpdirs[i]) < 0)
            fprintf(stderr, "t_tempdir: failed to remove %s: %s\n", t_tmpdirs[i], strerror(errno));
        /* Inherited entries are freed only at exit: tests hold pointers into them. */
        free(t_tmpdirs[i]);
    }
    free(t_tmpdirs);
    t_tmpdirs = NULL;
    t_n_tmpdirs = 0;
    t_tmpdir_first = 0;
}

char *t_tempdir(void)
{
    if (t_tmpdir_owner != getpid()) {
        /* First call in this process. Registering the handler again in a fork child is harmless:
         * handlers run LIFO, so the inherited registration finds the list already emptied. */
        t_tmpdir_first = t_n_tmpdirs;
        t_tmpdir_owner = getpid();
        atexit(tempdir_cleanup);
    }
    char *dir = strdup("/tmp/hax_test_XXXXXX");
    if (!dir || !mkdtemp(dir)) {
        fprintf(stderr, "t_tempdir: %s\n", strerror(errno));
        abort();
    }
    /* A /tmp spelling would surface as a puzzling mismatch in whichever test compares it against
     * getcwd(), far from the cause. */
    char *real = realpath(dir, NULL);
    if (!real) {
        fprintf(stderr, "t_tempdir: realpath(%s): %s\n", dir, strerror(errno));
        abort();
    }
    free(dir);
    char **grown = realloc(t_tmpdirs, (t_n_tmpdirs + 1) * sizeof(*grown));
    if (!grown)
        abort();
    t_tmpdirs = grown;
    t_tmpdirs[t_n_tmpdirs++] = real;
    return real;
}
