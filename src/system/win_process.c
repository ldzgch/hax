/* SPDX-License-Identifier: MIT */
#include "system/win_process.h"

#include <errhandlingapi.h>
#include <errno.h>
#include <fileapi.h>
#include <handleapi.h>
#include <minwinbase.h>
#include <minwindef.h>
#include <processthreadsapi.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stringapiset.h>
#include <synchapi.h>
#include <wchar.h>
#include <winerror.h>
#include <winnls.h>
#include <winnt.h>
/* The Windows API headers depend on this umbrella header's target declarations. */
#include <windows.h> // IWYU pragma: keep

#include "xalloc.h"
#include "system/fs.h"
#include "system/win_error.h"
#include "system/win_utf8.h"
#include "text/windows_argv.h"

static int environment_name_length(const wchar_t *entry)
{
    /* Native drive-current-directory entries begin with '=', for example '=C:=C:\\work'. */
    const wchar_t *separator = wcschr(entry + (*entry == L'='), L'=');
    return separator && separator != entry ? (int)(separator - entry) : -1;
}

static int compare_environment(const void *left, const void *right)
{
    const wchar_t *a = *(const wchar_t *const *)left;
    const wchar_t *b = *(const wchar_t *const *)right;
    int order =
        CompareStringOrdinal(a, environment_name_length(a), b, environment_name_length(b), TRUE);
    return order == CSTR_LESS_THAN ? -1 : order == CSTR_GREATER_THAN ? 1 : 0;
}

wchar_t *win_process_environment(const char *const *entries)
{
    if (!entries) {
        errno = EINVAL;
        return NULL;
    }
    size_t count = 0;
    while (entries[count])
        count++;
    wchar_t **wide = xcalloc(count + 1, sizeof(*wide));
    wchar_t *block = NULL;
    size_t length = count ? 1 : 2;
    for (size_t i = 0; i < count; i++) {
        wide[i] = win_utf8_to_wide(entries[i]);
        if (!wide[i])
            goto out;
        if (environment_name_length(wide[i]) < 0) {
            errno = EINVAL;
            goto out;
        }
        size_t needed = wcslen(wide[i]) + 1;
        if (needed > SIZE_MAX / sizeof(*block) - length) {
            errno = EOVERFLOW;
            goto out;
        }
        length += needed;
    }
    qsort(wide, count, sizeof(*wide), compare_environment);
    for (size_t i = 1; i < count; i++) {
        if (compare_environment(&wide[i - 1], &wide[i]) == 0) {
            errno = EINVAL;
            goto out;
        }
    }
    block = xcalloc(length, sizeof(*block));
    wchar_t *cursor = block;
    for (size_t i = 0; i < count; i++) {
        size_t needed = wcslen(wide[i]) + 1;
        memcpy(cursor, wide[i], needed * sizeof(*cursor));
        cursor += needed;
    }
out:
    for (size_t i = 0; i < count; i++)
        free(wide[i]);
    free(wide);
    return block;
}

static HANDLE inherit_standard_handle(HANDLE handle)
{
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        SECURITY_ATTRIBUTES security = {.nLength = sizeof(security), .bInheritHandle = TRUE};
        return CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           &security, OPEN_EXISTING, 0, NULL);
    }
    HANDLE duplicate = NULL;
    if (!DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &duplicate, 0, TRUE,
                         DUPLICATE_SAME_ACCESS))
        return INVALID_HANDLE_VALUE;
    return duplicate;
}

int win_process_start(struct win_process *result, const char *const *argv, HANDLE input,
                      HANDLE output, HANDLE error, int detached)
{
    return win_process_start_env(result, argv, input, output, error, detached, NULL);
}

int win_process_start_env(struct win_process *result, const char *const *argv, HANDLE input,
                          HANDLE output, HANDLE error, int detached, const wchar_t *environment)
{
    if (!result) {
        errno = EINVAL;
        return -1;
    }
    memset(result, 0, sizeof(*result));
    char *encoded = windows_argv_encode(argv);
    if (!encoded)
        return -1;
    wchar_t *command = win_utf8_to_wide(encoded);
    free(encoded);
    if (!command)
        return -1;
    char *program = fs_which(argv[0]);
    if (!program) {
        free(command);
        errno = ENOENT;
        return -1;
    }
    wchar_t *application = win_utf8_to_wide(program);
    free(program);
    if (!application) {
        free(command);
        return -1;
    }

    HANDLE handles[3] = {NULL, NULL, NULL};
    HANDLE originals[3] = {input, output, error};
    HANDLE job = NULL;
    PROCESS_INFORMATION process = {0};
    STARTUPINFOEXW startup = {
        .StartupInfo = {.cb = sizeof(startup),
                        .dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW,
                        .wShowWindow = SW_HIDE}};
    int attributes_ready = 0;
    int status = -1;
    DWORD native_error = ERROR_SUCCESS;
    for (size_t i = 0; i < 3; i++) {
        handles[i] = inherit_standard_handle(originals[i]);
        if (handles[i] == INVALID_HANDLE_VALUE)
            goto error;
    }
    startup.StartupInfo.hStdInput = handles[0];
    startup.StartupInfo.hStdOutput = handles[1];
    startup.StartupInfo.hStdError = handles[2];
    SIZE_T attribute_bytes = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attribute_bytes);
    if (!attribute_bytes)
        goto error;
    startup.lpAttributeList = xmalloc(attribute_bytes);
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attribute_bytes))
        goto error;
    attributes_ready = 1;
    if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   handles, sizeof(handles), NULL, NULL))
        goto error;
    if (!detached) {
        job = CreateJobObjectW(NULL, NULL);
        if (!job)
            goto error;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits,
                                     sizeof(limits)))
            goto error;
    }
    DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT |
                  (detached ? DETACHED_PROCESS : 0);
    if (!CreateProcessW(application, command, NULL, NULL, TRUE, flags, (void *)environment, NULL,
                        &startup.StartupInfo, &process))
        goto error;
    if (job && !AssignProcessToJobObject(job, process.hProcess))
        goto error;
    if (ResumeThread(process.hThread) == (DWORD)-1)
        goto error;
    result->handle = process.hProcess;
    result->job = job;
    result->id = process.dwProcessId;
    status = 0;
    goto out;
error:
    native_error = GetLastError();
    if (process.hProcess) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, INFINITE);
        CloseHandle(process.hProcess);
    }
    if (job)
        CloseHandle(job);
out:
    if (process.hThread)
        CloseHandle(process.hThread);
    if (attributes_ready)
        DeleteProcThreadAttributeList(startup.lpAttributeList);
    free(startup.lpAttributeList);
    for (size_t i = 0; i < 3; i++)
        if (handles[i] && handles[i] != INVALID_HANDLE_VALUE)
            CloseHandle(handles[i]);
    free(application);
    free(command);
    if (status < 0)
        win_error_set_errno(native_error);
    return status;
}

int win_process_wait(const struct win_process *process, DWORD timeout_ms, DWORD *exit_code)
{
    if (!process || !process->handle || !exit_code) {
        errno = EINVAL;
        return -1;
    }
    DWORD result = WaitForSingleObject(process->handle, timeout_ms);
    if (result == WAIT_TIMEOUT)
        return 0;
    if (result == WAIT_OBJECT_0 && GetExitCodeProcess(process->handle, exit_code))
        return 1;
    win_error_set_errno(GetLastError());
    return -1;
}

void win_process_terminate(const struct win_process *process)
{
    if (process->job)
        TerminateJobObject(process->job, 1);
}

void win_process_close(struct win_process *process)
{
    if (process->job)
        CloseHandle(process->job);
    if (process->handle)
        CloseHandle(process->handle);
    memset(process, 0, sizeof(*process));
}
