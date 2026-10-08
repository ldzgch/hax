/* SPDX-License-Identifier: MIT */
#include <stdlib.h>

#include "env.h"
#include "harness.h"
#include "terminal/width.h"

static void test_auto_display_width(void)
{
    EXPECT(auto_display_width(19) == 20);
    EXPECT(auto_display_width(20) == 20);
    EXPECT(auto_display_width(100) == 100);
    EXPECT(auto_display_width(101) == 101);
    EXPECT(auto_display_width(110) == 110);
    EXPECT(auto_display_width(111) == 100);
}

static void test_display_width_auto(void)
{
    t_env_unset("HAX_DISPLAY_WIDTH");
    int expected = auto_display_width(term_width());
    EXPECT(display_width() == expected);

    t_env_set("HAX_DISPLAY_WIDTH", "auto");
    EXPECT(display_width() == expected);
    t_env_unset("HAX_DISPLAY_WIDTH");
}

static void test_display_width_env_override(void)
{
    int terminal = term_width();
    if (terminal < 20)
        terminal = 20;
    t_env_set("HAX_DISPLAY_WIDTH", "terminal");
    EXPECT(display_width() == terminal);
    t_env_set("HAX_DISPLAY_WIDTH", "TERMINAL");
    EXPECT(display_width() == terminal);

    /* An exact width bypasses both terminal detection and the soft cap. */
    t_env_set("HAX_DISPLAY_WIDTH", "120");
    EXPECT(display_width() == 120);
    t_env_set("HAX_DISPLAY_WIDTH", "60");
    EXPECT(display_width() == 60);
    t_env_set("HAX_DISPLAY_WIDTH", "500");
    EXPECT(display_width() == 500);

    int automatic = auto_display_width(term_width());
    /* Out-of-range, malformed, and overflowing values fall back to auto. */
    t_env_set("HAX_DISPLAY_WIDTH", "5");
    EXPECT(display_width() == automatic);
    t_env_set("HAX_DISPLAY_WIDTH", "abc");
    EXPECT(display_width() == automatic);
    t_env_set("HAX_DISPLAY_WIDTH", "80x");
    EXPECT(display_width() == automatic);
    t_env_set("HAX_DISPLAY_WIDTH", "999999999999999999999999999");
    EXPECT(display_width() == automatic);
    t_env_unset("HAX_DISPLAY_WIDTH");
}

static void test_reflow_physical_rows(void)
{
    int fitting[] = {10, 80, 0};
    EXPECT(reflow_physical_rows(fitting, 3, 80) == 3);

    int wrapping[] = {81, 160, 161};
    EXPECT(reflow_physical_rows(wrapping, 3, 80) == 7);

    int narrow[] = {3};
    EXPECT(reflow_physical_rows(narrow, 1, 1) == 3);

    EXPECT(reflow_physical_rows(NULL, 0, 80) == 0);
}

int main(void)
{
    test_auto_display_width();
    test_display_width_auto();
    test_display_width_env_override();
    test_reflow_physical_rows();

    T_REPORT();
}
