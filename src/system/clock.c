/* SPDX-License-Identifier: MIT */
#include "system/clock.h"

#include <errno.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

long monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

int clock_utc(time_t timestamp, struct tm *out)
{
#ifdef _WIN32
    return gmtime_s(out, &timestamp) == 0 ? 0 : -1;
#else
    return gmtime_r(&timestamp, out) ? 0 : -1;
#endif
}

int clock_local(time_t timestamp, struct tm *out)
{
#ifdef _WIN32
    return localtime_s(out, &timestamp) == 0 ? 0 : -1;
#else
    return localtime_r(&timestamp, out) ? 0 : -1;
#endif
}

void clock_sleep_ms(int duration_ms)
{
    if (duration_ms <= 0)
        return;
#ifdef _WIN32
    Sleep((DWORD)duration_ms);
#else
    struct timespec remaining = {.tv_sec = duration_ms / 1000,
                                 .tv_nsec = (duration_ms % 1000) * 1000000L};
    while (nanosleep(&remaining, &remaining) < 0 && errno == EINTR)
        ;
#endif
}
