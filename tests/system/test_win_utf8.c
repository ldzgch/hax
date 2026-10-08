/* SPDX-License-Identifier: MIT */
#include <windows.h>
#include <errno.h>
#include <stdlib.h>
#include <wchar.h>

#include "harness.h"
#include "system/win_utf8.h"

static void test_roundtrip(void)
{
    const char *text = "C:/Users/Jos\xc3\xa9/\xe6\x96\x87\xf0\x9f\x90\xb1.txt";
    wchar_t *wide = win_utf8_to_wide(text);
    EXPECT(wide != NULL);
    if (!wide)
        return;
    char *result = win_utf8_from_wide(wide);
    EXPECT(result != NULL);
    if (result)
        EXPECT_STR_EQ(result, text);
    free(result);
    free(wide);
}

static void test_path_conversion(void)
{
    wchar_t *drive = win_utf8_path_to_wide("C:/Users/Jos\xc3\xa9/deep");
    EXPECT(drive != NULL);
    EXPECT(wcsncmp(drive, L"\\\\?\\C:\\Users\\Jos\u00e9\\deep", 21) == 0);
    free(drive);

    wchar_t *unc = win_utf8_path_to_wide("\\\\server\\share\\folder");
    EXPECT(unc != NULL);
    EXPECT(wcsncmp(unc, L"\\\\?\\UNC\\server\\share\\folder", 27) == 0);
    free(unc);

    wchar_t *device = win_utf8_path_to_wide("NUL");
    EXPECT(device != NULL);
    EXPECT(wcscmp(device, L"NUL") == 0);
    free(device);
}

static void test_empty(void)
{
    wchar_t *wide = win_utf8_to_wide("");
    EXPECT(wide != NULL);
    if (wide)
        EXPECT(wide[0] == 0);
    char *result = win_utf8_from_wide(L"");
    EXPECT(result != NULL);
    if (result)
        EXPECT_STR_EQ(result, "");
    free(result);
    free(wide);
}

static void test_invalid_utf8(void)
{
    errno = 0;
    EXPECT(win_utf8_to_wide("\xc0\xaf") == NULL);
    EXPECT(errno == EILSEQ);
    errno = 0;
    EXPECT(win_utf8_to_wide("\xf0\x9f") == NULL);
    EXPECT(errno == EILSEQ);
}

static void test_invalid_utf16(void)
{
    const wchar_t unpaired[] = {0xd800, 0};
    errno = 0;
    EXPECT(win_utf8_from_wide(unpaired) == NULL);
    EXPECT(errno == EILSEQ);
}

static void test_null(void)
{
    errno = 0;
    EXPECT(win_utf8_to_wide(NULL) == NULL);
    EXPECT(errno == EINVAL);
    errno = 0;
    EXPECT(win_utf8_from_wide(NULL) == NULL);
    EXPECT(errno == EINVAL);
}

static void test_unicode_environment(void)
{
    const wchar_t *name = L"HAX_TEST_WIN_UTF8_ENV";
    EXPECT(SetEnvironmentVariableW(name, L"C:\\Users\\Jos\u00e9\\\u6587"));
    char *value = win_utf8_getenv("HAX_TEST_WIN_UTF8_ENV");
    EXPECT(value != NULL);
    if (value)
        EXPECT_STR_EQ(value, "C:\\Users\\Jos\xc3\xa9\\\xe6\x96\x87");
    free(value);
    EXPECT(SetEnvironmentVariableW(name, L""));
    value = win_utf8_getenv_value("HAX_TEST_WIN_UTF8_ENV");
    EXPECT(value != NULL);
    if (value)
        EXPECT_STR_EQ(value, "");
    free(value);
    EXPECT(win_utf8_getenv("HAX_TEST_WIN_UTF8_ENV") == NULL);
    EXPECT(SetEnvironmentVariableW(name, NULL));
    errno = EINVAL;
    EXPECT(win_utf8_getenv("HAX_TEST_WIN_UTF8_ENV") == NULL);
    EXPECT(errno == 0);
    EXPECT(win_utf8_getenv(NULL) == NULL);
    EXPECT(errno == EINVAL);
}

int main(void)
{
    test_roundtrip();
    test_path_conversion();
    test_empty();
    test_invalid_utf8();
    test_invalid_utf16();
    test_null();
    test_unicode_environment();
    T_REPORT();
}
