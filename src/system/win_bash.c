/* SPDX-License-Identifier: MIT */
#include "system/win_bash.h"

#include <windows.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "xalloc.h"
#include "system/fs.h"
#include "system/path.h"
#include "system/win_utf8.h"

static char *bash_in_installation(const char *directory)
{
    const char *locations[] = {"bin/bash.exe", "usr/bin/bash.exe"};
    for (size_t i = 0; i < sizeof(locations) / sizeof(*locations); i++) {
        char *path = path_join(directory, locations[i]);
        if (fs_check_regular(path) == 0)
            return path;
        free(path);
    }
    return NULL;
}

static int is_wsl_launcher(const char *path)
{
    wchar_t directory[32768];
    UINT length = GetSystemDirectoryW(directory, sizeof(directory) / sizeof(*directory));
    if (!length || length >= sizeof(directory) / sizeof(*directory))
        return 1;
    char *system = win_utf8_from_wide(directory);
    if (!system)
        return 1;
    char *launcher = path_join(system, "bash.exe");
    free(system);
    char *normalized = xstrdup(path);
    for (char *p = normalized; *p; p++)
        if (*p == '/')
            *p = '\\';
    for (char *p = launcher; *p; p++)
        if (*p == '/')
            *p = '\\';
    int matches = _stricmp(normalized, launcher) == 0;
    free(normalized);
    free(launcher);
    return matches;
}

char *win_bash_path(void)
{
    char *git = fs_which("git.exe");
    if (git) {
        char *bash = NULL;
        for (int depth = 0; depth < 3 && path_climb_to_parent(git); depth++) {
            bash = bash_in_installation(git);
            if (bash)
                break;
        }
        free(git);
        if (bash)
            return bash;
    }
    char *bash = fs_which("bash.exe");
    if (bash && !is_wsl_launcher(bash))
        return bash;
    free(bash);
    const char *variables[] = {"ProgramFiles", "ProgramFiles(x86)", "LOCALAPPDATA"};
    for (size_t i = 0; i < sizeof(variables) / sizeof(*variables); i++) {
        char *base = win_utf8_getenv(variables[i]);
        if (!base)
            continue;
        char *installation = path_join(base, i == 2 ? "Programs/Git" : "Git");
        free(base);
        bash = bash_in_installation(installation);
        free(installation);
        if (bash)
            return bash;
    }
    errno = ENOENT;
    return NULL;
}
