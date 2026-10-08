/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "xalloc.h"
#ifdef _WIN32
#include "system/win_utf8.h"
#endif

#ifdef _WIN32
#define PATH_SEPARATOR ";"
#else
#define PATH_SEPARATOR ":"
#endif
#define TEST_PATH "/one" PATH_SEPARATOR "/two"

static int path_is(const char *want)
{
#ifdef _WIN32
    char *path = win_utf8_getenv_value("PATH");
#else
    char *path = xstrdup(getenv("PATH"));
#endif
    int equal = path && strcmp(path, want) == 0;
    free(path);
    return equal;
}

static void test_path_replace_round_trips_unset(void)
{
    char *original = t_path_replace(TEST_PATH);
    EXPECT(path_is(TEST_PATH));

    char *before_unset = t_path_replace(NULL);
    EXPECT(getenv("PATH") == NULL);
    EXPECT_STR_EQ(before_unset, TEST_PATH);

    /* Saved from an unset PATH: restore must unset again, not install "". */
    char *from_unset = t_path_replace("/stub");
    EXPECT(from_unset == NULL);
    EXPECT(path_is("/stub"));
    t_path_restore(from_unset);
    EXPECT(getenv("PATH") == NULL);

    t_path_restore(before_unset);
    EXPECT(path_is(TEST_PATH));
    t_path_restore(original);
}

static void test_path_prepend_shadows_without_trailing_colon(void)
{
    char *original = t_path_replace(TEST_PATH);

    char *before = t_path_prepend("/stub");
    EXPECT(path_is("/stub" PATH_SEPARATOR TEST_PATH));
    EXPECT_STR_EQ(before, TEST_PATH);
    t_path_restore(before);
    EXPECT(path_is(TEST_PATH));

    /* Nothing to keep behind the stub: "/stub:" would also search the current directory. */
    char *before_unset = t_path_replace(NULL);
    char *from_unset = t_path_prepend("/stub");
    EXPECT(from_unset == NULL);
    EXPECT(path_is("/stub"));
    t_path_restore(from_unset);
    EXPECT(getenv("PATH") == NULL);
    t_path_restore(before_unset);

    char *before_empty = t_path_replace("");
    char *from_empty = t_path_prepend("/stub");
    EXPECT(path_is("/stub"));
    EXPECT_STR_EQ(from_empty, "");
    t_path_restore(from_empty);
    EXPECT(path_is(""));
    t_path_restore(before_empty);

    EXPECT(path_is(TEST_PATH));
    t_path_restore(original);
}

static void test_unicode_path_round_trip(void)
{
    const char *unicode = "/tools-\xe6\x96\x87" PATH_SEPARATOR "/tools-\xc3\xa9";
    char *original = t_path_replace(unicode);
    EXPECT(path_is(unicode));
    char *saved = t_path_prepend("/first-\xf0\x9f\x98\x80");
    EXPECT_STR_EQ(saved, unicode);
    EXPECT(path_is("/first-\xf0\x9f\x98\x80" PATH_SEPARATOR "/tools-\xe6\x96\x87" PATH_SEPARATOR
                   "/tools-\xc3\xa9"));
    t_path_restore(saved);
    EXPECT(path_is(unicode));
    t_path_restore(original);
}

int main(void)
{
    test_path_replace_round_trips_unset();
    test_path_prepend_shadows_without_trailing_colon();
    test_unicode_path_round_trip();
    T_REPORT();
}
