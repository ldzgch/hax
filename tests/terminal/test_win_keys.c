/* SPDX-License-Identifier: MIT */
#include <string.h>

#include "harness.h"
#include "terminal/win_keys.h"

static void expect_key(WORD virtual_key, WCHAR character, DWORD controls, WORD repeats,
                       const char *expected)
{
    struct win_keys keys = {0};
    KEY_EVENT_RECORD event = {.bKeyDown = TRUE,
                              .wRepeatCount = repeats,
                              .wVirtualKeyCode = virtual_key,
                              .uChar.UnicodeChar = character,
                              .dwControlKeyState = controls};
    win_keys_feed(&keys, &event);
    char bytes[128];
    size_t count = 0;
    unsigned char byte;
    while (count + 1 < sizeof(bytes) && win_keys_pop(&keys, &byte))
        bytes[count++] = (char)byte;
    bytes[count] = '\0';
    EXPECT_STR_EQ(bytes, expected);
}

static void test_key_sequences(void)
{
    expect_key(VK_UP, 0, 0, 1, "\x1b[A");
    expect_key(VK_RIGHT, 0, LEFT_CTRL_PRESSED, 1, "\x1b[1;5C");
    expect_key(VK_LEFT, 0, LEFT_ALT_PRESSED | SHIFT_PRESSED, 1, "\x1b[1;4D");
    expect_key(VK_HOME, 0, 0, 1, "\x1b[H");
    expect_key(VK_DELETE, 0, 0, 1, "\x1b[3~");
    expect_key(VK_NEXT, 0, LEFT_CTRL_PRESSED, 1, "\x1b[6;5~");
    expect_key(VK_RETURN, '\r', SHIFT_PRESSED, 1, "\x1b\r");
    expect_key(VK_BACK, '\b', LEFT_CTRL_PRESSED, 1, "\x17");
    expect_key(VK_BACK, '\b', LEFT_ALT_PRESSED, 1, "\x1b\x7f");
    expect_key('C', 0, LEFT_CTRL_PRESSED, 1, "\x03");
    expect_key('Q', '@', LEFT_CTRL_PRESSED | RIGHT_ALT_PRESSED, 1, "@");
    expect_key('A', 'a', 0, 3, "aaa");
    expect_key(0, 0x00e9, 0, 1, "\xc3\xa9");
    expect_key(VK_SHIFT, 0, SHIFT_PRESSED, 1, "");
}

static void test_unicode_pairs_and_key_release(void)
{
    struct win_keys keys = {0};
    KEY_EVENT_RECORD event = {.bKeyDown = TRUE, .wRepeatCount = 1, .uChar.UnicodeChar = 0xd83d};
    unsigned char byte;
    win_keys_feed(&keys, &event);
    EXPECT(!win_keys_pop(&keys, &byte));
    event.bKeyDown = FALSE;
    win_keys_feed(&keys, &event);
    EXPECT(!win_keys_pop(&keys, &byte));
    event.bKeyDown = TRUE;
    event.uChar.UnicodeChar = 0xde00;
    win_keys_feed(&keys, &event);
    const unsigned char expected[] = {0xf0, 0x9f, 0x98, 0x80};
    for (size_t i = 0; i < sizeof(expected); i++)
        EXPECT(win_keys_pop(&keys, &byte) && byte == expected[i]);
    EXPECT(!win_keys_pop(&keys, &byte));
    event.uChar.UnicodeChar = 0xdc00;
    win_keys_feed(&keys, &event);
    const unsigned char replacement[] = {0xef, 0xbf, 0xbd};
    for (size_t i = 0; i < sizeof(replacement); i++)
        EXPECT(win_keys_pop(&keys, &byte) && byte == replacement[i]);
}

int main(void)
{
    test_key_sequences();
    test_unicode_pairs_and_key_release();
    T_REPORT();
}
