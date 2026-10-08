/* SPDX-License-Identifier: MIT */
#define COBJMACROS
#include "terminal/clipboard_win.h"

#include <fcntl.h>
#include <limits.h>
#include <objbase.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wincodec.h>

#include "xalloc.h"
#include "system/clock.h"
#include "system/fd.h"
#include "system/win_utf8.h"
#include "terminal/clipboard.h"

#define CLIPBOARD_IMAGE_MAX_BYTES (64u << 20)
#define CLIPBOARD_TEXT_MAX_BYTES  (1u << 20)

static int open_clipboard(HWND owner, long deadline_ms)
{
    while (monotonic_ms() < deadline_ms) {
        if (OpenClipboard(owner))
            return 0;
        Sleep(5);
    }
    return -1;
}

char *clipboard_bitmap_png(HBITMAP bitmap, size_t *out_len)
{
    BITMAP info;
    if (!bitmap || GetObjectW(bitmap, sizeof(info), &info) != sizeof(info) || info.bmWidth <= 0 ||
        info.bmHeight <= 0 ||
        (uint64_t)info.bmWidth * (uint64_t)info.bmHeight > CLIPBOARD_IMAGE_MAX_BYTES / 4)
        return NULL;
    HRESULT initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
        return NULL;
    IWICImagingFactory *factory = NULL;
    IWICBitmap *source = NULL;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    IStream *stream = NULL;
    char *result = NULL;
    if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IWICImagingFactory, (void **)&factory)))
        goto out;
    /* Clipboard bitmaps commonly have undefined alpha; encode their visible RGB pixels. */
    if (FAILED(IWICImagingFactory_CreateBitmapFromHBITMAP(factory, bitmap, NULL,
                                                          WICBitmapIgnoreAlpha, &source)))
        goto out;
    if (FAILED(CreateStreamOnHGlobal(NULL, TRUE, &stream)))
        goto out;
    if (FAILED(
            IWICImagingFactory_CreateEncoder(factory, &GUID_ContainerFormatPng, NULL, &encoder)) ||
        FAILED(IWICBitmapEncoder_Initialize(encoder, stream, WICBitmapEncoderNoCache)) ||
        FAILED(IWICBitmapEncoder_CreateNewFrame(encoder, &frame, NULL)) ||
        FAILED(IWICBitmapFrameEncode_Initialize(frame, NULL)) ||
        FAILED(IWICBitmapFrameEncode_WriteSource(frame, (IWICBitmapSource *)source, NULL)) ||
        FAILED(IWICBitmapFrameEncode_Commit(frame)) || FAILED(IWICBitmapEncoder_Commit(encoder)))
        goto out;
    STATSTG status;
    HGLOBAL memory;
    if (FAILED(IStream_Stat(stream, &status, STATFLAG_NONAME)) || !status.cbSize.QuadPart ||
        status.cbSize.QuadPart > CLIPBOARD_IMAGE_MAX_BYTES ||
        FAILED(GetHGlobalFromStream(stream, &memory)))
        goto out;
    const void *bytes = GlobalLock(memory);
    if (!bytes)
        goto out;
    size_t length = (size_t)status.cbSize.QuadPart;
    result = xmalloc(length);
    memcpy(result, bytes, length);
    GlobalUnlock(memory);
    *out_len = length;
out:
    if (frame)
        IWICBitmapFrameEncode_Release(frame);
    if (encoder)
        IWICBitmapEncoder_Release(encoder);
    if (stream)
        IStream_Release(stream);
    if (source)
        IWICBitmap_Release(source);
    if (factory)
        IWICImagingFactory_Release(factory);
    if (SUCCEEDED(initialized))
        CoUninitialize();
    return result;
}

static char *read_clipboard_bytes(UINT format, size_t *out_len)
{
    HANDLE memory = GetClipboardData(format);
    if (!memory)
        return NULL;
    size_t length = GlobalSize(memory);
    if (!length || length > CLIPBOARD_IMAGE_MAX_BYTES)
        return NULL;
    const void *bytes = GlobalLock(memory);
    if (!bytes)
        return NULL;
    char *result = xmalloc(length);
    memcpy(result, bytes, length);
    GlobalUnlock(memory);
    *out_len = length;
    return result;
}

char *clipboard_paste_image(size_t *out_len, long deadline_ms)
{
    if (open_clipboard(NULL, deadline_ms) < 0)
        return NULL;
    UINT png = RegisterClipboardFormatW(L"PNG");
    char *image = png ? read_clipboard_bytes(png, out_len) : NULL;
    if (!image && IsClipboardFormatAvailable(CF_BITMAP))
        image = clipboard_bitmap_png((HBITMAP)GetClipboardData(CF_BITMAP), out_len);
    CloseClipboard();
    return image;
}

char *clipboard_text_utf8(const wchar_t *text, size_t byte_len, size_t *out_len)
{
    size_t capacity = byte_len / sizeof(*text);
    if (!text || byte_len % sizeof(*text) || !capacity || capacity > CLIPBOARD_TEXT_MAX_BYTES)
        return NULL;
    size_t length = 0;
    while (length < capacity && text[length])
        length++;
    if (!length || length == capacity)
        return NULL;
    char *result = win_utf8_from_wide(text);
    if (result) {
        size_t utf8_len = strlen(result);
        if (utf8_len > CLIPBOARD_TEXT_MAX_BYTES) {
            free(result);
            return NULL;
        }
        *out_len = utf8_len;
    }
    return result;
}

char *clipboard_paste_text(size_t *out_len, long deadline_ms)
{
    if (open_clipboard(NULL, deadline_ms) < 0)
        return NULL;
    char *result = NULL;
    HANDLE memory = GetClipboardData(CF_UNICODETEXT);
    size_t byte_len = memory ? GlobalSize(memory) : 0;
    if (!byte_len)
        goto close_clipboard;
    const wchar_t *wide = GlobalLock(memory);
    if (!wide)
        goto close_clipboard;
    result = clipboard_text_utf8(wide, byte_len, out_len);
    GlobalUnlock(memory);
close_clipboard:
    CloseClipboard();
    return result;
}

static int copy_native(const char *text, size_t text_len)
{
    if (text_len > INT_MAX || memchr(text, '\0', text_len))
        return -1;
    char *terminated = xmalloc(text_len + 1);
    memcpy(terminated, text, text_len);
    terminated[text_len] = '\0';
    wchar_t *wide = win_utf8_to_wide(terminated);
    free(terminated);
    if (!wide)
        return -1;
    size_t length = (wcslen(wide) + 1) * sizeof(*wide);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, length);
    int result = -1;
    if (!memory)
        goto free_wide;
    void *bytes = GlobalLock(memory);
    if (!bytes)
        goto free_memory;
    memcpy(bytes, wide, length);
    GlobalUnlock(memory);
    /* SetClipboardData requires a window owner after EmptyClipboard. */
    HWND owner = CreateWindowExW(0, L"STATIC", L"hax clipboard", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL,
                                 GetModuleHandleW(NULL), NULL);
    if (!owner)
        goto free_memory;
    if (open_clipboard(owner, monotonic_ms() + CLIPBOARD_PASTE_TIMEOUT_MS) == 0) {
        if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory)) {
            memory = NULL; /* ownership transferred to the clipboard */
            result = 0;
        }
        CloseClipboard();
    }
    DestroyWindow(owner);
free_memory:
    if (memory)
        GlobalFree(memory);
free_wide:
    free(wide);
    return result;
}

static int copy_osc52(const char *text, size_t text_len)
{
    size_t length;
    char *sequence = clipboard_osc52_sequence(text, text_len, getenv("TMUX") != NULL, &length);
    if (!sequence)
        return -1;
    int fd = _wopen(L"CONOUT$", _O_WRONLY | _O_BINARY | _O_NOINHERIT);
    int result = fd_write_all(fd >= 0 ? fd : STDOUT_FILENO, sequence, length);
    if (fd >= 0)
        close(fd);
    free(sequence);
    return result;
}

int clipboard_copy(const char *text, size_t text_len, const char **error)
{
    int remote = getenv("SSH_TTY") != NULL || getenv("SSH_CONNECTION") != NULL;
    if (!remote && copy_native(text, text_len) == 0)
        return 0;
    if (copy_osc52(text, text_len) == 0)
        return 0;
    if (error)
        *error = remote && text_len > CLIPBOARD_OSC52_MAX_BYTES
                     ? "response too large for OSC 52 over SSH"
                     : "clipboard unavailable and terminal did not accept OSC 52 sequence";
    return -1;
}
