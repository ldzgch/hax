/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "env.h"
#include "harness.h"
#include "tools/bash_env.h"

static const char *value_of(char *const *environment, const char *name)
{
    size_t length = strlen(name);
    for (size_t i = 0; environment[i]; i++) {
        if (strncmp(environment[i], name, length) == 0 && environment[i][length] == '=')
            return environment[i] + length + 1;
    }
    return NULL;
}

static void expect_value(char *const *environment, const char *name, const char *expected)
{
    const char *value = value_of(environment, name);
    EXPECT(value != NULL);
    if (value)
        EXPECT_STR_EQ(value, expected);
}

static void test_owned_snapshot_and_overrides(void)
{
    t_env_set("HAX_TEST_OWNED", "before");
    t_env_set("HAX_TRACE", "parent-trace");
    t_env_set("PAGER", "parent-pager");
    bash_env_set_selection("openai",
                           "m\xc3\xb8"
                           "del",
                           "high");
    char **first = bash_build_child_env();
    EXPECT(first != NULL);
    if (!first)
        return;
    expect_value(first, "HAX_TEST_OWNED", "before");
    expect_value(first, "HAX_TRACE", "");
    expect_value(first, "PAGER", "cat");
    expect_value(first, "GIT_EDITOR", "false");
    expect_value(first, "HAX_PROVIDER", "openai");
    expect_value(first, "HAX_MODEL",
                 "m\xc3\xb8"
                 "del");
    t_env_set("HAX_TEST_OWNED", "after");
    bash_env_set_selection("next", "other", "low");
    char **second = bash_build_child_env();
    EXPECT(second != NULL);
    if (second) {
        expect_value(second, "HAX_TEST_OWNED", "after");
        expect_value(second, "HAX_PROVIDER", "next");
        free(second);
    }
    expect_value(first, "HAX_TEST_OWNED", "before");
    expect_value(first, "HAX_MODEL",
                 "m\xc3\xb8"
                 "del");
    EXPECT_STR_EQ(getenv("HAX_TRACE"), "parent-trace");
    EXPECT_STR_EQ(getenv("PAGER"), "parent-pager");
    free(first);
    bash_env_set_selection(NULL, NULL, NULL);
}

#ifdef _WIN32
static void test_native_unicode_and_case(void)
{
    EXPECT(SetEnvironmentVariableW(L"HAX_TEST_NATIVE_UNICODE", L"\x00e9-\xd83d\xde00"));
    EXPECT(SetEnvironmentVariableW(L"gIt_EdItOr", L"parent-editor"));
    char **environment = bash_build_child_env();
    EXPECT(environment != NULL);
    if (environment) {
        expect_value(environment, "HAX_TEST_NATIVE_UNICODE", "\xc3\xa9-\xf0\x9f\x98\x80");
        expect_value(environment, "GIT_EDITOR", "false");
        int matches = 0;
        for (size_t i = 0; environment[i]; i++)
            if (_strnicmp(environment[i], "GIT_EDITOR=", 11) == 0)
                matches++;
        EXPECT(matches == 1);
        free(environment);
    }
    EXPECT(SetEnvironmentVariableW(L"HAX_TEST_NATIVE_UNICODE", NULL));
}
#endif

int main(void)
{
    test_owned_snapshot_and_overrides();
#ifdef _WIN32
    test_native_unicode_and_case();
#endif
    T_REPORT();
}
