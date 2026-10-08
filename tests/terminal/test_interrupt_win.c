/* SPDX-License-Identifier: MIT */
#include <windows.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "system/win_utf8.h"
#include "terminal/interrupt.h"
#include "terminal/tty.h"
#include "terminal/win_console.h"

static int await_request(int abort)
{
    ULONGLONG deadline = GetTickCount64() + 2000;
    do {
        if (abort ? interrupt_abort_requested() : interrupt_pause_requested())
            return 1;
        Sleep(5);
    } while (GetTickCount64() < deadline);
    return 0;
}

static void report_ready(const char *text)
{
    fputs(text, stdout);
    fflush(stdout);
}

static int escape_child(void)
{
    tty_init();
    DWORD baseline;
    EXPECT(GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &baseline));
    interrupt_init();
    interrupt_clear_requests();
    interrupt_arm();
    interrupt_arm();
    report_ready("READY");
    Sleep(200);
    EXPECT(!interrupt_pause_requested());
    report_ready("ARROW-DONE");
    EXPECT(await_request(0));
    EXPECT(!interrupt_abort_requested());
    interrupt_disarm();
    interrupt_disarm();
    DWORD restored;
    EXPECT(GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &restored) && restored == baseline);
    EXPECT(interrupt_pause_requested());
    interrupt_clear_requests();
    interrupt_arm();
    report_ready("PAUSE-DONE");
    EXPECT(await_request(1));
    EXPECT(interrupt_pause_requested());
    interrupt_disarm();
    report_ready("BOTH-DONE");
    return t_failures ? 1 : 0;
}

static int control_child(void)
{
    tty_init();
    interrupt_install_fatal_signal_handlers();
    interrupt_install_request_signal_handlers();
    interrupt_clear_requests();
    EXPECT(GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, 0));
    EXPECT(await_request(0));
    EXPECT(!interrupt_abort_requested());
    interrupt_clear_requests();
    EXPECT(GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0));
    EXPECT(await_request(1));
    EXPECT(interrupt_pause_requested());
    interrupt_clear_requests();
    EXPECT(raise(SIGTERM) == 0);
    EXPECT(interrupt_abort_requested() && interrupt_pause_requested());
    interrupt_clear_requests();
    EXPECT(raise(SIGINT) == 0);
    EXPECT(interrupt_abort_requested() && interrupt_pause_requested());
    report_ready("CONTROL-DONE");
    return t_failures ? 1 : 0;
}

static void test_console_requests(const char *program, int control)
{
    const char *argv[] = {program, control ? "--control-child" : "--escape-child", NULL};
    struct t_win_console *console = t_win_console_start(argv);
    EXPECT(console != NULL);
    if (!console)
        return;
    if (control) {
        EXPECT(t_win_console_expect(console, "CONTROL-DONE", 3000));
    } else {
        EXPECT(t_win_console_expect(console, "READY", 3000));
        EXPECT(t_win_console_key(console, VK_UP, 0, 0));
        EXPECT(t_win_console_expect(console, "ARROW-DONE", 3000));
        EXPECT(t_win_console_key(console, VK_ESCAPE, 0x1b, 0));
        EXPECT(t_win_console_expect(console, "PAUSE-DONE", 3000));
        EXPECT(t_win_console_key(console, VK_ESCAPE, 0x1b, 0));
        EXPECT(t_win_console_key(console, VK_ESCAPE, 0x1b, 0));
        EXPECT(t_win_console_expect(console, "BOTH-DONE", 3000));
    }
    unsigned long exit_code = 1;
    EXPECT(t_win_console_wait(console, 3000, &exit_code) == 1 && exit_code == 0);
    t_win_console_close(console);
}

int main(int argc, char **argv)
{
    if (argc == 2)
        return strcmp(argv[1], "--control-child") == 0 ? control_child() : escape_child();
    wchar_t module[32768];
    EXPECT(GetModuleFileNameW(NULL, module, sizeof(module) / sizeof(*module)) != 0);
    char *program = win_utf8_from_wide(module);
    test_console_requests(program, 0);
    test_console_requests(program, 1);
    free(program);
    T_REPORT();
}
