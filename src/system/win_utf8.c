/* SPDX-License-Identifier: MIT */
#include "system/win_utf8.h"

#include <windows.h>
#include <errno.h>
#include <stdlib.h>
#include <wchar.h>

#include "xalloc.h"

wchar_t *win_utf8_to_wide(const char *text)
{
    if (!text) {
        errno = EINVAL;
        return NULL;
    }
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (!count) {
        errno = EILSEQ;
        return NULL;
    }
    wchar_t *wide = xcalloc((size_t)count, sizeof(*wide));
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, count)) {
        free(wide);
        errno = EILSEQ;
        return NULL;
    }
    return wide;
}

wchar_t *win_utf8_path_to_wide(const char *path)
{
    wchar_t *wide = win_utf8_to_wide(path);
    if (!wide)
        return NULL;
    if (wcsncmp(wide, L"\\\\?\\", 4) == 0 || wcsncmp(wide, L"\\\\.\\", 4) == 0)
        return wide;

    int unc = wcsncmp(wide, L"\\\\", 2) == 0;
    int drive_absolute =
        ((wide[0] >= L'A' && wide[0] <= L'Z') || (wide[0] >= L'a' && wide[0] <= L'z')) &&
        wide[1] == L':' && (wide[2] == L'\\' || wide[2] == L'/');
    if (unc || drive_absolute) {
        size_t path_length = wcslen(wide);
        wchar_t *native = xcalloc(path_length + (unc ? 7 : 5), sizeof(*native));
        if (unc) {
            wcscpy(native, L"\\\\?\\UNC\\");
            wcscat(native, wide + 2);
        } else {
            wcscpy(native, L"\\\\?\\");
            wcscat(native, wide);
        }
        for (wchar_t *cursor = native; *cursor; cursor++)
            if (*cursor == L'/')
                *cursor = L'\\';
        free(wide);
        return native;
    }

    const wchar_t *name = wcsrchr(wide, L'\\');
    const wchar_t *slash = wcsrchr(wide, L'/');
    if (!name || (slash && slash > name))
        name = slash;
    name = name ? name + 1 : wide;
    if (_wcsicmp(name, L"CON") == 0 || _wcsicmp(name, L"PRN") == 0 || _wcsicmp(name, L"AUX") == 0 ||
        _wcsicmp(name, L"NUL") == 0 || _wcsicmp(name, L"CONIN$") == 0 ||
        _wcsicmp(name, L"CONOUT$") == 0 ||
        (wcslen(name) == 4 &&
         (_wcsnicmp(name, L"COM", 3) == 0 || _wcsnicmp(name, L"LPT", 3) == 0) && name[3] >= L'1' &&
         name[3] <= L'9'))
        return wide;

    DWORD length = GetFullPathNameW(wide, 0, NULL, NULL);
    if (!length) {
        free(wide);
        errno = EINVAL;
        return NULL;
    }
    wchar_t *absolute = xcalloc((size_t)length + 1, sizeof(*absolute));
    DWORD written = GetFullPathNameW(wide, length + 1, absolute, NULL);
    free(wide);
    if (!written || written > length) {
        free(absolute);
        errno = ENAMETOOLONG;
        return NULL;
    }

    wchar_t *native;
    if (wcsncmp(absolute, L"\\\\", 2) == 0) {
        native = xcalloc((size_t)written + 7, sizeof(*native));
        wcscpy(native, L"\\\\?\\UNC\\");
        wcscat(native, absolute + 2);
    } else {
        native = xcalloc((size_t)written + 5, sizeof(*native));
        wcscpy(native, L"\\\\?\\");
        wcscat(native, absolute);
    }
    free(absolute);
    return native;
}

char *win_utf8_from_wide(const wchar_t *text)
{
    if (!text) {
        errno = EINVAL;
        return NULL;
    }
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, NULL, 0, NULL, NULL);
    if (!count) {
        errno = EILSEQ;
        return NULL;
    }
    char *utf8 = xmalloc((size_t)count);
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, utf8, count, NULL, NULL)) {
        free(utf8);
        errno = EILSEQ;
        return NULL;
    }
    return utf8;
}

char *win_utf8_getenv_value(const char *name)
{
    wchar_t *wide_name = win_utf8_to_wide(name);
    if (!wide_name)
        return NULL;
    SetLastError(ERROR_SUCCESS);
    DWORD capacity = GetEnvironmentVariableW(wide_name, NULL, 0);
    wchar_t *wide_value = NULL;
    char *value = NULL;
    if (!capacity && GetLastError() == ERROR_SUCCESS)
        value = xstrdup("");
    while (capacity) {
        free(wide_value);
        wide_value = xcalloc(capacity, sizeof(*wide_value));
        SetLastError(ERROR_SUCCESS);
        DWORD count = GetEnvironmentVariableW(wide_name, wide_value, capacity);
        if (!count) {
            if (GetLastError() == ERROR_SUCCESS)
                value = xstrdup("");
            break;
        }
        if (count < capacity) {
            value = win_utf8_from_wide(wide_value);
            goto out;
        }
        /* A foreground environment update may change the required capacity between reads. */
        capacity = count;
    }
    errno = 0;
out:
    free(wide_value);
    free(wide_name);
    return value;
}

char *win_utf8_getenv(const char *name)
{
    char *value = win_utf8_getenv_value(name);
    if (value && !*value) {
        free(value);
        return NULL;
    }
    return value;
}
