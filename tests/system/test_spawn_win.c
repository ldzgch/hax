/* SPDX-License-Identifier: MIT */
#include <windows.h>
#include <errno.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "env.h"
#include "harness.h"
#include "xalloc.h"
#include "system/fs.h"
#include "system/path.h"
#include "system/spawn.h"
#include "system/win_utf8.h"

static char *program;

static void child_write(const char *bytes, size_t length)
{
    DWORD written;
    if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bytes, (DWORD)length, &written, NULL) ||
        written != length)
        ExitProcess(1);
}

static int run_child(int argc, wchar_t **argv)
{
    if (wcscmp(argv[1], L"args") == 0) {
        for (int i = 2; i < argc; i++) {
            char *text = win_utf8_from_wide(argv[i]);
            if (!text)
                return 1;
            child_write(text, strlen(text));
            child_write("\n", 1);
            free(text);
        }
    } else if (wcscmp(argv[1], L"binary") == 0) {
        const char bytes[] = "\r\n\x1a\0\xff";
        child_write(bytes, sizeof(bytes));
    } else if (wcscmp(argv[1], L"failure") == 0) {
        child_write("failure", 7);
        return 42;
    } else if (wcscmp(argv[1], L"sleep") == 0) {
        Sleep(10000);
    } else if (wcscmp(argv[1], L"close-sleep") == 0) {
        CloseHandle(GetStdHandle(STD_OUTPUT_HANDLE));
        Sleep(10000);
    } else if (wcscmp(argv[1], L"marker") == 0) {
        char *path = win_utf8_from_wide(argv[2]);
        int result = fs_write_atomic(path, "detached", 8, 0);
        free(path);
        return result < 0;
    }
    return 0;
}

static void test_capture_arguments(void)
{
    const char *argv[] = {program,
                          "args",
                          "",
                          "two words",
                          "embedded\"quote",
                          "tail\\",
                          "slashes\\\"quote",
                          "\xc3\xa9-\xf0\x9f\x98\x80",
                          NULL};
    size_t length = 0;
    char *output = spawn_capture_stdout(argv, 1024, 2000, &length);
    const char expected[] = "\ntwo words\nembedded\"quote\ntail\\\nslashes\\\"quote\n"
                            "\xc3\xa9-\xf0\x9f\x98\x80\n";
    EXPECT(output != NULL);
    if (output)
        EXPECT_MEM_EQ(output, length, expected, sizeof(expected) - 1);
    free(output);
}

static void test_binary_bounds_and_exit(void)
{
    const char *argv[] = {program, "binary", NULL};
    const char expected[] = "\r\n\x1a\0\xff";
    size_t length = 0;
    char *output = spawn_capture_stdout(argv, sizeof(expected), 2000, &length);
    EXPECT(output != NULL);
    if (output)
        EXPECT_MEM_EQ(output, length, expected, sizeof(expected));
    free(output);
    EXPECT(spawn_capture_stdout(argv, sizeof(expected) - 1, 2000, &length) == NULL);
    EXPECT(spawn_capture_stdout(argv, 0, 2000, &length) == NULL);
    const char *failure[] = {program, "failure", NULL};
    EXPECT(spawn_capture_stdout(failure, 1024, 2000, &length) == NULL);
    const char *empty[] = {program, "empty", NULL};
    EXPECT(spawn_capture_stdout(empty, 1024, 2000, &length) == NULL);
    const char *missing[] = {"hax-nonexistent-41a2.exe", NULL};
    EXPECT(spawn_capture_stdout(missing, 1024, 2000, &length) == NULL);
}

static void test_capture_deadlines(void)
{
    const char *modes[] = {"sleep", "close-sleep"};
    for (size_t i = 0; i < sizeof(modes) / sizeof(*modes); i++) {
        const char *argv[] = {program, modes[i], NULL};
        size_t length = 0;
        ULONGLONG started = GetTickCount64();
        EXPECT(spawn_capture_stdout(argv, 1024, 50, &length) == NULL);
        EXPECT(GetTickCount64() - started < 2000);
    }
}

static void test_shell_and_pipes(void)
{
    EXPECT(spawn_status_success(spawn_shell_wait("true")));
    EXPECT(!spawn_status_success(spawn_shell_wait("exit 42")));
    struct spawn_pipe pipe;
    EXPECT(spawn_pipe_open_read(&pipe, "printf 'hello\\n'") == 0);
    if (pipe.stream) {
        char bytes[7] = {0};
        EXPECT(fread(bytes, 1, 6, pipe.stream) == 6);
        EXPECT_STR_EQ(bytes, "hello\n");
        EXPECT(spawn_status_success(spawn_pipe_close(&pipe)));
    }
    char *path = path_join(t_tempdir(), "piped.txt");
    char *command = xasprintf("cat > '%s'", path);
    EXPECT(spawn_pipe_open_write(&pipe, command) == 0);
    if (pipe.stream) {
        EXPECT(fwrite("hello\n", 1, 6, pipe.stream) == 6);
        EXPECT(spawn_status_success(spawn_pipe_close(&pipe)));
        char *bytes = fs_read_file(path, NULL);
        EXPECT(bytes != NULL);
        if (bytes)
            EXPECT_STR_EQ(bytes, "hello\n");
        free(bytes);
    }
    free(command);
    free(path);
    EXPECT(spawn_pipe_close(NULL) == 0);
    EXPECT(spawn_pipe_open_read(&pipe, NULL) == -1 && pipe.stream == NULL);
}

static void test_detached_launch(void)
{
    char *path = path_join(t_tempdir(), "detached.txt");
    const char *argv[] = {program, "marker", path, NULL};
    EXPECT(spawn_detached(argv) == 0);
    ULONGLONG deadline = GetTickCount64() + 2000;
    char *bytes = NULL;
    do {
        bytes = fs_read_file(path, NULL);
        if (!bytes)
            Sleep(10);
    } while (!bytes && GetTickCount64() < deadline);
    EXPECT(bytes != NULL);
    if (bytes)
        EXPECT_STR_EQ(bytes, "detached");
    free(bytes);
    free(path);
}

static void test_render_helper_locale(void)
{
    t_env_set("LC_ALL", "C");
    char *command =
        spawn_shell_cmd_force_utf8(xstrdup("locale charmap; printf '%s' \"$LC_NUMERIC\""));
    struct spawn_pipe pipe;
    EXPECT(spawn_pipe_open_read(&pipe, command) == 0);
    if (pipe.stream) {
        char bytes[32] = {0};
        size_t length = fread(bytes, 1, sizeof(bytes) - 1, pipe.stream);
        EXPECT(length == 7);
        EXPECT_STR_EQ(bytes, "UTF-8\nC");
        EXPECT(spawn_status_success(spawn_pipe_close(&pipe)));
    }
    free(command);
    t_env_unset("LC_ALL");
}

int main(void)
{
    int argc;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return 1;
    if (argc > 1) {
        int result = run_child(argc, argv);
        LocalFree(argv);
        return result;
    }
    program = win_utf8_from_wide(argv[0]);
    LocalFree(argv);
    test_capture_arguments();
    test_binary_bounds_and_exit();
    test_capture_deadlines();
    test_shell_and_pipes();
    test_render_helper_locale();
    test_detached_launch();
    free(program);
    T_REPORT();
}
