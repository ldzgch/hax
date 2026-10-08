/* SPDX-License-Identifier: MIT */
#include <time.h>

#include "harness.h"
#include "system/clock.h"

static void test_monotonic_ms_advances(void)
{
    long first = monotonic_ms();
    EXPECT(first >= 0);

    /* nanosleep() waits at least the requested time, so two elapsed milliseconds floor to a
     * difference of one or more. */
    struct timespec delay = {.tv_nsec = 2 * 1000 * 1000};
    nanosleep(&delay, NULL);

    long second = monotonic_ms();
    EXPECT(second - first >= 1);
}

static void test_calendar_conversions(void)
{
    struct tm utc;
    EXPECT(clock_utc(0, &utc) == 0);
    EXPECT(utc.tm_year == 70 && utc.tm_mon == 0 && utc.tm_mday == 1);
    EXPECT(utc.tm_hour == 0 && utc.tm_min == 0 && utc.tm_sec == 0);
    EXPECT(clock_utc((time_t)2200000000, &utc) == 0);
    EXPECT(utc.tm_year == 139);
    struct tm local;
    time_t timestamp = 946684800;
    EXPECT(clock_local(timestamp, &local) == 0);
    EXPECT(mktime(&local) == timestamp);
}

int main(void)
{
    test_monotonic_ms_advances();
    test_calendar_conversions();

    T_REPORT();
}
