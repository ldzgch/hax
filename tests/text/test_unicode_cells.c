/* SPDX-License-Identifier: MIT */
#include "harness.h"
#include "text/unicode_cells.h"

static void test_printable_widths(void)
{
    EXPECT(unicode_codepoint_cells('a') == 1);
    EXPECT(unicode_codepoint_cells(0x00E9) == 1);
    EXPECT(unicode_codepoint_cells(0x03A9) == 1);
    EXPECT(unicode_codepoint_cells(0x6587) == 2);
    EXPECT(unicode_codepoint_cells(0xFF21) == 2);
    EXPECT(unicode_codepoint_cells(0x1F980) == 2);
    EXPECT(unicode_codepoint_cells(0x20000) == 2);
    EXPECT(unicode_codepoint_cells(0x10FFFF) == -1);
}

static void test_combining_widths(void)
{
    EXPECT(unicode_codepoint_cells(0x0301) == 0);
    EXPECT(unicode_codepoint_cells(0x1D167) == 0);
    EXPECT(unicode_codepoint_cells(0xE0100) == 0);
    EXPECT(unicode_codepoint_cells(0x1161) == 0);
}

static void test_nonprintable(void)
{
    EXPECT(unicode_codepoint_cells(0) == -1);
    EXPECT(unicode_codepoint_cells('\n') == -1);
    EXPECT(unicode_codepoint_cells(0x7F) == -1);
    EXPECT(unicode_codepoint_cells(0x9F) == -1);
    EXPECT(unicode_codepoint_cells(0x378) == -1);
    EXPECT(unicode_codepoint_cells(0xD800) == -1);
    EXPECT(unicode_codepoint_cells(0xDFFF) == -1);
    EXPECT(unicode_codepoint_cells(0x110000) == -1);
    EXPECT(unicode_codepoint_cells(UINT32_MAX) == -1);
}

int main(void)
{
    test_printable_widths();
    test_combining_widths();
    test_nonprintable();
    T_REPORT();
}
