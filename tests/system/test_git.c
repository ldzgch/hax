/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
/* The wait macros are provided by <sys/wait.h> per POSIX; glibc also leaks
 * them through <stdlib.h>, so the include cleaner cannot attribute them. */
#ifndef _WIN32
#include <sys/wait.h> // IWYU pragma: keep
#endif

#include "buf.h"
#include "env.h"
#include "files.h"
#include "harness.h"
#include "process.h"
#include "xalloc.h"
#include "system/fs.h"
#include "system/git.h"
#include "system/spawn.h"

static int git_available(void)
{
    char *path = fs_which("git");
    int found = path != NULL;
    free(path);
    return found;
}

static void run_quiet(const char *command)
{
#ifdef _WIN32
    char *silenced = xasprintf(
        "GIT_TERMINAL_PROMPT=0 GIT_EDITOR=: GIT_SEQUENCE_EDITOR=: %s </dev/null", command);
#else
    char *silenced = xasprintf("%s >/dev/null 2>&1", command);
#endif
    int status = spawn_shell_wait(silenced);
    EXPECT(spawn_status_success(status));
    free(silenced);
}

/* A working directory that git cannot mistake for part of an enclosing repository: the ceiling
 * stops the upward search, so the probe sees exactly what this test built. */
static void enter_tempdir(void)
{
    char *dir = t_tempdir();
    EXPECT(t_chdir(dir) == 0);
    t_env_set("GIT_CEILING_DIRECTORIES", dir);
#ifdef _WIN32
    char *global_config = xasprintf("%s/gitconfig", t_tempdir());
    EXPECT(fs_write_atomic(global_config, "", 0, 0) == 0);
    t_env_set("GIT_CONFIG_GLOBAL", global_config);
    free(global_config);
    t_env_set("GIT_CONFIG_NOSYSTEM", "1");
    t_env_set("GIT_TERMINAL_PROMPT", "0");
    t_env_set("GIT_EDITOR", ":");
    t_env_set("GIT_SEQUENCE_EDITOR", ":");
    t_env_set("GIT_CONFIG_COUNT", "2");
    t_env_set("GIT_CONFIG_KEY_0", "commit.gpgsign");
    t_env_set("GIT_CONFIG_VALUE_0", "false");
    t_env_set("GIT_CONFIG_KEY_1", "core.hooksPath");
    char *hooks_dir = t_tempdir();
    t_env_set("GIT_CONFIG_VALUE_1", hooks_dir);
#else
    t_env_set("GIT_CONFIG_GLOBAL", "/dev/null");
    t_env_set("GIT_CONFIG_SYSTEM", "/dev/null");
#endif
    t_env_set("GIT_AUTHOR_NAME", "hax test");
    t_env_set("GIT_AUTHOR_EMAIL", "test@example.com");
    t_env_set("GIT_COMMITTER_NAME", "hax test");
    t_env_set("GIT_COMMITTER_EMAIL", "test@example.com");
}

static void init_repo(void)
{
    run_quiet("git init -q");
    EXPECT(fs_entry_exists(".git") == 1);
    /* Not `git init -b`: older git rejects the flag, and the branch name must be predictable. */
    run_quiet("git symbolic-ref HEAD refs/heads/topic");
}

static void test_outside_repository(void)
{
    if (!git_available())
        T_SKIP("git not installed");
    enter_tempdir();

    struct git_state state;
    git_state_probe(&state);
    EXPECT(state.branch == NULL);
    EXPECT(state.commit == NULL);
    EXPECT(state.subject == NULL);
    git_state_free(&state);
}

static void test_commit_is_described(void)
{
    if (!git_available())
        T_SKIP("git not installed");
    enter_tempdir();
    init_repo();
    run_quiet("echo hello > file.txt");
    run_quiet("git add file.txt");
    run_quiet("git -c commit.gpgsign=false commit -q -m 'Add the first file' "
              "-m 'Body text ignored'");

    struct git_state state;
    git_state_probe(&state);
    EXPECT(state.branch && strcmp(state.branch, "topic") == 0);
    EXPECT(state.subject && strcmp(state.subject, "Add the first file") == 0);
    EXPECT(state.commit != NULL);
    if (state.commit)
        EXPECT(strlen(state.commit) >= 7 && strchr(state.commit, '\n') == NULL);
    git_state_free(&state);
}

static void test_unborn_branch_has_no_commit(void)
{
    if (!git_available())
        T_SKIP("git not installed");
    enter_tempdir();
    init_repo();

    struct git_state state;
    git_state_probe(&state);
    EXPECT(state.branch && strcmp(state.branch, "topic") == 0);
    EXPECT(state.commit == NULL);
    EXPECT(state.subject == NULL);
    git_state_free(&state);
}

static void test_detached_head_has_no_branch(void)
{
    if (!git_available())
        T_SKIP("git not installed");
    enter_tempdir();
    init_repo();
    run_quiet("echo hello > file.txt");
    run_quiet("git add file.txt");
    run_quiet("git -c commit.gpgsign=false commit -q -m 'Add the first file'");
    run_quiet("git checkout -q --detach HEAD");

    struct git_state state;
    git_state_probe(&state);
    EXPECT(state.branch == NULL);
    EXPECT(state.subject && strcmp(state.subject, "Add the first file") == 0);
    git_state_free(&state);
}

/* A stub git on PATH that only records that it ran, by creating `marker`. Returns the saved PATH
 * for t_path_restore. */
static char *prepend_recording_git(const char *marker)
{
    char *dir = t_tempdir();
#ifdef _WIN32
    char *path = xasprintf("%s/git.exe", dir);
    char *program = t_program_path(NULL);
    size_t length = 0;
    char *body = fs_read_file(program, &length);
    EXPECT(body != NULL);
    if (body)
        EXPECT(fs_write_atomic(path, body, length, 0) == 0);
    free(body);
    free(program);
    t_env_set("HAX_TEST_RECORDING_GIT", marker);
#else
    char *path = xasprintf("%s/git", dir);
    FILE *script = fopen(path, "w");
    EXPECT(script != NULL);
    if (script) {
        fprintf(script, "#!/bin/sh\n: > '%s'\n", marker);
        fclose(script);
    }
    EXPECT(chmod(path, 0755) == 0);
#endif
    free(path);
    return t_path_prepend(dir);
}

static void test_probe_runs_git_only_where_a_repository_may_be(void)
{
    enter_tempdir();
    char *marker = xasprintf("%s/ran", t_tempdir());
    char *saved_path = prepend_recording_git(marker);

    struct git_state state;
    git_state_probe(&state);
    EXPECT(access(marker, F_OK) != 0);
    git_state_free(&state);

    /* GIT_DIR may name a repository anywhere. */
    t_env_set("GIT_DIR", "/nonexistent");
    git_state_probe(&state);
    t_env_unset("GIT_DIR");
    EXPECT(access(marker, F_OK) == 0);
    git_state_free(&state);

    t_path_restore(saved_path);
    t_env_unset("HAX_TEST_RECORDING_GIT");
    free(marker);
}

static void test_worktree_root_found_from_subdirectory(void)
{
    char *root = t_tempdir();
    char *marker = xasprintf("%s/.git", root);
    char *nested = xasprintf("%s/a/b", root);
    EXPECT(fs_mkdir_p(marker) == 0);
    EXPECT(fs_mkdir_p(nested) == 0);

    char *found = git_find_worktree_root(nested);
    EXPECT(found != NULL);
    if (found)
        EXPECT_STR_EQ(found, root);
    free(found);
    free(nested);
    free(marker);
}

static void test_worktree_root_marker_may_be_a_file(void)
{
    /* A linked worktree's .git is a file pointing at the main repository. */
    char *root = t_tempdir();
    char *marker = xasprintf("%s/.git", root);
    FILE *file = fopen(marker, "w");
    EXPECT(file != NULL);
    if (file)
        fclose(file);

    char *found = git_find_worktree_root(root);
    EXPECT(found != NULL);
    if (found)
        EXPECT_STR_EQ(found, root);
    free(found);
    free(marker);
}

static void test_worktree_root_marker_symlink_is_not_followed(void)
{
    char *root = t_tempdir();
    char *marker = xasprintf("%s/.git", root);
    int linked = t_symlink("/nonexistent/hax-test-git-dir", marker, 0) == 0;
    int link_errno = errno;
    if (!linked) {
        free(marker);
        if (link_errno == EPERM)
            T_SKIP("symlink creation requires Developer Mode or symlink privilege");
        FAIL("cannot create symlink: %s", strerror(link_errno));
        return;
    }

    char *found = git_find_worktree_root(root);
    EXPECT(found != NULL);
    if (found)
        EXPECT_STR_EQ(found, root);
    free(found);
    free(marker);
}

static void test_worktree_root_found_from_deep_subdirectory(void)
{
    char *root = t_tempdir();
    char *marker = xasprintf("%s/.git", root);
    EXPECT(fs_mkdir_p(marker) == 0);
    struct buf nested;
    buf_init(&nested);
    buf_append_str(&nested, root);
    for (int depth = 0; depth < 100; depth++)
        buf_append_str(&nested, "/d");
    EXPECT(fs_mkdir_p(nested.data) == 0);

    char *found = git_find_worktree_root(nested.data);
    EXPECT(found != NULL);
    if (found)
        EXPECT_STR_EQ(found, root);
    free(found);
    buf_free(&nested);
    free(marker);
}

static void test_worktree_root_absent_outside_repository(void)
{
    EXPECT(git_find_worktree_root(t_tempdir()) == NULL);
}

int main(void)
{
    const char *marker = getenv("HAX_TEST_RECORDING_GIT");
    if (marker)
        return fs_write_atomic(marker, "", 0, 0) < 0;
    test_worktree_root_found_from_subdirectory();
    test_worktree_root_marker_may_be_a_file();
    test_worktree_root_marker_symlink_is_not_followed();
    test_worktree_root_found_from_deep_subdirectory();
    test_worktree_root_absent_outside_repository();
    test_probe_runs_git_only_where_a_repository_may_be();
    test_outside_repository();
    test_commit_is_described();
    test_unborn_branch_has_no_commit();
    test_detached_head_has_no_branch();
    T_REPORT();
}
