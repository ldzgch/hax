/* SPDX-License-Identifier: MIT */
#include "terminal/win_console.h"

#include <windows.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buf.h"
#include "xalloc.h"
#include "render/ctrl_strip.h"
#include "system/bg_job.h"
#include "system/win_error.h"
#include "system/win_process.h"
#include "system/win_utf8.h"
#include "text/windows_argv.h"

struct t_win_console {
    HPCON terminal;
    HANDLE input;
    HANDLE output;
    struct win_process process;
    struct bg_job *drainer;
    CRITICAL_SECTION lock;
    struct buf bytes;
};

static void drain_output(struct bg_job *job, void *arg)
{
    struct t_win_console *console = arg;
    char bytes[4096];
    while (!bg_job_cancel_requested(job)) {
        DWORD count;
        if (!ReadFile(console->output, bytes, sizeof(bytes), &count, NULL) || !count)
            return;
        EnterCriticalSection(&console->lock);
        if (console->bytes.len + count <= (1u << 20))
            buf_append(&console->bytes, bytes, count);
        LeaveCriticalSection(&console->lock);
    }
}

struct t_win_console *t_win_console_start(const char *const *argv)
{
    char *encoded = windows_argv_encode(argv);
    if (!encoded)
        return NULL;
    wchar_t *command = win_utf8_to_wide(encoded);
    free(encoded);
    if (!command)
        return NULL;
    wchar_t *application = win_utf8_to_wide(argv[0]);
    if (!application) {
        free(command);
        return NULL;
    }
    struct t_win_console *console = xcalloc(1, sizeof(*console));
    InitializeCriticalSection(&console->lock);
    buf_init(&console->bytes);
    HANDLE reader = NULL, writer = NULL;
    STARTUPINFOEXW startup = {.StartupInfo = {.cb = sizeof(startup)}};
    PROCESS_INFORMATION process = {0};
    int attributes_ready = 0;
    DWORD error = ERROR_SUCCESS;
    if (!CreatePipe(&reader, &console->input, NULL, 0) ||
        !CreatePipe(&console->output, &writer, NULL, 0))
        goto fail;
    COORD size = {.X = 110, .Y = 32};
    HRESULT result = CreatePseudoConsole(size, reader, writer, 0, &console->terminal);
    if (FAILED(result)) {
        error = HRESULT_CODE(result);
        goto cleanup;
    }
    CloseHandle(reader);
    CloseHandle(writer);
    reader = writer = NULL;
    console->drainer = bg_job_spawn(drain_output, console);
    if (!console->drainer) {
        error = ERROR_NOT_ENOUGH_MEMORY;
        goto cleanup;
    }
    SIZE_T attribute_bytes = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attribute_bytes);
    if (!attribute_bytes)
        goto fail;
    startup.lpAttributeList = xmalloc(attribute_bytes);
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attribute_bytes))
        goto fail;
    attributes_ready = 1;
    if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   console->terminal, sizeof(console->terminal), NULL, NULL))
        goto fail;
    console->process.job = CreateJobObjectW(NULL, NULL);
    if (!console->process.job)
        goto fail;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(console->process.job, JobObjectExtendedLimitInformation, &limits,
                                 sizeof(limits)))
        goto fail;
    /* A redirected test runner's standard handles otherwise survive pseudoconsole attachment.
     * The drainer uses its owned pipe directly; only this foreground launcher changes stdio. */
    const DWORD standard_ids[] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    HANDLE standard_handles[3];
    for (size_t i = 0; i < 3; i++) {
        standard_handles[i] = GetStdHandle(standard_ids[i]);
        SetStdHandle(standard_ids[i], NULL);
    }
    int launched = CreateProcessW(application, command, NULL, NULL, FALSE,
                                  EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED, NULL, NULL,
                                  &startup.StartupInfo, &process);
    DWORD launch_error = GetLastError();
    for (size_t i = 0; i < 3; i++)
        SetStdHandle(standard_ids[i], standard_handles[i]);
    if (!launched) {
        error = launch_error;
        goto cleanup;
    }
    console->process.handle = process.hProcess;
    console->process.id = process.dwProcessId;
    if (!AssignProcessToJobObject(console->process.job, process.hProcess) ||
        ResumeThread(process.hThread) == (DWORD)-1)
        goto fail;
    CloseHandle(process.hThread);
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    free(startup.lpAttributeList);
    free(application);
    free(command);
    return console;
fail:
    error = GetLastError();
cleanup:
    if (process.hProcess) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 2000);
    }
    if (process.hThread)
        CloseHandle(process.hThread);
    if (attributes_ready)
        DeleteProcThreadAttributeList(startup.lpAttributeList);
    free(startup.lpAttributeList);
    if (reader)
        CloseHandle(reader);
    if (writer)
        CloseHandle(writer);
    free(application);
    free(command);
    t_win_console_close(console);
    win_error_set_errno(error);
    return NULL;
}

int t_win_console_send(struct t_win_console *console, const char *bytes, unsigned length)
{
    DWORD written;
    return WriteFile(console->input, bytes, length, &written, NULL) && written == length;
}

int t_win_console_key(struct t_win_console *console, unsigned virtual_key, unsigned utf16,
                      unsigned controls)
{
    char event[96];
    int length =
        snprintf(event, sizeof(event), "\x1b[%u;0;%u;1;%u;1_", virtual_key, utf16, controls);
    return t_win_console_send(console, event, (unsigned)length);
}

size_t t_win_console_mark(struct t_win_console *console)
{
    EnterCriticalSection(&console->lock);
    size_t mark = console->bytes.len;
    LeaveCriticalSection(&console->lock);
    return mark;
}

int t_win_console_expect(struct t_win_console *console, const char *text, int timeout_ms)
{
    return t_win_console_expect_since(console, text, timeout_ms, 0);
}

char *t_win_console_output(struct t_win_console *console, size_t mark)
{
    EnterCriticalSection(&console->lock);
    const char *bytes =
        console->bytes.data && mark < console->bytes.len ? console->bytes.data + mark : "";
    char *plain = ctrl_strip_dup(bytes);
    LeaveCriticalSection(&console->lock);
    return plain;
}

int t_win_console_expect_since(struct t_win_console *console, const char *text, int timeout_ms,
                               size_t mark)
{
    ULONGLONG deadline = GetTickCount64() + (DWORD)timeout_ms;
    do {
        char *plain = t_win_console_output(console, mark);
        int found = strstr(plain, text) != NULL;
        free(plain);
        if (found)
            return 1;
        Sleep(5);
    } while (GetTickCount64() < deadline);
    char *plain = t_win_console_output(console, mark);
    fprintf(stderr, "console did not emit '%s'; output: %.4096s\n", text, plain);
    free(plain);
    return 0;
}

int t_win_console_wait(struct t_win_console *console, int timeout_ms, unsigned long *exit_code)
{
    int result = win_process_wait(&console->process, (DWORD)timeout_ms, exit_code);
    if (result == 1 && *exit_code) {
        EnterCriticalSection(&console->lock);
        char *plain = ctrl_strip_dup(console->bytes.data ? console->bytes.data : "");
        fprintf(stderr, "console child exit %lu; output: %.4096s\n", *exit_code, plain);
        free(plain);
        LeaveCriticalSection(&console->lock);
    }
    return result;
}

void t_win_console_close(struct t_win_console *console)
{
    if (!console)
        return;
    win_process_terminate(&console->process);
    if (console->process.handle) {
        DWORD exit_code;
        win_process_wait(&console->process, 2000, &exit_code);
    }
    if (console->input)
        CloseHandle(console->input);
    if (console->terminal)
        ClosePseudoConsole(console->terminal);
    bg_job_join(console->drainer);
    if (console->output)
        CloseHandle(console->output);
    win_process_close(&console->process);
    DeleteCriticalSection(&console->lock);
    buf_free(&console->bytes);
    free(console);
}
