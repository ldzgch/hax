/* SPDX-License-Identifier: MIT */
#include <windows.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>

#include "system/bg_job.h"
#include "terminal/ansi.h"
#include "terminal/interrupt.h"
#include "terminal/tty.h"

#define ESCAPE_TIMEOUT_MS       50
#define ESCAPE_POLL_INTERVAL_MS 5

static struct bg_job *watcher;
static struct tty_mode *raw_mode;
static HANDLE input_handle;
static DWORD original_input_mode;
static atomic_int interactive_terminal;
enum request_bits {
    REQUEST_PAUSE = 1,
    REQUEST_ABORT = 2,
};

static atomic_uint requests;
static atomic_int escape_pending;
static atomic_int request_handlers;
static _Atomic(void (*)(void)) fatal_hook;
static int initialized;
static int control_handler_installed;

static void restore_terminal(void)
{
    if (atomic_load(&interactive_terminal)) {
        SetConsoleMode(input_handle, original_input_mode);
        static const char sequence[] = ANSI_BRACKETED_PASTE_DISABLE ANSI_CURSOR_SHOW ANSI_SYNC_END;
        DWORD written;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), sequence, sizeof(sequence) - 1, &written, NULL);
    }
    tty_restore_output();
}

static void fatal_cleanup(void)
{
    void (*hook)(void) = atomic_load(&fatal_hook);
    if (hook)
        hook();
    restore_terminal();
}

static void restore_and_reraise_signal(int signal_number)
{
    fatal_cleanup();
    signal(signal_number, SIG_DFL);
    raise(signal_number);
}

static void request_signal(int signal_number)
{
    signal(signal_number, request_signal);
    if (atomic_fetch_or(&requests, REQUEST_PAUSE | REQUEST_ABORT) & REQUEST_ABORT)
        restore_and_reraise_signal(signal_number);
}

static BOOL WINAPI console_control(DWORD event)
{
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) {
        if (atomic_load(&request_handlers)) {
            if (event == CTRL_BREAK_EVENT) {
                atomic_fetch_or(&requests, REQUEST_PAUSE);
                return TRUE;
            }
            if (!(atomic_fetch_or(&requests, REQUEST_PAUSE | REQUEST_ABORT) & REQUEST_ABORT))
                return TRUE;
        }
        fatal_cleanup();
        return FALSE;
    }
    if (event == CTRL_CLOSE_EVENT || event == CTRL_LOGOFF_EVENT || event == CTRL_SHUTDOWN_EVENT)
        fatal_cleanup();
    return FALSE;
}

void interrupt_install_fatal_signal_handlers(void)
{
    atomic_store(&request_handlers, 0);
    signal(SIGINT, restore_and_reraise_signal);
    signal(SIGTERM, restore_and_reraise_signal);
    /* An inherited Ctrl-C ignore flag must not suppress hax's own handler. */
    SetConsoleCtrlHandler(NULL, FALSE);
    if (!control_handler_installed && SetConsoleCtrlHandler(console_control, TRUE))
        control_handler_installed = 1;
}

void interrupt_install_request_signal_handlers(void)
{
    interrupt_install_fatal_signal_handlers();
    signal(SIGINT, request_signal);
    signal(SIGTERM, request_signal);
    atomic_store(&request_handlers, 1);
}

void interrupt_set_fatal_signal_hook(void (*hook)(void))
{
    atomic_store(&fatal_hook, hook);
}

static void latch_escape(void)
{
    if (atomic_fetch_or(&requests, REQUEST_PAUSE) & REQUEST_PAUSE)
        atomic_fetch_or(&requests, REQUEST_PAUSE | REQUEST_ABORT);
}

static void watch_input(struct bg_job *job, void *unused)
{
    (void)unused;
    struct interrupt_classifier classifier;
    interrupt_classifier_init(&classifier);
    ULONGLONG pending_since = 0;
    atomic_store(&escape_pending, 0);
    while (!bg_job_cancel_requested(job)) {
        unsigned char byte;
        int result = tty_read_byte(&byte, ESCAPE_POLL_INTERVAL_MS);
        if (result < 0)
            break;
        if (bg_job_cancel_requested(job))
            break;
        if (result > 0) {
            if (interrupt_classifier_feed(&classifier, byte))
                latch_escape();
            if (classifier.state == INTERRUPT_CLASSIFIER_ESCAPE_PENDING)
                pending_since = GetTickCount64();
        } else if (classifier.state == INTERRUPT_CLASSIFIER_ESCAPE_PENDING &&
                   GetTickCount64() - pending_since >= ESCAPE_TIMEOUT_MS) {
            if (interrupt_classifier_timeout(&classifier))
                latch_escape();
        }
        atomic_store(&escape_pending, classifier.state == INTERRUPT_CLASSIFIER_ESCAPE_PENDING);
    }
    atomic_store(&escape_pending, 0);
}

static void shutdown_watcher(void)
{
    interrupt_disarm();
    restore_terminal();
}

void interrupt_init(void)
{
    if (initialized)
        return;
    initialized = 1;
    DWORD output_mode;
    input_handle = GetStdHandle(STD_INPUT_HANDLE);
    if (!GetConsoleMode(input_handle, &original_input_mode) ||
        !GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &output_mode))
        return;
    atomic_store(&interactive_terminal, 1);
    atexit(shutdown_watcher);
    interrupt_install_fatal_signal_handlers();
}

void interrupt_arm(void)
{
    if (watcher || !atomic_load(&interactive_terminal))
        return;
    raw_mode = tty_raw_enter(1);
    if (!raw_mode)
        return;
    watcher = bg_job_spawn(watch_input, NULL);
    if (!watcher) {
        tty_raw_leave(raw_mode);
        raw_mode = NULL;
    }
}

void interrupt_disarm(void)
{
    if (!watcher)
        return;
    bg_job_cancel(watcher);
    bg_job_join(watcher);
    watcher = NULL;
    tty_raw_leave(raw_mode);
    raw_mode = NULL;
    tty_flush_input();
}

int interrupt_pause_requested(void)
{
    return !!(atomic_load(&requests) & REQUEST_PAUSE);
}

int interrupt_abort_requested(void)
{
    return !!(atomic_load(&requests) & REQUEST_ABORT);
}

void interrupt_clear_requests(void)
{
    atomic_store(&requests, 0);
}

void interrupt_resolve_pending_escape(void)
{
    if (!watcher)
        return;
    int remaining = ESCAPE_TIMEOUT_MS + 2 * ESCAPE_POLL_INTERVAL_MS;
    while (remaining > 0) {
        if ((atomic_load(&requests) & REQUEST_ABORT))
            return;
        DWORD queued = 0;
        GetNumberOfConsoleInputEvents(input_handle, &queued);
        if (!atomic_load(&escape_pending) && !queued)
            return;
        Sleep(ESCAPE_POLL_INTERVAL_MS);
        remaining -= ESCAPE_POLL_INTERVAL_MS;
    }
}
