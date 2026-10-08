/* SPDX-License-Identifier: MIT */
#include "harness.h"
#include "text/glob.h"

int main(void)
{
    EXPECT(glob_match("gpt-*", "gpt-4.1"));
    EXPECT(!glob_match("gpt-*", "claude-4"));
    EXPECT(!glob_match("GPT-*", "gpt-4.1"));
    EXPECT(glob_match("*a*b", "aaab"));
    EXPECT(glob_match("*a*b", "aaaxab"));
    EXPECT(!glob_match("*a*b", "aaaxac"));
    EXPECT(glob_match("", ""));
    EXPECT(glob_match("**", ""));
    EXPECT(!glob_match("?", ""));
    EXPECT(glob_match("*", ".provider/model"));
    EXPECT(glob_match("model-[a-c]", "model-b"));
    EXPECT(!glob_match("model-[!a-c]", "model-b"));
    EXPECT(glob_match("model-[!a-c]", "model-z"));
    EXPECT(glob_match("[]-]", "]"));
    EXPECT(glob_match("[]-]", "-"));
    EXPECT(glob_match("[abc", "[abc"));
    EXPECT(glob_match("model-\\*", "model-*"));
    EXPECT(!glob_match("model-\\*", "model-foo"));
    EXPECT(!glob_match("model-\\", "model-"));
    EXPECT(glob_match("[[:digit:]][[:alpha:]]", "4a"));
    EXPECT(!glob_match("[[:digit:]][[:alpha:]]", "aa"));
#ifdef _WIN32
    EXPECT(glob_match("?", "\xf0\x9f\x98\x80"));
    EXPECT(glob_match("[\xc3\xa9]", "\xc3\xa9"));
    EXPECT(!glob_match("??", "\xc3\xa9"));
#endif
    EXPECT(!glob_match(NULL, "model"));
    T_REPORT();
}
