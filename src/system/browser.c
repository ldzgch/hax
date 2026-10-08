/* SPDX-License-Identifier: MIT */
#include "system/browser.h"

#include <stddef.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <stdlib.h>

#include "system/win_utf8.h"
#else
#include "system/spawn.h"
#endif

void browser_open_url(const char *url)
{
#ifdef _WIN32
    wchar_t *wide = win_utf8_to_wide(url);
    if (!wide)
        return;
    ShellExecuteW(NULL, L"open", wide, NULL, NULL, SW_SHOWNORMAL);
    free(wide);
#else
    /* xdg-open's generic fallback execs the browser directly and lives as long as it, which is
     * why the spawn must be detached rather than waited on or killed. */
#ifdef __APPLE__
    const char *argv[] = {"open", url, NULL};
#else
    const char *argv[] = {"xdg-open", url, NULL};
#endif
    spawn_detached(argv);
#endif
}
