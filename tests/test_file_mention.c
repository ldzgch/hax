/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif
#ifdef _WIN32
/* The Windows API headers depend on this umbrella header's target declarations. */
#include <windows.h> // IWYU pragma: keep
#include <libloaderapi.h>
#include <minwindef.h>
#include <wchar.h>
#endif

#include "env.h"
#include "file_mention.h"
#include "harness.h"
#include "xalloc.h"
#include "system/path.h"
#include "terminal/input_core.h"
#include "text/shell_quote.h"
#ifdef _WIN32
#include "system/win_utf8.h"
#endif

static void write_file(const char *path, const char *contents, unsigned mode)
{
    FILE *file = fopen(path, "wb");

    EXPECT(file != NULL);
    if (!file)
        return;
    EXPECT(fputs(contents, file) != EOF);
    EXPECT(fclose(file) == 0);
#ifndef _WIN32
    EXPECT(chmod(path, mode) == 0);
#else
    (void)mode;
#endif
}

static void test_command_candidate_sources(void)
{
    char *command = file_mention_build_fzf_command("src");

    EXPECT(strstr(command, "git ls-files -z --cached --others --exclude-standard") != NULL);
    EXPECT(strstr(command, "|| find .") != NULL);
    EXPECT(strstr(command, "-name .git") != NULL);
    EXPECT(strncmp(command, "{ ", 2) == 0);
    EXPECT(strstr(command, "; } | fzf ") != NULL);
    EXPECT(strstr(command, "--query='src'") != NULL);
    EXPECT(strstr(command, "cd ") == NULL);
    free(command);
}

static void test_command_uses_nul_records(void)
{
    char *command = file_mention_build_fzf_command("");

    EXPECT(strstr(command, "git ls-files -z") != NULL);
    EXPECT(strstr(command, "-print0") != NULL);
    EXPECT(strstr(command, "--read0 --print0") != NULL);
    free(command);
}

static void test_command_quotes_query(void)
{
    char *command = file_mention_build_fzf_command("a b$(x)");

    EXPECT(strstr(command, "--query='a b$(x)'") != NULL);
    free(command);

    command = file_mention_build_fzf_command("it's");
    EXPECT(strstr(command, "--query='it'\\''s'") != NULL);
    free(command);

    command = file_mention_build_fzf_command("");
    EXPECT(strstr(command, "--query=''") != NULL);
    free(command);

    command = file_mention_build_fzf_command(NULL);
    EXPECT(strstr(command, "--query=''") != NULL);
    free(command);
}

static void test_command_parent_and_absolute_queries(void)
{
    char *command = file_mention_build_fzf_command("../");

    EXPECT(strstr(command, "cd '../' 2>/dev/null") != NULL);
    EXPECT(strstr(command, "--query=''") != NULL);
    free(command);

    command = file_mention_build_fzf_command("../foo");
    EXPECT(strstr(command, "cd '../' 2>/dev/null") != NULL);
    EXPECT(strstr(command, "--query='foo'") != NULL);
    free(command);

    command = file_mention_build_fzf_command("..");
    EXPECT(strstr(command, "cd '../' 2>/dev/null") != NULL);
    EXPECT(strstr(command, "--query=''") != NULL);
    free(command);

    command = file_mention_build_fzf_command("../../lib/x");
    EXPECT(strstr(command, "cd '../../lib/' 2>/dev/null") != NULL);
    EXPECT(strstr(command, "--query='x'") != NULL);
    free(command);

    command = file_mention_build_fzf_command("/tmp/x");
    EXPECT(strstr(command, "cd '/tmp/' 2>/dev/null") != NULL);
    EXPECT(strstr(command, "--query='x'") != NULL);
    free(command);

#ifdef _WIN32
    command = file_mention_build_fzf_command("C:\\tmp\\x");
    EXPECT(strstr(command, "cd 'C:\\tmp\\' 2>/dev/null") != NULL);
    EXPECT(strstr(command, "--query='x'") != NULL);
    free(command);

    command = file_mention_build_fzf_command("\\\\server\\share\\x");
    EXPECT(strstr(command, "cd ") != NULL);
    EXPECT(strstr(command, "--query='x'") != NULL);
    free(command);
#endif

    command = file_mention_build_fzf_command("../a b$(x)");
    EXPECT(strstr(command, "cd '../' 2>/dev/null") != NULL);
    EXPECT(strstr(command, "--query='a b$(x)'") != NULL);
    free(command);

    command = file_mention_build_fzf_command("../a b$(x)/file");
    EXPECT(strstr(command, "cd '../a b$(x)/' 2>/dev/null") != NULL);
    EXPECT(strstr(command, "--query='file'") != NULL);
    free(command);
}

static void test_command_expands_home_root(void)
{
    char *expanded_root = path_expand_home("~/src/");
    char *quoted_root = shell_single_quote(expanded_root);
    char *expected_cd = xasprintf("cd %s 2>/dev/null", quoted_root);
    char *command = file_mention_build_fzf_command("~/src/fil");

    EXPECT(strstr(command, expected_cd) != NULL);
    EXPECT(strstr(command, "cd '~") == NULL);
    EXPECT(strstr(command, "--query='fil'") != NULL);

    free(command);
    free(expected_cd);
    free(quoted_root);
    free(expanded_root);
}

static void test_command_keeps_project_queries_in_cwd(void)
{
    char *command = file_mention_build_fzf_command("src/tools/ba");

    EXPECT(strstr(command, "cd ") == NULL);
    EXPECT(strstr(command, "--query='src/tools/ba'") != NULL);
    free(command);

    command = file_mention_build_fzf_command("mispted/file");
    EXPECT(strstr(command, "cd ") == NULL);
    EXPECT(strstr(command, "--query='mispted/file'") != NULL);
    free(command);

    command = file_mention_build_fzf_command("./src/x");
    EXPECT(strstr(command, "cd ") == NULL);
    EXPECT(strstr(command, "--query='./src/x'") != NULL);
    free(command);

    command = file_mention_build_fzf_command("~other/x");
    EXPECT(strstr(command, "cd ") == NULL);
    EXPECT(strstr(command, "--query='~other/x'") != NULL);
    free(command);
}

static void test_pick_reads_and_rejoins_nul_record(void)
{
    static const char FZF_SCRIPT[] = "#!/bin/sh\n"
                                     "printf '%s\\000' \"$HAX_TEST_FZF_SELECTION\"\n";
    char *dir = t_tempdir();
#ifdef _WIN32
    char *fzf_path = xasprintf("%s/fzf.exe", dir);
#else
    char *fzf_path = xasprintf("%s/fzf", dir);
#endif
#ifdef _WIN32
    char *picked_file = xasprintf("%s/picked file.txt", dir);
    const char *selection = "./picked file.txt";
#else
    char *picked_file = xasprintf("%s/picked\nfile.txt", dir);
    const char *selection = "./picked\nfile.txt";
#endif
    char *query = xasprintf("%s/", dir);

#ifdef _WIN32
    (void)FZF_SCRIPT;
    wchar_t *source = xmalloc(32768 * sizeof(*source));
    DWORD source_length = GetModuleFileNameW(NULL, source, 32768);
    wchar_t *target = win_utf8_to_wide(fzf_path);
    EXPECT(source_length > 0 && source_length < 32768 && target != NULL);
    if (source_length > 0 && source_length < 32768 && target)
        EXPECT(CopyFileW(source, target, FALSE));
    free(target);
    free(source);
#else
    write_file(fzf_path, FZF_SCRIPT, 0755);
#endif
    write_file(picked_file, "contents", 0644);
    /* Prepended rather than replacing PATH: the stub still shadows any real fzf, but the system
     * directories stay reachable for the utilities it runs. printf is a builtin in dash and
     * FreeBSD's sh, but not in the ksh that OpenBSD installs as /bin/sh. */
    char *saved_path = t_path_prepend(dir);
    t_env_set("HAX_TEST_FZF_SELECTION", selection);

    EXPECT(file_mention_available() == 1);
    char *picked = file_mention_pick(query);
    EXPECT(picked != NULL);
    if (picked)
        EXPECT_STR_EQ(picked, picked_file);

    free(picked);
    t_env_unset("HAX_TEST_FZF_SELECTION");
    t_path_restore(saved_path);
    free(query);
    free(picked_file);
    free(fzf_path);
}

static int match_mention(const char *buffer, size_t len, size_t cursor, size_t *start, size_t *end)
{
    return file_mention_completer.match(buffer, len, cursor, start, end,
                                        file_mention_completer.user);
}

static void test_completer_matches_mentions(void)
{
    size_t start = 999;
    size_t end = 999;

    EXPECT(match_mention("@foo", 4, 4, &start, &end) == 1);
    EXPECT(start == 0);
    EXPECT(end == 4);

    EXPECT(match_mention("see @src/m", 10, 10, &start, &end) == 1);
    EXPECT(start == 4);
    EXPECT(end == 10);

    EXPECT(match_mention("x\n@foo", 6, 6, &start, &end) == 1);
    EXPECT(start == 2);
    EXPECT(end == 6);

    EXPECT(match_mention("@src/m x", 8, 4, &start, &end) == 1);
    EXPECT(start == 0);
    EXPECT(end == 4);

    EXPECT(match_mention("@", 1, 1, &start, &end) == 1);
    EXPECT(start == 0);
    EXPECT(end == 1);
}

static void test_completer_rejects_non_mentions(void)
{
    size_t start;
    size_t end;

    EXPECT(match_mention("foo@bar", 7, 7, &start, &end) == 0);
    EXPECT(match_mention("@foo", 4, 0, &start, &end) == 0);
    EXPECT(match_mention("a @b", 4, 2, &start, &end) == 0);
    EXPECT(match_mention("hello", 5, 5, &start, &end) == 0);
    EXPECT(match_mention("", 0, 0, &start, &end) == 0);
    EXPECT(match_mention("@a c", 4, 4, &start, &end) == 0);
    EXPECT(match_mention("@a", 2, 3, &start, &end) == 0);
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    (void)argc;
    const char *name = strrchr(argv[0], '\\');
    if (!name)
        name = strrchr(argv[0], '/');
    name = name ? name + 1 : argv[0];
    if (_stricmp(name, "fzf.exe") == 0) {
        const char *selection = getenv("HAX_TEST_FZF_SELECTION");
        if (selection)
            fwrite(selection, 1, strlen(selection), stdout);
        fputc('\0', stdout);
        return fflush(stdout) == 0 ? 0 : 1;
    }
#else
    (void)argc;
    (void)argv;
#endif
    test_command_candidate_sources();
    test_command_uses_nul_records();
    test_command_quotes_query();
    test_command_parent_and_absolute_queries();
    test_command_expands_home_root();
    test_command_keeps_project_queries_in_cwd();
    test_pick_reads_and_rejoins_nul_record();
    test_completer_matches_mentions();
    test_completer_rejects_non_mentions();
    T_REPORT();
}
