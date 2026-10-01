/*
 * Headless capture: --record out.mp4 [--frames N] [--fps N].
 *
 * Civ3 does not draw through DirectDraw or D3D. Its 2D library, jgl.dll (a
 * native DLL the host does not lift), renders into DIB sections and presents
 * them to its window with GDI BitBlt / StretchBlt. A hidden window clips every
 * one of those blits away, so there is nothing to read back from the window.
 * Instead jgl's own import table is patched when it loads: each blit to a
 * window DC also lands in a shadow bitmap the size of the game's client area,
 * and a host thread writes that bitmap to ffmpeg as raw BGRX at a fixed rate.
 *
 * The same IAT pass carries jgl's half of --headless (it creates, shows and
 * resizes the game window and switches display modes itself, none of which
 * goes through the guest's shimmed imports) and the 16-bit colour shim below,
 * which every run needs. docs/host.md has the reasoning.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "record.h"

static const char* g_record;
static char g_record_full[MAX_PATH];
static long g_record_frames, g_fps = 15, g_recorded;
static int g_closed;                 /* once closed, never reopened (that would truncate the file) */
static FILE* g_ffmpeg;
static HDC g_shadow_dc;
static HBITMAP g_shadow_bmp;
static void* g_shadow_bits;
static int g_w, g_h;
static CRITICAL_SECTION g_lock;
volatile LONG g_blits;

int record_arg(int argc, char** argv, int i) {
    if (i + 1 >= argc) return 0;
    if (!strcmp(argv[i], "--record")) { g_record = argv[i + 1]; return 2; }
    if (!strcmp(argv[i], "--frames")) { g_record_frames = strtol(argv[i + 1], NULL, 0); return 2; }
    if (!strcmp(argv[i], "--fps")) { g_fps = strtol(argv[i + 1], NULL, 0); return 2; }
    return 0;
}

void record_resolve_path(void) {
    InitializeCriticalSection(&g_lock);        /* before any thread can call record_close */
    if (g_record) { GetFullPathNameA(g_record, MAX_PATH, g_record_full, NULL); g_record = g_record_full; }
}

/* The shadow is sized on the first blit (the game settles its resolution
 * before it presents). ponytail: fixed after that, a mid-run resolution change
 * would need the ffmpeg pipe reopened. */
static void shadow_ensure(int w, int h) {
    if (g_shadow_dc || w <= 0 || h <= 0) return;
    BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB } };
    g_shadow_dc = CreateCompatibleDC(NULL);
    g_shadow_bmp = CreateDIBSection(g_shadow_dc, &bi, DIB_RGB_COLORS, &g_shadow_bits, NULL, 0);
    SelectObject(g_shadow_dc, g_shadow_bmp);
    g_w = w; g_h = h;
    fprintf(stderr, "[record] game surface %dx%d\n", w, h);
}

static int is_game_dc(HDC dc) {
    HWND w = WindowFromDC(dc);
    return w && g_game_hwnd && (w == g_game_hwnd || IsChild(g_game_hwnd, w));
}

static void shadow_size(void) {
    if (!g_record) return;
    RECT r;
    if (!g_shadow_dc && GetClientRect(g_game_hwnd, &r)) shadow_ensure(r.right, r.bottom);
}

static BOOL WINAPI hk_BitBlt(HDC d, int x, int y, int w, int h, HDC s, int sx, int sy, DWORD rop) {
    BOOL ok = BitBlt(d, x, y, w, h, s, sx, sy, rop);
    if (is_game_dc(d)) {
        EnterCriticalSection(&g_lock);
        shadow_size();
        if (g_shadow_dc) BitBlt(g_shadow_dc, x, y, w, h, s, sx, sy, rop);
        LeaveCriticalSection(&g_lock);
        if (InterlockedIncrement(&g_blits) == 1) fprintf(stderr, "[record] first blit to the game window\n");
    }
    return ok;
}

static BOOL WINAPI hk_StretchBlt(HDC d, int x, int y, int w, int h, HDC s, int sx, int sy,
                                 int sw, int sh, DWORD rop) {
    BOOL ok = StretchBlt(d, x, y, w, h, s, sx, sy, sw, sh, rop);
    if (is_game_dc(d)) {
        EnterCriticalSection(&g_lock);
        shadow_size();
        if (g_shadow_dc) StretchBlt(g_shadow_dc, x, y, w, h, s, sx, sy, sw, sh, rop);
        LeaveCriticalSection(&g_lock);
        if (InterlockedIncrement(&g_blits) == 1) fprintf(stderr, "[record] first blit to the game window\n");
    }
    return ok;
}

/* jgl's half of --headless. */
static HWND WINAPI hk_CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR name, DWORD style, int x, int y,
                                      int w, int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID p) {
    HWND r = CreateWindowExA(ex, cls, name, style & ~WS_VISIBLE, x, y, w, h, parent, menu, inst, p);
    fprintf(stderr, "[window] jgl CreateWindowExA(\"%s\", %dx%d) -> hidden %p\n", name ? name : "", w, h, (void*)r);
    if (r && !parent) g_game_hwnd = r;
    return r;
}
static BOOL WINAPI hk_ShowWindow(HWND h, int cmd) { (void)h; (void)cmd; return FALSE; }
static int WINAPI hk_MessageBoxA(HWND h, LPCSTR text, LPCSTR cap, UINT type) {
    (void)h;
    fprintf(stderr, "[messagebox] jgl: %s: %s\n", cap ? cap : "", text ? text : "");
    return (type & MB_TYPEMASK) == MB_YESNO || (type & MB_TYPEMASK) == MB_YESNOCANCEL ? IDNO : IDOK;
}

/* 16-bit colour. jgl picks its full-screen mode by finding width x height at
 * 16 bpp in the EnumDisplaySettings list, and Windows 8 and later list no
 * 16-bit modes at all, so the lookup fails and WinMain quits silently. The
 * real Civ3Conquests.exe only works because the app-compat database applies
 * a 16-bit-colour shim to it by file name; this host is not that file. So the
 * shim is done here: every mode is listed twice, as itself and as 16 bpp, and
 * a switch to 16 bpp becomes a switch to the same mode at the desktop's depth.
 * jgl draws into its own DIB sections and presents with GDI, which converts. */
static int g_headless_display;

/* Headless, the modes are a fixed list rather than the real display's: over
 * RDP the only mode is the client's (a phone's 1806x972), there is no
 * 1024x768 to find, and the game quits exactly as it did without the 16-bit
 * shim. Nothing is shown, so any listed mode is as good as another. */
static const DWORD g_fake_modes[][2] = { {640, 480}, {800, 600}, {1024, 768}, {1280, 1024}, {1920, 1080} };
#define N_FAKE (sizeof g_fake_modes / sizeof g_fake_modes[0])

static BOOL WINAPI hk_EnumDisplaySettingsA(LPCSTR dev, DWORD i, DEVMODEA* dm) {
    if (g_headless_display) {
        DWORD k = (i == ENUM_CURRENT_SETTINGS || i == ENUM_REGISTRY_SETTINGS) ? N_FAKE - 1 : i / 2;
        if (k >= N_FAKE) return FALSE;
        memset(dm, 0, sizeof *dm);
        dm->dmSize = sizeof *dm;
        dm->dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
        dm->dmPelsWidth = g_fake_modes[k][0];
        dm->dmPelsHeight = g_fake_modes[k][1];
        dm->dmBitsPerPel = (i & 1) && (int)i >= 0 ? 16 : 32;
        dm->dmDisplayFrequency = 60;
        return TRUE;
    }
    if (i == ENUM_CURRENT_SETTINGS || i == ENUM_REGISTRY_SETTINGS) return EnumDisplaySettingsA(dev, i, dm);
    if (!EnumDisplaySettingsA(dev, i / 2, dm)) return FALSE;
    if (i & 1) dm->dmBitsPerPel = 16;
    return TRUE;
}


static LONG WINAPI hk_ChangeDisplaySettingsA(DEVMODEA* dm, DWORD fl) {
    if (g_headless_display) {
        if (dm) fprintf(stderr, "[headless] ChangeDisplaySettingsA(%lux%lu x%lu) -> ignored\n",
                        dm->dmPelsWidth, dm->dmPelsHeight, dm->dmBitsPerPel);
        return DISP_CHANGE_SUCCESSFUL;
    }
    if (dm && (dm->dmFields & DM_BITSPERPEL) && dm->dmBitsPerPel < 32) {
        DEVMODEA cur = { .dmSize = sizeof cur };
        EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &cur);
        dm->dmBitsPerPel = cur.dmBitsPerPel;
    }
    return ChangeDisplaySettingsA(dm, fl);
}

/* Replace one import of module m, by name, in place. */
static int iat_patch(HMODULE m, const char* name, void* fn) {
    BYTE* b = (BYTE*)m;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(b + ((IMAGE_DOS_HEADER*)b)->e_lfanew);
    IMAGE_DATA_DIRECTORY dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dd.VirtualAddress) return 0;
    for (IMAGE_IMPORT_DESCRIPTOR* d = (IMAGE_IMPORT_DESCRIPTOR*)(b + dd.VirtualAddress); d->Name; d++) {
        IMAGE_THUNK_DATA* names = (IMAGE_THUNK_DATA*)(b + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        IMAGE_THUNK_DATA* slots = (IMAGE_THUNK_DATA*)(b + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, slots++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            if (strcmp((char*)((IMAGE_IMPORT_BY_NAME*)(b + names->u1.AddressOfData))->Name, name)) continue;
            DWORD old;
            VirtualProtect(&slots->u1.Function, 4, PAGE_READWRITE, &old);
            slots->u1.Function = (DWORD)(uintptr_t)fn;
            VirtualProtect(&slots->u1.Function, 4, old, &old);
            return 1;
        }
    }
    return 0;
}

void record_hook_module(HMODULE m, int headless) {
    g_headless_display = headless;
    iat_patch(m, "EnumDisplaySettingsA", (void*)hk_EnumDisplaySettingsA);
    iat_patch(m, "ChangeDisplaySettingsA", (void*)hk_ChangeDisplaySettingsA);
    if (g_record || headless) {        /* headless counts frames: input.c starts on the first */
        iat_patch(m, "BitBlt", (void*)hk_BitBlt);
        iat_patch(m, "StretchBlt", (void*)hk_StretchBlt);
    }
    if (headless) {
        iat_patch(m, "CreateWindowExA", (void*)hk_CreateWindowExA);
        iat_patch(m, "ShowWindow", (void*)hk_ShowWindow);
        iat_patch(m, "MessageBoxA", (void*)hk_MessageBoxA);
        iat_patch(m, "GetCursorPos", (void*)input_GetCursorPos);
    }
}

/* Under the lock: the watchdog closes the pipe from its own thread, and a
 * close in the middle of the recorder's fwrite ended the process with
 * 0xC0000409 instead of the watchdog's exit code. */
void record_close(void) {
    EnterCriticalSection(&g_lock);
    if (g_ffmpeg) {
        _pclose(g_ffmpeg);
        g_ffmpeg = NULL;
        g_closed = 1;
        fprintf(stderr, "[record] %ld frames -> %s\n", g_recorded, g_record);
    }
    LeaveCriticalSection(&g_lock);
}

static DWORD WINAPI recorder(LPVOID unused) {
    (void)unused;
    DWORD period = 1000 / (g_fps > 0 ? g_fps : 15), next = GetTickCount();
    for (;;) {
        next += period;
        DWORD now = GetTickCount();
        if ((LONG)(next - now) > 0) Sleep(next - now);
        EnterCriticalSection(&g_lock);
        if (g_shadow_dc && !g_ffmpeg && !g_closed) {
            char cmd[MAX_PATH * 2];
            _snprintf(cmd, sizeof cmd - 1, "ffmpeg -y -loglevel error -f rawvideo -pix_fmt bgr0 "
                      "-s %dx%d -r %ld -i - -c:v libx264 -pix_fmt yuv420p \"%s\"", g_w, g_h, g_fps, g_record);
            g_ffmpeg = _popen(cmd, "wb");
            fprintf(stderr, "[record] %dx%d at %ld fps -> %s\n", g_w, g_h, g_fps, g_record);
        }
        if (g_ffmpeg) {
            GdiFlush();
            fwrite(g_shadow_bits, 4, (size_t)g_w * g_h, g_ffmpeg);
            g_recorded++;
        }
        LeaveCriticalSection(&g_lock);
        if (g_record_frames && g_recorded >= g_record_frames) {
            record_close();
            fflush(stderr);
            TerminateProcess(GetCurrentProcess(), 0);
        }
    }
}

void record_start(void) {
    if (g_record) CloseHandle(CreateThread(NULL, 0, recorder, NULL, 0, NULL));
}
