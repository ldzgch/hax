/* SPDX-License-Identifier: MIT */
#include "text/unicode_cells.h"

#include <stddef.h>

struct unicode_interval {
    uint32_t first;
    uint32_t last;
};

#include "text/unicode_width_data.h"

static int contains(const struct unicode_interval *intervals, size_t count, uint32_t codepoint)
{
    size_t first = 0;
    while (first < count) {
        size_t middle = first + (count - first) / 2;
        if (codepoint < intervals[middle].first)
            count = middle;
        else if (codepoint > intervals[middle].last)
            first = middle + 1;
        else
            return 1;
    }
    return 0;
}

int unicode_codepoint_cells(uint32_t codepoint)
{
    if (codepoint > 0x10FFFF ||
        contains(NONPRINTABLE, sizeof(NONPRINTABLE) / sizeof(*NONPRINTABLE), codepoint))
        return -1;
    if (contains(ZERO_WIDTH, sizeof(ZERO_WIDTH) / sizeof(*ZERO_WIDTH), codepoint))
        return 0;
    return contains(WIDE, sizeof(WIDE) / sizeof(*WIDE), codepoint) ? 2 : 1;
}
