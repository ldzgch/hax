/* SPDX-License-Identifier: MIT */
#include <windows.h>
#include <errno.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "harness.h"
#include "system/win_process.h"
#include "system/win_utf8.h"

static char *program;

static int run_child(int argc, wchar_t **argv)
{
    if (wcscmp(argv[1], L"environment") == 0) {
        wchar_t value[80];
        DWORD length = GetEnvironmentVariableW(L"HAX_TEST_UNICODE", value, 80);
        if (!length || length >= 80 || wcscmp(value, L"\x00e9-\xd83d\xde00") != 0)
            return 2;
        return GetEnvironmentVariableW(L"HAX_TEST_PARENT_ONLY", value, 80) == 0 ? 0 : 3;
    }
    if (wcscmp(argv[1], L"handle") == 0 && argc == 3) {
        uintptr_t value = wcstoull(argv[2], NULL, 10);
        return SetEvent((HANDLE)value) ? 1 : 0;
    }
    if (wcscmp(argv[1], L"tree") == 0) {
        const char *child_argv[] = {program, "sleep", NULL};
        struct win_process child;
        if (win_process_start(&child, child_argv, NULL, NULL, NULL, 1) < 0)
            return 1;
        DWORD written;
        DWORD id = child.id;
        int sent = WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), &id, sizeof(id), &written, NULL);
        win_process_close(&child);
        if (!sent || written != sizeof(id))
            return 1;
    }
    Sleep(10000);
    return 0;
}

static void test_explicit_inheritance(void)
{
    SECURITY_ATTRIBUTES security = {.nLength = sizeof(security), .bInheritHandle = TRUE};
    HANDLE event = CreateEventW(&security, TRUE, FALSE, NULL);
    EXPECT(event != NULL);
    char value[32];
    snprintf(value, sizeof(value), "%llu", (unsigned long long)(uintptr_t)event);
    const char *argv[] = {program, "handle", value, NULL};
    struct win_process process;
    int launched = win_process_start(&process, argv, NULL, NULL, NULL, 0);
    EXPECT(launched == 0);
    if (launched == 0) {
        DWORD exit_code;
        EXPECT(win_process_wait(&process, 2000, &exit_code) == 1 && exit_code == 0);
        win_process_close(&process);
        EXPECT(!process.handle && !process.job);
    }
    EXPECT(WaitForSingleObject(event, 0) == WAIT_TIMEOUT);
    CloseHandle(event);
}

static void test_owned_descendants(void)
{
    HANDLE reader, writer;
    int created = CreatePipe(&reader, &writer, NULL, 0);
    EXPECT(created);
    if (!created)
        return;
    const char *argv[] = {program, "tree", NULL};
    struct win_process process;
    int launched = win_process_start(&process, argv, NULL, writer, NULL, 0);
    CloseHandle(writer);
    EXPECT(launched == 0);
    if (launched < 0) {
        CloseHandle(reader);
        return;
    }
    DWORD available = 0;
    ULONGLONG deadline = GetTickCount64() + 2000;
    while (PeekNamedPipe(reader, NULL, 0, NULL, &available, NULL) && !available &&
           GetTickCount64() < deadline)
        Sleep(5);
    DWORD id = 0, received = 0;
    EXPECT(available == sizeof(id));
    HANDLE descendant = NULL;
    if (available == sizeof(id)) {
        EXPECT(ReadFile(reader, &id, sizeof(id), &received, NULL) && received == sizeof(id));
        descendant = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);
        EXPECT(descendant != NULL);
    }
    DWORD exit_code;
    EXPECT(win_process_wait(&process, 0, &exit_code) == 0);
    win_process_terminate(&process);
    EXPECT(win_process_wait(&process, 2000, &exit_code) == 1);
    win_process_close(&process);
    CloseHandle(reader);
    if (descendant) {
        EXPECT(WaitForSingleObject(descendant, 2000) == WAIT_OBJECT_0);
        CloseHandle(descendant);
    }
}

static void test_close_terminates(void)
{
    const char *argv[] = {program, "sleep", NULL};
    struct win_process process;
    int launched = win_process_start(&process, argv, NULL, NULL, NULL, 0);
    EXPECT(launched == 0);
    if (launched < 0)
        return;
    HANDLE observer = OpenProcess(SYNCHRONIZE, FALSE, process.id);
    EXPECT(observer != NULL);
    win_process_close(&process);
    if (observer) {
        EXPECT(WaitForSingleObject(observer, 2000) == WAIT_OBJECT_0);
        CloseHandle(observer);
    }
    const char *missing[] = {"hax-nonexistent-41a2.exe", NULL};
    EXPECT(win_process_start(&process, missing, NULL, NULL, NULL, 0) == -1);
    EXPECT(!process.handle && !process.job);
    EXPECT(win_process_start(&process, NULL, NULL, NULL, NULL, 0) == -1 && errno == EINVAL);
}

static void test_unicode_environment(void)
{
    const wchar_t name[] = L"HAX_TEST_PARENT_ONLY";
    wchar_t parent[16];
    DWORD capacity = GetEnvironmentVariableW(name, NULL, 0);
    wchar_t *saved = capacity ? calloc(capacity, sizeof(*saved)) : NULL;
    EXPECT(!capacity || saved != NULL);
    if (capacity && !saved)
        return;
    if (saved)
        GetEnvironmentVariableW(name, saved, capacity);
    EXPECT(SetEnvironmentVariableW(name, L"parent"));
    const char *entries[] = {"HAX_TEST_UNICODE=\xc3\xa9-\xf0\x9f\x98\x80", "HAX_TEST_SORT_A0=zero",
                             "HAX_TEST_SORT_A=a", NULL};
    wchar_t *environment = win_process_environment(entries);
    EXPECT(environment != NULL);
    if (!environment)
        goto restore;
    EXPECT(wcscmp(environment, L"HAX_TEST_SORT_A=a") == 0);
    const char *argv[] = {program, "environment", NULL};
    struct win_process process;
    int launched = win_process_start_env(&process, argv, NULL, NULL, NULL, 0, environment);
    free(environment);
    EXPECT(launched == 0);
    if (launched == 0) {
        DWORD exit_code;
        EXPECT(win_process_wait(&process, 2000, &exit_code) == 1 && exit_code == 0);
        win_process_close(&process);
    }
restore:
    EXPECT(GetEnvironmentVariableW(name, parent, 16) == 6);
    EXPECT(wcscmp(parent, L"parent") == 0);
    EXPECT(SetEnvironmentVariableW(name, saved));
    free(saved);
}

static void test_environment_validation(void)
{
    const char *empty[] = {NULL};
    wchar_t *block = win_process_environment(empty);
    EXPECT(block != NULL);
    if (block) {
        EXPECT(block[0] == 0 && block[1] == 0);
        free(block);
    }
    const char *duplicate[] = {"Name=one", "nAME=two", NULL};
    EXPECT(win_process_environment(duplicate) == NULL && errno == EINVAL);
    const char *malformed[] = {"missing-equals", NULL};
    EXPECT(win_process_environment(malformed) == NULL && errno == EINVAL);
    const char *invalid[] = {"NAME=\xff", NULL};
    EXPECT(win_process_environment(invalid) == NULL && errno == EILSEQ);
    EXPECT(win_process_environment(NULL) == NULL && errno == EINVAL);
}

int main(void)
{
    int argc;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return 1;
    program = win_utf8_from_wide(argv[0]);
    if (argc > 1) {
        int result = run_child(argc, argv);
        free(program);
        LocalFree(argv);
        return result;
    }
    LocalFree(argv);
    test_explicit_inheritance();
    test_unicode_environment();
    test_environment_validation();
    test_owned_descendants();
    test_close_terminates();
    free(program);
    T_REPORT();
}
