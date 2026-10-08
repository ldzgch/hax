/* SPDX-License-Identifier: MIT */
#ifndef HAX_TERMINAL_CLIPBOARD_WIN_H
#define HAX_TERMINAL_CLIPBOARD_WIN_H

#include <windows.h>
#include <stddef.h>

/* Encode a borrowed native bitmap as owned PNG bytes. No clipboard access is performed. */
char *clipboard_bitmap_png(HBITMAP bitmap, size_t *out_len);

/* Decode a bounded CF_UNICODETEXT allocation, requiring a terminator inside the allocation.
 * Return owned UTF-8 bytes or NULL for empty, malformed, or oversized data. */
char *clipboard_text_utf8(const wchar_t *text, size_t byte_len, size_t *out_len);

#endif /* HAX_TERMINAL_CLIPBOARD_WIN_H */
