/* SPDX-License-Identifier: MIT */
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "env.h"
#include "harness.h"
#include "system/path.h"
#ifdef _WIN32
#include "system/win_utf8.h"
#endif

static void test_current_directory(void)
{
    char *cwd = path_cwd();
    EXPECT(cwd != NULL);
    if (cwd)
        EXPECT(path_is_absolute(cwd));
#ifdef _WIN32
    wchar_t *original = cwd ? win_utf8_to_wide(cwd) : NULL;
    char *unicode = path_join(t_tempdir(), "cwd-\xc3\xa9-\xf0\x9f\x98\x80");
    wchar_t *wide = win_utf8_to_wide(unicode);
    EXPECT(original != NULL && wide != NULL);
    if (original && wide) {
        EXPECT(CreateDirectoryW(wide, NULL));
        EXPECT(SetCurrentDirectoryW(wide));
        char *actual = path_cwd();
        wchar_t native[MAX_PATH];
        DWORD length = GetCurrentDirectoryW(MAX_PATH, native);
        EXPECT(length > 0 && length < MAX_PATH);
        char *expected = length > 0 && length < MAX_PATH ? win_utf8_from_wide(native) : NULL;
        EXPECT(actual != NULL && expected != NULL);
        if (actual && expected)
            EXPECT_STR_EQ(actual, expected);
        free(expected);
        free(actual);
        EXPECT(SetCurrentDirectoryW(original));
    }
    free(wide);
    free(original);
    free(unicode);
#endif
    free(cwd);
}

static void test_path_join_simple(void)
{
    char *joined = path_join("/tmp", "foo");
    EXPECT_STR_EQ(joined, "/tmp/foo");
    free(joined);
}

static void test_path_join_strips_trailing_slash(void)
{
    char *joined = path_join("/var/folders/abc/T/", "hax-bash-XXXXXX");
    EXPECT_STR_EQ(joined, "/var/folders/abc/T/hax-bash-XXXXXX");
    free(joined);
}

static void test_path_join_strips_multiple_trailing_slashes(void)
{
    char *joined = path_join("/tmp///", "foo");
    EXPECT_STR_EQ(joined, "/tmp/foo");
    free(joined);
}

static void test_path_join_strips_leading_slash_from_suffix(void)
{
    char *joined = path_join("/tmp", "/foo");
    EXPECT_STR_EQ(joined, "/tmp/foo");
    free(joined);
}

static void test_path_join_root_base(void)
{
    char *joined = path_join("/", "etc");
    EXPECT_STR_EQ(joined, "/etc");
    free(joined);
}

static void test_path_join_root_with_leading_slash_suffix(void)
{
    char *joined = path_join("/", "/etc");
    EXPECT_STR_EQ(joined, "/etc");
    free(joined);
}

static void test_path_join_relative_base(void)
{
    char *joined = path_join("subdir", "file.txt");
    EXPECT_STR_EQ(joined, "subdir/file.txt");
    free(joined);
}

static void test_path_join_dot_base(void)
{
    char *joined = path_join(".", "file.txt");
    EXPECT_STR_EQ(joined, "./file.txt");
    free(joined);
}

static void test_path_join_empty_base(void)
{
    char *joined = path_join("", "foo");
    EXPECT_STR_EQ(joined, "/foo");
    free(joined);
}

static void test_path_join_empty_suffix(void)
{
    char *joined = path_join("/tmp", "");
    EXPECT_STR_EQ(joined, "/tmp/");
    free(joined);
}

static void test_path_expand_home_null(void)
{
    EXPECT(path_expand_home(NULL) == NULL);
}

static void test_path_expand_home_copies_path_without_tilde(void)
{
    t_env_set("HOME", "/tmp/fake");
    char *expanded = path_expand_home("/absolute/path");
    EXPECT_STR_EQ(expanded, "/absolute/path");
    free(expanded);
}

static void test_path_expand_home_bare_tilde(void)
{
    t_env_set("HOME", "/tmp/fake");
    char *expanded = path_expand_home("~");
    EXPECT_STR_EQ(expanded, "/tmp/fake");
    free(expanded);
}

static void test_path_expand_home_tilde_prefix(void)
{
    t_env_set("HOME", "/tmp/fake");
    char *expanded = path_expand_home("~/sub/file");
    EXPECT_STR_EQ(expanded, "/tmp/fake/sub/file");
    free(expanded);
}

static void test_path_expand_home_avoids_duplicate_separator(void)
{
    t_env_set("HOME", "/tmp/fake/");
    char *expanded = path_expand_home("~/sub/file");
    EXPECT_STR_EQ(expanded, "/tmp/fake/sub/file");
    free(expanded);
}

static void test_path_expand_home_root(void)
{
    t_env_set("HOME", "/");
    char *expanded = path_expand_home("~/etc/hosts");
    EXPECT_STR_EQ(expanded, "/etc/hosts");
    free(expanded);
}

static void test_path_expand_home_without_home(void)
{
    t_env_unset("HOME");
    char *expanded = path_expand_home("~/foo");
    EXPECT_STR_EQ(expanded, "~/foo");
    free(expanded);
}

static void test_path_expand_home_with_empty_home(void)
{
    t_env_set("HOME", "");
    char *expanded = path_expand_home("~/foo");
    EXPECT_STR_EQ(expanded, "~/foo");
    free(expanded);
}

static void test_path_expand_home_leaves_named_home_unchanged(void)
{
    t_env_set("HOME", "/tmp/fake");
    char *expanded = path_expand_home("~root/etc");
    EXPECT_STR_EQ(expanded, "~root/etc");
    free(expanded);
}

static void test_path_collapse_home_null(void)
{
    EXPECT(path_collapse_home(NULL) == NULL);
}

static void test_path_collapse_home_prefix(void)
{
    t_env_set("HOME", "/Users/alice");
    char *collapsed = path_collapse_home("/Users/alice/source/hax");
    EXPECT_STR_EQ(collapsed, "~/source/hax");
    free(collapsed);
}

static void test_path_collapse_home_exact_match(void)
{
    t_env_set("HOME", "/Users/alice");
    char *collapsed = path_collapse_home("/Users/alice");
    EXPECT_STR_EQ(collapsed, "~");
    free(collapsed);
}

static void test_path_collapse_home_copies_unrelated_path(void)
{
    t_env_set("HOME", "/Users/alice");
    char *collapsed = path_collapse_home("/etc/hosts");
    EXPECT_STR_EQ(collapsed, "/etc/hosts");
    free(collapsed);
}

static void test_path_collapse_home_requires_component_boundary(void)
{
    t_env_set("HOME", "/Users/alice");
    char *collapsed = path_collapse_home("/Users/alice2/x");
    EXPECT_STR_EQ(collapsed, "/Users/alice2/x");
    free(collapsed);
}

static void test_path_collapse_home_ignores_trailing_home_slash(void)
{
    t_env_set("HOME", "/Users/alice/");
    char *collapsed = path_collapse_home("/Users/alice/foo");
    EXPECT_STR_EQ(collapsed, "~/foo");
    free(collapsed);
}

static void test_path_collapse_home_without_home(void)
{
    t_env_unset("HOME");
    char *collapsed = path_collapse_home("/Users/alice/foo");
    EXPECT_STR_EQ(collapsed, "/Users/alice/foo");
    free(collapsed);
}

static void test_path_collapse_home_with_empty_home(void)
{
    t_env_set("HOME", "");
    char *collapsed = path_collapse_home("/Users/alice/foo");
    EXPECT_STR_EQ(collapsed, "/Users/alice/foo");
    free(collapsed);
}

static void test_path_collapse_root_home(void)
{
    t_env_set("HOME", "/");
    char *collapsed = path_collapse_home("/etc/hosts");
    EXPECT_STR_EQ(collapsed, "~/etc/hosts");
    free(collapsed);
}

static void test_path_collapse_root_home_exact_match(void)
{
    t_env_set("HOME", "/");
    char *collapsed = path_collapse_home("/");
    EXPECT_STR_EQ(collapsed, "~");
    free(collapsed);
}

static void test_path_relativize_descendant(void)
{
    char *relative = path_relativize("/home/u/proj/src/x.c", "/home/u/proj");
    EXPECT_STR_EQ(relative, "src/x.c");
    free(relative);
}

static void test_path_relativize_direct_child(void)
{
    char *relative = path_relativize("/home/u/proj/calc.py", "/home/u/proj");
    EXPECT_STR_EQ(relative, "calc.py");
    free(relative);
}

static void test_path_relativize_rejects_cwd(void)
{
    EXPECT(path_relativize("/home/u/proj", "/home/u/proj") == NULL);
}

static void test_path_relativize_rejects_unrelated_path(void)
{
    EXPECT(path_relativize("/etc/hosts", "/home/u/proj") == NULL);
}

static void test_path_relativize_requires_component_boundary(void)
{
    EXPECT(path_relativize("/home/u/proj2/x", "/home/u/proj") == NULL);
}

static void test_path_relativize_rejects_relative_path(void)
{
    EXPECT(path_relativize("src/x.c", "/home/u/proj") == NULL);
}

static void test_path_relativize_rejects_relative_cwd(void)
{
    EXPECT(path_relativize("/home/u/proj/x.c", "home/u/proj") == NULL);
}

static void test_path_relativize_ignores_trailing_cwd_slash(void)
{
    char *relative = path_relativize("/home/u/proj/x.c", "/home/u/proj/");
    EXPECT_STR_EQ(relative, "x.c");
    free(relative);
}

static void test_path_relativize_from_root(void)
{
    char *relative = path_relativize("/etc/hosts", "/");
    EXPECT_STR_EQ(relative, "etc/hosts");
    free(relative);
}

static void test_path_relativize_rejects_root_from_root(void)
{
    EXPECT(path_relativize("/", "/") == NULL);
}

static void test_path_relativize_rejects_null_inputs(void)
{
    EXPECT(path_relativize(NULL, "/home") == NULL);
    EXPECT(path_relativize("/home/x", NULL) == NULL);
}

static void test_path_relativize_rejects_escaping_parent_component(void)
{
    EXPECT(path_relativize("/repo/../outside/file", "/repo") == NULL);
}

static void test_path_relativize_rejects_non_escaping_parent_component(void)
{
    EXPECT(path_relativize("/repo/a/../b/file", "/repo") == NULL);
}

static void test_path_relativize_rejects_trailing_parent_component(void)
{
    EXPECT(path_relativize("/repo/sub/..", "/repo") == NULL);
}

static void test_path_relativize_accepts_dots_within_component(void)
{
    char *relative = path_relativize("/repo/a..b/file", "/repo");
    EXPECT_STR_EQ(relative, "a..b/file");
    free(relative);
}

static void test_path_climb_to_parent_stops_at_root(void)
{
    char dir[] = "/a/b";
    EXPECT(path_climb_to_parent(dir) == 1);
    EXPECT_STR_EQ(dir, "/a");
    EXPECT(path_climb_to_parent(dir) == 1);
    EXPECT_STR_EQ(dir, "/");
    EXPECT(path_climb_to_parent(dir) == 0);
    EXPECT_STR_EQ(dir, "/");
}

static void test_path_roots(void)
{
    EXPECT(!path_is_absolute(NULL));
    EXPECT(!path_is_absolute(""));
    EXPECT(!path_is_absolute("relative/file"));
    EXPECT(path_root_length("/tmp/file") == 1);
    EXPECT(path_is_separator('/'));
    EXPECT(!path_is_separator('x'));
}

static void test_xdg_paths(void)
{
    t_env_set("XDG_CONFIG_HOME", "/config");
    t_env_set("XDG_STATE_HOME", "/state");
    t_env_set("XDG_CACHE_HOME", "/cache");
    char *config = xdg_hax_config_path("config.json");
    char *state = xdg_hax_state_path("sessions");
    char *cache = xdg_hax_cache_path("models.json");
    EXPECT_STR_EQ(config, "/config/hax/config.json");
    EXPECT_STR_EQ(state, "/state/hax/sessions");
    EXPECT_STR_EQ(cache, "/cache/hax/models.json");
    free(config);
    free(state);
    free(cache);
    t_env_unset("XDG_CONFIG_HOME");
    t_env_unset("XDG_STATE_HOME");
    t_env_unset("XDG_CACHE_HOME");
}

#ifdef _WIN32
static void test_windows_roots(void)
{
    EXPECT(path_root_length("C:\\work") == 3);
    EXPECT(path_root_length("z:/work") == 3);
    EXPECT(!path_is_absolute("C:work"));
    EXPECT(!path_is_absolute("1:/work"));
    EXPECT(path_root_length("\\\\server\\share\\work") == 14);
    EXPECT(path_root_length("//server/share") == 14);
    EXPECT(path_root_length("\\\\?\\C:\\work") == 7);
    EXPECT(path_root_length("\\\\?\\UNC\\server\\share\\work") == 20);
    EXPECT(!path_is_absolute("\\\\server"));
    EXPECT(!path_is_absolute("\\\\server\\"));
    EXPECT(!path_is_absolute("\\\\?\\"));
    EXPECT(path_is_separator('\\'));
}

static void test_windows_join(void)
{
    char *joined = path_join("C:\\", "\\work");
    EXPECT_STR_EQ(joined, "C:\\work");
    free(joined);
    joined = path_join("C:\\work\\\\", "\\\\file");
    EXPECT_STR_EQ(joined, "C:\\work/file");
    free(joined);
    joined = path_join("\\\\server\\share\\", "file");
    EXPECT_STR_EQ(joined, "\\\\server\\share/file");
    free(joined);
}

static void test_windows_relativize(void)
{
    char *relative = path_relativize("c:\\work\\src\\file.c", "C:/work/");
    EXPECT(relative != NULL);
    if (relative)
        EXPECT_STR_EQ(relative, "src\\file.c");
    free(relative);
    relative = path_relativize("C:\\work\\file.c", "C:/");
    EXPECT(relative != NULL);
    if (relative)
        EXPECT_STR_EQ(relative, "work\\file.c");
    free(relative);
    relative = path_relativize("\\\\server\\share\\file", "//server/share/");
    EXPECT(relative != NULL);
    if (relative)
        EXPECT_STR_EQ(relative, "file");
    free(relative);
    EXPECT(path_relativize("C:\\work\\..\\other", "C:\\work") == NULL);
    EXPECT(path_relativize("C:\\work\\a\\..\\b", "C:\\work") == NULL);
    EXPECT(path_relativize("D:\\work\\file", "C:\\work") == NULL);
    EXPECT(path_relativize("C:\\work2\\file", "C:\\work") == NULL);
    EXPECT(path_relativize("C:\\Work\\file", "C:\\work") == NULL);
    EXPECT(path_relativize("\\\\server\\other\\file", "\\\\server\\share") == NULL);
    EXPECT(path_relativize("C:\\", "c:/") == NULL);
}

static void test_windows_parent(void)
{
    char drive[] = "C:\\work\\src";
    EXPECT(path_climb_to_parent(drive));
    EXPECT_STR_EQ(drive, "C:\\work");
    EXPECT(path_climb_to_parent(drive));
    EXPECT_STR_EQ(drive, "C:\\");
    EXPECT(!path_climb_to_parent(drive));
    char unc[] = "\\\\server\\share\\work\\src";
    EXPECT(path_climb_to_parent(unc));
    EXPECT_STR_EQ(unc, "\\\\server\\share\\work");
    EXPECT(path_climb_to_parent(unc));
    EXPECT_STR_EQ(unc, "\\\\server\\share");
    EXPECT(!path_climb_to_parent(unc));
}

static void test_windows_home(void)
{
    t_env_unset("HOME");
    t_env_set("USERPROFILE", "C:\\Users\\alice");
    char *expanded = path_expand_home("~\\work");
    EXPECT_STR_EQ(expanded, "C:\\Users\\alice/work");
    free(expanded);
    char *collapsed = path_collapse_home("c:/Users/alice/work");
    EXPECT_STR_EQ(collapsed, "~/work");
    free(collapsed);
    t_env_set("HOME", "D:/custom");
    expanded = path_expand_home("~/work");
    EXPECT_STR_EQ(expanded, "D:/custom/work");
    free(expanded);
    t_env_set("HOME", "C:\\");
    collapsed = path_collapse_home("c:/work");
    EXPECT_STR_EQ(collapsed, "~/work");
    free(collapsed);
    t_env_unset("USERPROFILE");
}

static void test_windows_appdata(void)
{
    t_env_set("APPDATA", "C:/Roaming");
    t_env_set("LOCALAPPDATA", "C:/Local");
    char *config = xdg_hax_config_path("config.json");
    char *state = xdg_hax_state_path("sessions");
    char *cache = xdg_hax_cache_path("models.json");
    EXPECT_STR_EQ(config, "C:/Roaming/hax/config.json");
    EXPECT_STR_EQ(state, "C:/Local/hax/state/sessions");
    EXPECT_STR_EQ(cache, "C:/Local/hax/cache/models.json");
    free(config);
    free(state);
    free(cache);
    test_xdg_paths();
}

static void test_windows_unicode_home(void)
{
    t_env_unset("HOME");
    EXPECT(SetEnvironmentVariableW(L"USERPROFILE", L"C:\\Users\\Jos\u00e9"));
    char *expanded = path_expand_home("~/work");
    EXPECT_STR_EQ(expanded, "C:\\Users\\Jos\xc3\xa9/work");
    free(expanded);
    EXPECT(SetEnvironmentVariableW(L"USERPROFILE", NULL));
}
#endif

static void test_home_directory(void)
{
    t_env_set("HOME", "/home/\xc3\xa9");
    char *home = path_home();
    EXPECT_STR_EQ(home, "/home/\xc3\xa9");
    t_env_set("HOME", "/different");
    EXPECT_STR_EQ(home, "/home/\xc3\xa9");
    free(home);
    t_env_unset("HOME");
#ifdef _WIN32
    t_env_set("USERPROFILE", "C:/Users/\xe6\x96\x87");
    home = path_home();
    EXPECT_STR_EQ(home, "C:/Users/\xe6\x96\x87");
    free(home);
    t_env_unset("USERPROFILE");
#endif
    EXPECT(path_home() == NULL);
    t_env_set("HOME", "");
    EXPECT(path_home() == NULL);
}

int main(void)
{
    test_current_directory();
#ifdef _WIN32
    t_env_unset("USERPROFILE");
    t_env_unset("APPDATA");
    t_env_unset("LOCALAPPDATA");
#endif
    test_home_directory();
    test_path_join_simple();
    test_path_join_strips_trailing_slash();
    test_path_join_strips_multiple_trailing_slashes();
    test_path_join_strips_leading_slash_from_suffix();
    test_path_join_root_base();
    test_path_join_root_with_leading_slash_suffix();
    test_path_join_relative_base();
    test_path_join_dot_base();
    test_path_join_empty_base();
    test_path_join_empty_suffix();

    test_path_expand_home_null();
    test_path_expand_home_copies_path_without_tilde();
    test_path_expand_home_bare_tilde();
    test_path_expand_home_tilde_prefix();
    test_path_expand_home_avoids_duplicate_separator();
    test_path_expand_home_root();
    test_path_expand_home_without_home();
    test_path_expand_home_with_empty_home();
    test_path_expand_home_leaves_named_home_unchanged();

    test_path_collapse_home_null();
    test_path_collapse_home_prefix();
    test_path_collapse_home_exact_match();
    test_path_collapse_home_copies_unrelated_path();
    test_path_collapse_home_requires_component_boundary();
    test_path_collapse_home_ignores_trailing_home_slash();
    test_path_collapse_home_without_home();
    test_path_collapse_home_with_empty_home();
    test_path_collapse_root_home();
    test_path_collapse_root_home_exact_match();

    test_path_relativize_descendant();
    test_path_relativize_direct_child();
    test_path_relativize_rejects_cwd();
    test_path_relativize_rejects_unrelated_path();
    test_path_relativize_requires_component_boundary();
    test_path_relativize_rejects_relative_path();
    test_path_relativize_rejects_relative_cwd();
    test_path_relativize_ignores_trailing_cwd_slash();
    test_path_relativize_from_root();
    test_path_relativize_rejects_root_from_root();
    test_path_relativize_rejects_null_inputs();
    test_path_relativize_rejects_escaping_parent_component();
    test_path_relativize_rejects_non_escaping_parent_component();
    test_path_relativize_rejects_trailing_parent_component();
    test_path_relativize_accepts_dots_within_component();

    test_path_climb_to_parent_stops_at_root();
    test_path_roots();
    test_xdg_paths();
#ifdef _WIN32
    test_windows_roots();
    test_windows_join();
    test_windows_relativize();
    test_windows_parent();
    test_windows_home();
    test_windows_appdata();
    test_windows_unicode_home();
#endif

    T_REPORT();
}
