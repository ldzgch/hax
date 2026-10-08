/* SPDX-License-Identifier: MIT */
#ifndef _WIN32
#include <langinfo.h>
#endif
#include <locale.h>
#include <stdlib.h>
#include <string.h>

#include "env.h"
#include "harness.h"
#include "xalloc.h"
#include "system/locale.h"

#ifndef _WIN32
/* C.UTF-8, C.utf8 and a bare UTF-8 are all spellings hax may settle on, so ask the C library what a
 * name means rather than matching its text. */
static int names_utf8(const char *locale)
{
    if (!locale)
        return 0;
    char *restore = xstrdup(setlocale(LC_CTYPE, NULL));
    int is_utf8 = setlocale(LC_CTYPE, locale) && strcmp(nl_langinfo(CODESET), "UTF-8") == 0;
    setlocale(LC_CTYPE, restore);
    free(restore);
    return is_utf8;
}

/* Children read the environment, not this process's locale. OpenBSD arrives here by default. */
static void test_locale_override_reaches_the_environment(void)
{
    t_env_unset("LC_ALL");
    t_env_set("LANG", "C");
    t_env_set("LC_CTYPE", "C");

    locale_init_utf8();
    if (!locale_have_utf8())
        T_SKIP("no UTF-8 locale available to switch to");

    EXPECT(names_utf8(getenv("LC_CTYPE")));
    /* The environment carries it, so a child needs nothing further. */
    EXPECT(locale_child_ctype_override() == NULL);
}

/* LC_ALL outranks LC_CTYPE, so an override under it would not reach children anyway, and clearing
 * it would hand every other category to LANG. This process still needs UTF-8 to measure text. */
static void test_locale_defers_to_a_deliberate_lc_all(void)
{
    t_env_set("LANG", "de_DE.UTF-8");
    t_env_set("LC_CTYPE", "C");
    t_env_set("LC_ALL", "C");

    locale_init_utf8();
    if (!locale_have_utf8())
        T_SKIP("no UTF-8 locale available to switch to");

    EXPECT_STR_EQ(getenv("LC_ALL"), "C");
    EXPECT_STR_EQ(getenv("LC_CTYPE"), "C");
    /* Nothing in the environment will decode UTF-8, so a child has to be handed a locale. */
    EXPECT(names_utf8(locale_child_ctype_override()));
}

/* A deliberate LC_ALL survives, because the other categories under it were never in question. */
static void test_locale_leaves_a_utf8_environment_alone(const char *utf8_locale)
{
    t_env_set("LC_ALL", utf8_locale);
    t_env_set("LC_CTYPE", utf8_locale);

    locale_init_utf8();

    EXPECT_STR_EQ(getenv("LC_ALL"), utf8_locale);
}

#endif

#ifdef _WIN32
static void test_native_utf8_preserves_locale_environment(void)
{
    t_env_set("LANG", "C");
    t_env_set("LC_ALL", "C");
    t_env_set("LC_CTYPE", "C");
    locale_init_utf8();
    EXPECT(locale_have_utf8());
    EXPECT(locale_child_ctype_override() == NULL);
    EXPECT_STR_EQ(getenv("LANG"), "C");
    EXPECT_STR_EQ(getenv("LC_ALL"), "C");
    EXPECT_STR_EQ(getenv("LC_CTYPE"), "C");
}
#endif

int main(void)
{
#ifdef _WIN32
    test_native_utf8_preserves_locale_environment();
#else
    test_locale_override_reaches_the_environment();
    /* Whichever name the override proved available; the two are not both present everywhere. Copied
     * because setenv() may reallocate the block the value points into. */
    char *utf8_locale = xstrdup(getenv("LC_CTYPE"));
    if (names_utf8(utf8_locale))
        test_locale_leaves_a_utf8_environment_alone(utf8_locale);
    free(utf8_locale);
    test_locale_defers_to_a_deliberate_lc_all();

#endif
    T_REPORT();
}
