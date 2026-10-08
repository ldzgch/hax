/* SPDX-License-Identifier: MIT */
#ifndef HAX_SYSTEM_FILE_LOCK_H
#define HAX_SYSTEM_FILE_LOCK_H

/* Coordinate access to a regular file. POSIX uses advisory flock; Windows locks a reserved byte
 * at INT64_MAX so shared holders can still write file data. All participants must use this API.
 * Locks belong to the open descriptor and are released on close.
 * Separate opens contend even within one process. With nonblocking, contention returns -1 with
 * errno EWOULDBLOCK. Returns 0 on success, or -1 with errno set. */
int file_lock_fd(int fd, int exclusive, int nonblocking);
int file_unlock_fd(int fd);

#endif /* HAX_SYSTEM_FILE_LOCK_H */
