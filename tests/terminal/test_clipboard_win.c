/* SPDX-License-Identifier: MIT */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdlib.h>
#include <string.h>
#include <wincodec.h>

#include "env.h"
#include "harness.h"
#include "system/clock.h"
#include "terminal/clipboard.h"
#include "terminal/clipboard_win.h"

static void expect_png_pixels(const char *png, size_t length)
{
    IStream *stream = NULL;
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *converter = NULL;
    EXPECT(SUCCEEDED(CreateStreamOnHGlobal(NULL, TRUE, &stream)));
    if (!stream)
        return;
    ULONG written;
    EXPECT(SUCCEEDED(IStream_Write(stream, png, (ULONG)length, &written)));
    EXPECT(written == length);
    LARGE_INTEGER start = {0};
    EXPECT(SUCCEEDED(IStream_Seek(stream, start, STREAM_SEEK_SET, NULL)));
    EXPECT(SUCCEEDED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                      &IID_IWICImagingFactory, (void **)&factory)));
    if (!factory)
        goto out;
    EXPECT(SUCCEEDED(IWICImagingFactory_CreateDecoderFromStream(
        factory, stream, NULL, WICDecodeMetadataCacheOnLoad, &decoder)));
    if (!decoder)
        goto out;
    EXPECT(SUCCEEDED(IWICBitmapDecoder_GetFrame(decoder, 0, &frame)));
    if (!frame)
        goto out;
    UINT width = 0, height = 0;
    EXPECT(SUCCEEDED(IWICBitmapFrameDecode_GetSize(frame, &width, &height)));
    EXPECT(width == 2 && height == 2);
    EXPECT(SUCCEEDED(IWICImagingFactory_CreateFormatConverter(factory, &converter)));
    if (!converter)
        goto out;
    EXPECT(SUCCEEDED(IWICFormatConverter_Initialize(
        converter, (IWICBitmapSource *)frame, &GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone,
        NULL, 0, WICBitmapPaletteTypeCustom)));
    unsigned char pixels[12] = {0};
    EXPECT(SUCCEEDED(IWICFormatConverter_CopyPixels(converter, NULL, 6, sizeof(pixels), pixels)));
    const unsigned char expected[] = {0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255};
    EXPECT(memcmp(pixels, expected, sizeof(expected)) == 0);
out:
    if (converter)
        IWICFormatConverter_Release(converter);
    if (frame)
        IWICBitmapFrameDecode_Release(frame);
    if (decoder)
        IWICBitmapDecoder_Release(decoder);
    if (factory)
        IWICImagingFactory_Release(factory);
    IStream_Release(stream);
}

static void test_bitmap_png(int top_down)
{
    BITMAPINFO info = {0};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = 2;
    info.bmiHeader.biHeight = top_down ? -2 : 2;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *bytes = NULL;
    HBITMAP bitmap = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, &bytes, NULL, 0);
    EXPECT(bitmap != NULL);
    if (!bitmap)
        return;
    /* Undefined alpha bytes must not make ordinary clipboard RGB pixels transparent. */
    const unsigned char top[] = {0, 0, 255, 0, 0, 255, 0, 17};
    const unsigned char bottom[] = {255, 0, 0, 0, 255, 255, 255, 255};
    memcpy(bytes, top_down ? top : bottom, 8);
    memcpy((char *)bytes + 8, top_down ? bottom : top, 8);
    for (int i = 0; i < 2; i++) {
        size_t length = 0;
        char *png = clipboard_bitmap_png(bitmap, &length);
        EXPECT(png != NULL);
        if (png) {
            EXPECT(length > 8);
            EXPECT(memcmp(png, "\x89PNG\r\n\x1a\n", 8) == 0);
            expect_png_pixels(png, length);
            free(png);
        }
    }
    BITMAP retained;
    EXPECT(GetObjectW(bitmap, sizeof(retained), &retained) == sizeof(retained));
    DeleteObject(bitmap);
}

static void test_bounded_unicode_text(void)
{
    const wchar_t text[] = {'a', 0xe9, 0xd83d, 0xde00, '\r', '\n', 0};
    size_t length = 0;
    char *utf8 = clipboard_text_utf8(text, sizeof(text), &length);
    EXPECT(utf8 != NULL);
    if (utf8) {
        EXPECT_STR_EQ(utf8, "a\xc3\xa9\xf0\x9f\x98\x80\r\n");
        EXPECT(length == 9);
        free(utf8);
    }
    EXPECT(clipboard_text_utf8(text, sizeof(text) - sizeof(*text), &length) == NULL);
    EXPECT(clipboard_text_utf8(text, sizeof(text) - 1, &length) == NULL);
    EXPECT(clipboard_text_utf8(text, (2u << 20) + 2, &length) == NULL);
    EXPECT(clipboard_text_utf8(NULL, 0, &length) == NULL);
    EXPECT(clipboard_text_utf8(L"", sizeof(wchar_t), &length) == NULL);
    const wchar_t malformed[] = {0xd83d, 0};
    EXPECT(clipboard_text_utf8(malformed, sizeof(malformed), &length) == NULL);
}

static void test_private_clipboard(void)
{
    HWINSTA original_station = GetProcessWindowStation();
    HDESK original_desktop = GetThreadDesktop(GetCurrentThreadId());
    wchar_t name[80];
    swprintf(name, sizeof(name) / sizeof(*name), L"hax-clipboard-%lu-%llu",
             (unsigned long)GetCurrentProcessId(), (unsigned long long)GetTickCount64());
    /* Each window station has its own clipboard, separate from the user's interactive station. */
    HWINSTA station = CreateWindowStationW(name, 0, WINSTA_ALL_ACCESS, NULL);
    if (!station) {
        if (GetLastError() == ERROR_ACCESS_DENIED)
            T_SKIP("window station creation denied; isolated clipboard API test unavailable");
        FAIL("CreateWindowStationW failed: %lu", (unsigned long)GetLastError());
        return;
    }
    if (!SetProcessWindowStation(station)) {
        EXPECT(0);
        goto close_station;
    }
    HDESK desktop = CreateDesktopW(L"test", NULL, NULL, 0, GENERIC_ALL, NULL);
    EXPECT(desktop != NULL);
    if (!desktop)
        goto restore_station;
    if (!SetThreadDesktop(desktop)) {
        EXPECT(0);
        goto close_desktop;
    }
    const char text[] = "native \xc3\xa9 \xf0\x9f\x98\x80\r\n";
    const char *error = NULL;
    EXPECT(clipboard_copy(text, sizeof(text) - 1, &error) == 0);
    EXPECT(IsClipboardFormatAvailable(CF_UNICODETEXT));
    size_t length = 0;
    char *pasted = clipboard_paste_text(&length, monotonic_ms() + 1000);
    EXPECT(pasted != NULL);
    if (pasted) {
        EXPECT_MEM_EQ(pasted, length, text, sizeof(text) - 1);
        free(pasted);
    }
    HWND owner = CreateWindowExW(0, L"STATIC", L"test", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL,
                                 GetModuleHandleW(NULL), NULL);
    EXPECT(owner != NULL);
    if (owner) {
        UINT format = RegisterClipboardFormatW(L"PNG");
        const unsigned char png_bytes[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 255};
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, sizeof(png_bytes));
        EXPECT(memory != NULL);
        if (memory) {
            void *bytes = GlobalLock(memory);
            EXPECT(bytes != NULL);
            if (bytes) {
                memcpy(bytes, png_bytes, sizeof(png_bytes));
                GlobalUnlock(memory);
                EXPECT(OpenClipboard(owner));
                EXPECT(EmptyClipboard());
                if (SetClipboardData(format, memory))
                    memory = NULL;
                else
                    EXPECT(0);
                CloseClipboard();
                char *image = clipboard_paste_image(&length, monotonic_ms() + 1000);
                EXPECT(image != NULL);
                if (image) {
                    /* Global allocations can be rounded up; the image bytes must survive intact. */
                    EXPECT(length >= sizeof(png_bytes));
                    EXPECT(memcmp(image, png_bytes, sizeof(png_bytes)) == 0);
                    free(image);
                }
            }
            if (memory)
                GlobalFree(memory);
        }
        DestroyWindow(owner);
    }
    EXPECT(SetProcessWindowStation(original_station));
    EXPECT(SetThreadDesktop(original_desktop));
    CloseDesktop(desktop);
    CloseWindowStation(station);
    return;
close_desktop:
    CloseDesktop(desktop);
restore_station:
    EXPECT(SetProcessWindowStation(original_station));
close_station:
    CloseWindowStation(station);
}

int main(void)
{
    t_env_unset("SSH_TTY");
    t_env_unset("SSH_CONNECTION");
    HRESULT initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    EXPECT(SUCCEEDED(initialized));
    if (FAILED(initialized))
        T_REPORT();
    size_t length = 17;
    EXPECT(clipboard_bitmap_png(NULL, &length) == NULL);
    EXPECT(length == 17);
    /* An already expired read must return without touching the user's clipboard. */
    EXPECT(clipboard_paste_text(&length, -1) == NULL);
    EXPECT(clipboard_paste_image(&length, -1) == NULL);
    test_bitmap_png(0);
    test_bitmap_png(1);
    test_bounded_unicode_text();
    CoUninitialize();
    test_private_clipboard();
    T_REPORT();
}
