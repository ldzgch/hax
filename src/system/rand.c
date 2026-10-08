/* SPDX-License-Identifier: MIT */
#include "system/rand.h"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <limits.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"

void random_bytes(void *out, size_t len)
{
#ifdef _WIN32
    unsigned char *next = out;
    while (len) {
        ULONG count = len > ULONG_MAX ? ULONG_MAX : (ULONG)len;
        if (BCryptGenRandom(NULL, next, count, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
            hax_err("Windows random number generator failed");
            abort();
        }
        next += count;
        len -= count;
    }
#else
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        hax_err("open /dev/urandom: %s", strerror(errno));
        abort();
    }

    size_t bytes_read = 0;
    while (bytes_read < len) {
        ssize_t count = read(fd, (char *)out + bytes_read, len - bytes_read);
        if (count < 0) {
            if (errno == EINTR)
                continue;
            hax_err("read /dev/urandom: %s", strerror(errno));
            abort();
        }
        if (count == 0) {
            hax_err("unexpected EOF on /dev/urandom");
            abort();
        }
        bytes_read += (size_t)count;
    }
    close(fd);
#endif
}

void gen_uuid_v4(char out[37])
{
    uint8_t bytes[16];
    random_bytes(bytes, sizeof(bytes));

    bytes[6] = (bytes[6] & 0x0f) | 0x40; /* RFC 4122 version 4 */
    bytes[8] = (bytes[8] & 0x3f) | 0x80; /* RFC 4122 variant */

    snprintf(out, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
             bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
}
