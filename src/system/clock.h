/* SPDX-License-Identifier: MIT */
#ifndef HAX_SYSTEM_CLOCK_H
#define HAX_SYSTEM_CLOCK_H

#include <time.h>

/* CLOCK_MONOTONIC milliseconds since an unspecified epoch. */
long monotonic_ms(void);

/* Convert a timestamp into caller-owned broken-down time, avoiding shared libc buffers. Return
 * 0 on success or -1 on an unrepresentable timestamp. Local time follows the process timezone. */
int clock_utc(time_t timestamp, struct tm *out);
int clock_local(time_t timestamp, struct tm *out);

/* Delay for positive milliseconds. Negative and zero durations are no-ops. */
void clock_sleep_ms(int duration_ms);

#endif /* HAX_SYSTEM_CLOCK_H */
