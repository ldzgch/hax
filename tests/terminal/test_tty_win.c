/* SPDX-License-Identifier: MIT */
#include <windows.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "system/win_utf8.h"
#include "terminal/tty.h"
#include "terminal/win_console.h"

static const char console_bytes[] = "\xc3\xa9\xf0\x9f\x98\x80\x1b[D\x03";

static int console_child(void)
{
    HANDLE input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    DWORD input_mode, output_mode;
    EXPECT(GetConsoleMode(input, &input_mode));
    EXPECT(GetConsoleMode(output, &output_mode));
    UINT codepage = GetConsoleOutputCP();
    tty_init();
    EXPECT(GetConsoleOutputCP() == CP_UTF8);
    int columns = 0, rows = 0;
    EXPECT(tty_size(&columns, &rows) == 0 && columns == 110 && rows == 32);
    struct tty_mode *raw = tty_raw_enter(0);
    EXPECT(raw != NULL);
    if (!raw)
        return 1;
    fputs("READY", stdout);
    fflush(stdout);
    for (size_t i = 0; i < sizeof(console_bytes) - 1; i++) {
        unsigned char byte = 0;
        EXPECT(tty_read_byte(&byte, 2000) == 1 && byte == (unsigned char)console_bytes[i]);
    }
    tty_raw_leave(raw);
    DWORD restored;
    EXPECT(GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &restored) && restored == input_mode);
    tty_restore_output();
    EXPECT(GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &restored) && restored == output_mode);
    EXPECT(GetConsoleOutputCP() == codepage);
    CloseHandle(input);
    CloseHandle(output);
    fputs("DONE", stdout);
    fflush(stdout);
    return t_failures ? 1 : 0;
}

static void test_native_console(void)
{
    wchar_t module[32768];
    EXPECT(GetModuleFileNameW(NULL, module, sizeof(module) / sizeof(*module)) != 0);
    char *program = win_utf8_from_wide(module);
    const char *argv[] = {program, "--console-child", NULL};
    struct t_win_console *console = t_win_console_start(argv);
    free(program);
    EXPECT(console != NULL);
    if (!console)
        return;
    int ready = t_win_console_expect(console, "READY", 3000);
    EXPECT(ready);
    if (ready) {
        EXPECT(t_win_console_key(console, VK_PACKET, 0x00e9, 0));
        EXPECT(t_win_console_key(console, VK_PACKET, 0xd83d, 0));
        EXPECT(t_win_console_key(console, VK_PACKET, 0xde00, 0));
        EXPECT(t_win_console_key(console, VK_LEFT, 0, 0));
        EXPECT(t_win_console_key(console, 'C', 0x03, LEFT_CTRL_PRESSED));
        EXPECT(t_win_console_expect(console, "DONE", 3000));
        unsigned long exit_code = 1;
        EXPECT(t_win_console_wait(console, 3000, &exit_code) == 1 && exit_code == 0);
    }
    t_win_console_close(console);
}

static void test_redirected_pipe_reads(void)
{
    HANDLE reader, writer;
    int created = CreatePipe(&reader, &writer, NULL, 0);
    EXPECT(created);
    if (!created)
        return;
    HANDLE saved = GetStdHandle(STD_INPUT_HANDLE);
    EXPECT(SetStdHandle(STD_INPUT_HANDLE, reader));
    unsigned char byte;
    ULONGLONG started = GetTickCount64();
    EXPECT(tty_read_byte(&byte, 20) == 0);
    EXPECT(GetTickCount64() - started < 2000);
    const unsigned char bytes[] = {'\r', '\n', 0x1a, 0, 0xff};
    DWORD written;
    EXPECT(WriteFile(writer, bytes, sizeof(bytes), &written, NULL) && written == sizeof(bytes));
    for (size_t i = 0; i < sizeof(bytes); i++)
        EXPECT(tty_read_byte(&byte, 0) == 1 && byte == bytes[i]);
    CloseHandle(writer);
    EXPECT(tty_read_byte(&byte, 20) == 0);
    EXPECT(SetStdHandle(STD_INPUT_HANDLE, saved));
    CloseHandle(reader);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--console-child") == 0)
        return console_child();
    test_redirected_pipe_reads();
    test_native_console();
    tty_raw_leave(NULL);
    T_REPORT();
}
