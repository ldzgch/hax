/* SPDX-License-Identifier: MIT */
#ifndef HAX_TERMINAL_WIN_KEYS_H
#define HAX_TERMINAL_WIN_KEYS_H

#include <windows.h>
#include <stddef.h>
#include <stdint.h>

/* Unicode console events become the UTF-8 and xterm key bytes consumed by the shared editor.
 * Zero-initialize before use; feed only after draining the previous event. */
struct win_keys {
    char bytes[32];
    size_t length;
    size_t offset;
    unsigned repeats;
    uint16_t high_surrogate;
    int surrogate_alt;
};

void win_keys_feed(struct win_keys *keys, const KEY_EVENT_RECORD *event);
int win_keys_pop(struct win_keys *keys, unsigned char *byte);

#endif /* HAX_TERMINAL_WIN_KEYS_H */
