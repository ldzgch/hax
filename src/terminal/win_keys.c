/* SPDX-License-Identifier: MIT */
#include "terminal/win_keys.h"

#include <stdio.h>
#include <string.h>

#include "text/utf8.h"

static void append_codepoint(struct win_keys *keys, uint32_t codepoint)
{
    keys->length += utf8_encode_codepoint(codepoint, keys->bytes + keys->length);
}

static void append_unicode(struct win_keys *keys, uint16_t value, int alt)
{
    if (keys->high_surrogate) {
        uint16_t high = keys->high_surrogate;
        keys->high_surrogate = 0;
        if (keys->surrogate_alt)
            keys->bytes[keys->length++] = 0x1b;
        if (value >= 0xdc00 && value <= 0xdfff) {
            append_codepoint(keys, 0x10000u + ((high - 0xd800u) << 10) + value - 0xdc00u);
            return;
        }
        append_codepoint(keys, 0xfffd);
    }
    if (value >= 0xd800 && value <= 0xdbff) {
        keys->high_surrogate = value;
        keys->surrogate_alt = alt;
    } else {
        if (alt && value != 0x1b)
            keys->bytes[keys->length++] = 0x1b;
        append_codepoint(keys, value >= 0xdc00 && value <= 0xdfff ? 0xfffd : value);
    }
}

void win_keys_feed(struct win_keys *keys, const KEY_EVENT_RECORD *event)
{
    keys->offset = keys->length = keys->repeats = 0;
    if (!event->bKeyDown)
        return;
    DWORD controls = event->dwControlKeyState;
    int shift = !!(controls & SHIFT_PRESSED);
    int control = !!(controls & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED));
    int alt = !!(controls & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED));
    /* AltGr is text entry, rather than the editor's Meta prefix. */
    int altgr = (controls & RIGHT_ALT_PRESSED) && (controls & LEFT_CTRL_PRESSED);
    if (altgr)
        alt = control = 0;
    int modifier = 1 + shift + 2 * alt + 4 * control;
    char final = 0;
    switch (event->wVirtualKeyCode) {
    case VK_UP:
        final = 'A';
        break;
    case VK_DOWN:
        final = 'B';
        break;
    case VK_RIGHT:
        final = 'C';
        break;
    case VK_LEFT:
        final = 'D';
        break;
    case VK_HOME:
        final = 'H';
        break;
    case VK_END:
        final = 'F';
        break;
    }
    if (final) {
        keys->high_surrogate = 0;
        keys->length = modifier == 1
                           ? (size_t)snprintf(keys->bytes, sizeof(keys->bytes), "\x1b[%c", final)
                           : (size_t)snprintf(keys->bytes, sizeof(keys->bytes), "\x1b[1;%d%c",
                                              modifier, final);
    } else {
        int tilde = 0;
        switch (event->wVirtualKeyCode) {
        case VK_INSERT:
            tilde = 2;
            break;
        case VK_DELETE:
            tilde = 3;
            break;
        case VK_PRIOR:
            tilde = 5;
            break;
        case VK_NEXT:
            tilde = 6;
            break;
        }
        if (tilde) {
            keys->high_surrogate = 0;
            keys->length = modifier == 1 ? (size_t)snprintf(keys->bytes, sizeof(keys->bytes),
                                                            "\x1b[%d~", tilde)
                                         : (size_t)snprintf(keys->bytes, sizeof(keys->bytes),
                                                            "\x1b[%d;%d~", tilde, modifier);
        } else {
            uint16_t character = event->uChar.UnicodeChar;
            if (event->wVirtualKeyCode == VK_RETURN) {
                character = '\r';
                alt |= shift;
            } else if (event->wVirtualKeyCode == VK_BACK) {
                character = control ? 0x17 : 0x7f;
            } else if (event->wVirtualKeyCode == VK_TAB) {
                character = '\t';
            } else if (control && event->wVirtualKeyCode >= 'A' && event->wVirtualKeyCode <= 'Z') {
                character = event->wVirtualKeyCode - 'A' + 1;
            }
            if (character) {
                append_unicode(keys, character, alt);
            }
        }
    }
    if (keys->length)
        keys->repeats = event->wRepeatCount ? event->wRepeatCount : 1;
}

int win_keys_pop(struct win_keys *keys, unsigned char *byte)
{
    if (!keys->repeats)
        return 0;
    *byte = (unsigned char)keys->bytes[keys->offset++];
    if (keys->offset == keys->length) {
        keys->offset = 0;
        keys->repeats--;
    }
    return 1;
}
