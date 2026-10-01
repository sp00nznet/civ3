/*
 * Civilization III: Conquests - static recompilation host.
 *
 * A 32-bit host on pcrecomp's runtime/native32 (the native bridge, callbacks
 * and machine lock; see its header). Only the game-specific parts live here:
 * where the image goes, which imports are shimmed, the command line, and the
 * fault report. docs/host.md has the reasoning.
 *
 * Only Civ3Conquests.exe is lifted. jgl.dll (the 2D blitter), sound.dll,
 * binkw32, mss32, IFC23 and steam_api are loaded as the real 32-bit DLLs by
 * native32_bind, exactly as the Windows loader would.
 *
 * Linked at /BASE:0x60000000 (CMakeLists.txt) so 0x00400000..0x00CF7000 is
 * free when main() maps the image.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "native32.h"
#include "recomp_trace.h"
#include "record.h"

extern const uint32_t civ3_entry_va;   /* recomp_dispatch.c */

#define CIV3_IMAGE_BASE 0x00400000u

static DWORD g_watchdog_s;
static int   g_headless;

#define ARG(n) MEM32(g_esp + 4 + 4 * (n))
static const char* gstr(uint32_t va) { return va ? (const char*)(uintptr_t)va : "(null)"; }

/* ------------------------------------------------------------ guest identity */

/* The guest is Civ3Conquests.exe in game\Conquests, not this host. The game
 * finds its data (..\Art, Text\, the .bic) from GetModuleFileNameA and its
 * hInstance (icon, cursors, dialogs) from GetModuleHandleA(NULL). */
static char g_guest_exe[MAX_PATH], g_guest_cmdline[MAX_PATH + 3];

static void shim_GetModuleHandleA(void) {
    g_eax = ARG(0) ? (uint32_t)(uintptr_t)GetModuleHandleA((LPCSTR)(uintptr_t)ARG(0))
                   : CIV3_IMAGE_BASE;
    g_esp += 4 + 1 * 4;
}

static void shim_GetModuleFileNameA(void) {
    uint32_t h = ARG(0), size = ARG(2);
    char* out = (char*)(uintptr_t)ARG(1);
    if (h == 0 || h == CIV3_IMAGE_BASE) {
        uint32_t n = (uint32_t)strlen(g_guest_exe);
        if (size) {
            uint32_t k = n < size ? n : size - 1;
            memcpy(out, g_guest_exe, k);
            out[k] = 0;
            n = k;
        }
        g_eax = n;
    } else {
        g_eax = GetModuleFileNameA((HMODULE)(uintptr_t)h, out, size);
    }
    g_esp += 4 + 3 * 4;
}

static void shim_GetCommandLineA(void) {
    g_eax = (uint32_t)(uintptr_t)g_guest_cmdline;
    g_esp += 4;
}

/* ------------------------------------------------------------------- headless */

/* --headless: nothing reaches the screen. Over RDP a window lands on whatever
 * device is connected (REPO_RULES section 13), so a message box prints, and the
 * game's window is real but never shown. record.c captures what the game draws
 * into it. */
static uint32_t mb_answer(uint32_t type) {
    uint32_t buttons = type & MB_TYPEMASK;
    return (buttons == MB_YESNO || buttons == MB_YESNOCANCEL) ? IDNO : IDOK;
}

static void shim_MessageBoxA(void) {
    g_eax = mb_answer(ARG(3));
    fprintf(stderr, "[messagebox] type 0x%X -> %u: %s: %s\n", ARG(3), g_eax, gstr(ARG(2)), gstr(ARG(1)));
    g_esp += 4 + 4 * 4;
}

HWND g_game_hwnd;

static void shim_CreateWindowExA(void) {
    DWORD style = ARG(3);
    if (g_headless) style &= ~WS_VISIBLE;
    HWND h = CreateWindowExA(ARG(0), (LPCSTR)(uintptr_t)ARG(1), (LPCSTR)(uintptr_t)ARG(2),
                             style, (int)ARG(4), (int)ARG(5), (int)ARG(6),
                             (int)ARG(7), (HWND)(uintptr_t)ARG(8), (HMENU)(uintptr_t)ARG(9),
                             (HINSTANCE)(uintptr_t)ARG(10), (LPVOID)(uintptr_t)ARG(11));
    fprintf(stderr, "[window] CreateWindowExA(\"%s\", style 0x%08X, %dx%d) from sub_%08X -> %p\n",
            gstr(ARG(2)), ARG(3), (int)ARG(6), (int)ARG(7), g_cur_func, (void*)h);
    if (h && !ARG(8)) g_game_hwnd = h;          /* the top-level one */
    g_eax = (uint32_t)(uintptr_t)h;
    g_esp += 4 + 12 * 4;
}

static void shim_ShowWindow(void) {
    g_eax = 0;                         /* "was hidden", which is true */
    g_esp += 4 + 2 * 4;
}

/* jgl.dll and sound.dll come in through LoadLibraryA. jgl creates and
 * presents the game window itself, so its imports are patched as it loads
 * (record.c): the headless half, and the blit mirror for --record. */
static void shim_LoadLibraryA(void) {
    HMODULE m = LoadLibraryA((LPCSTR)(uintptr_t)ARG(0));
    fprintf(stderr, "[load] %s -> %p\n", gstr(ARG(0)), (void*)m);
    if (m) record_hook_module(m, g_headless);
    g_eax = (uint32_t)(uintptr_t)m;
    g_esp += 4 + 1 * 4;
}

#define GUEST_SHIMS \
    { "GetModuleHandleA", shim_GetModuleHandleA }, \
    { "GetModuleFileNameA", shim_GetModuleFileNameA }, \
    { "GetCommandLineA", shim_GetCommandLineA }, \
    { "CreateWindowExA", shim_CreateWindowExA }, \
    { "LoadLibraryA", shim_LoadLibraryA }

static native32_shim_t g_shims[] = { GUEST_SHIMS };

static native32_shim_t g_headless_shims[] = {
    GUEST_SHIMS,
    { "MessageBoxA", shim_MessageBoxA },
    { "ShowWindow", shim_ShowWindow },
};

/* ------------------------------------------------------------- diagnostics */

/* --probe VA (repeatable): report indirect calls to VA -- a virtual method or
 * callback -- with `this` and the first arguments. RECOMP_ICALL asks this
 * hook before the dispatch table, so it costs nothing when unset. */
#define MAX_PROBES 8
static uint32_t g_probe[MAX_PROBES];
static int g_nprobe;
static volatile LONG g_probe_hits[MAX_PROBES];

recomp_func_t recomp_lookup_manual(uint32_t va) {
    for (int i = 0; i < g_nprobe; i++)
        if (g_probe[i] == va && InterlockedIncrement(&g_probe_hits[i]) <= 5)
            fprintf(stderr, "[probe] sub_%08X from sub_%08X  ecx=%08X  args %08X %08X %08X\n",
                    va, g_cur_func, g_ecx, MEM32(g_esp), MEM32(g_esp + 4), MEM32(g_esp + 8));
    return NULL;
}

static void probe_report(void) {
    for (int i = 0; i < g_nprobe; i++)
        fprintf(stderr, "[probe] sub_%08X: %ld calls\n", g_probe[i], g_probe_hits[i]);
}

void recomp_not_lifted(uint32_t va) {
    fprintf(stderr,
        "\n[not-lifted] sub_%08X  (called from 0x%08X)\n"
        "  Widen the closure:  py -3 run_lift.py --roots 0x%08X  (or --max N, or --all)\n",
        va, g_cur_func, va);
    recomp_dump_trace("not-lifted");
    native32_dump_icalls(8);
    record_close();
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 2);
}

/* Added after native32's own handler, so callbacks are resolved first and only
 * real faults get here. The report goes out through WriteFile from a static
 * buffer, not stdio: a fault while another thread holds the CRT's stderr lock,
 * or a stack overflow, otherwise ends the process with no report (themovies). */
static char g_crash_buf[4096];
static int g_crash_len;
static void crash_emit(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf(g_crash_buf + g_crash_len, sizeof g_crash_buf - 1 - g_crash_len, fmt, ap);
    va_end(ap);
    if (n > 0) g_crash_len += n;
}

static LONG CALLBACK crash(EXCEPTION_POINTERS* ep) {
    static volatile LONG once;
    EXCEPTION_RECORD* r = ep->ExceptionRecord;
    if ((r->ExceptionCode & 0xF0000000u) != 0xC0000000u) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedExchange(&once, 1)) TerminateProcess(GetCurrentProcess(), 3);
    crash_emit("\n=== fault 0x%08lX at 0x%p, thread %lu ===\n", r->ExceptionCode,
               r->ExceptionAddress, GetCurrentThreadId());
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2) {
        ULONG_PTR op = r->ExceptionInformation[0];
        uint32_t at = (uint32_t)r->ExceptionInformation[1];
        crash_emit("  %s of 0x%08X%s\n", op == 0 ? "read" : op == 1 ? "write" : "execute", at,
                   native32_in_guest(at) ? " (inside the guest image)" : at < 0x10000 ? " (null/low)" : "");
    }
    crash_emit("  in lifted sub_%08X, last native call %s\n", g_cur_func, g_cur_import);
    crash_emit("  eax=%08X ecx=%08X edx=%08X ebx=%08X esp=%08X ebp=%08X esi=%08X edi=%08X\n",
               g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi);
    crash_emit("last indirect calls (newest first):\n");
    for (int i = 1; i <= 12 && i <= (int)g_icall_trace_idx; i++) {
        uint32_t k = (g_icall_trace_idx - i) & (ICALL_TRACE_SIZE - 1);
        const char* nm = native32_name(g_icall_trace[k]);
        crash_emit("  0x%08X  from 0x%08X  %s\n", g_icall_trace[k], g_icall_from[k], nm ? nm : "");
    }
    DWORD w;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), g_crash_buf, (DWORD)g_crash_len, &w, NULL);
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_CONTINUE_SEARCH;
}

static DWORD WINAPI watchdog(LPVOID unused) {
    (void)unused;
    Sleep(g_watchdog_s * 1000);
    fprintf(stderr, "\n[watchdog] %lu s: in sub_%08X, last native call %s, %u indirect calls\n",
            g_watchdog_s, g_cur_func, g_cur_import, g_icall_count);
    native32_dump_icalls(8);
    probe_report();
    record_close();
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 4);
    return 0;
}

int main(int argc, char** argv) {
    const char* exe = "work\\Civ3Conquests.exe";
    const char* game = "game\\Conquests";
    char exe_full[MAX_PATH], gd[MAX_PATH];
    int run = 0;
    for (int i = 1; i < argc; i++) {
        int n = recomp_trace_arg(argc, argv, i);
        if (!n) n = record_arg(argc, argv, i);
        if (!n) n = input_arg(argc, argv, i);
        if (n) { i += n - 1; continue; }
        if (!strcmp(argv[i], "--run")) run = 1;
        else if (!strcmp(argv[i], "--headless")) g_headless = 1;
        else if (!strcmp(argv[i], "--probe") && i + 1 < argc && g_nprobe < MAX_PROBES)
            g_probe[g_nprobe++] = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--exe") && i + 1 < argc) exe = argv[++i];
        else if (!strcmp(argv[i], "--game") && i + 1 < argc) game = argv[++i];
        else if (!strcmp(argv[i], "--watchdog") && i + 1 < argc) g_watchdog_s = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--native-trace")) native32_trace_native = 1;
        else if (!strcmp(argv[i], "--callbacks")) native32_trace_callbacks = 1;
        else {
            printf("usage: civ3 [--run] [--headless] [--record out.mp4] [--frames N] [--fps N]\n"
                   "            [--move x,y@s] [--click x,y@s] [--key vk@s]\n"
                   "            [--exe work\\Civ3Conquests.exe] [--game game\\Conquests]\n"
                   "            [--watchdog S] [--probe VA] [--native-trace] [--callbacks]\n");
            recomp_trace_help();
            return argv[i][1] == 'h' || argv[i][2] == 'h' ? 0 : 1;
        }
    }
    GetFullPathNameA(exe, MAX_PATH, exe_full, NULL);
    GetFullPathNameA(game, MAX_PATH, gd, NULL);
    /* The game keeps its folder in fixed-size buffers: from a folder ~150
     * characters deep it loads, draws nothing, and spins forever, while the
     * same files through a short junction reach the menu. The 8.3 form keeps
     * any install location short enough. No-op where 8.3 names are off. */
    {
        char sh[MAX_PATH];
        DWORD n = GetShortPathNameA(gd, sh, MAX_PATH);
        if (n && n < MAX_PATH) strcpy(gd, sh);
    }
    record_resolve_path();             /* the run chdirs into the game folder */
    _snprintf(g_guest_exe, sizeof g_guest_exe - 1, "%s\\Civ3Conquests.exe", gd);
    _snprintf(g_guest_cmdline, sizeof g_guest_cmdline - 1, "\"%s\"", g_guest_exe);

    /* The game's DLLs (jgl, sound, binkw32, steam_api...) sit beside the guest
     * exe, and native32_bind loads them with LoadLibrary. */
    SetDllDirectoryA(gd);
    native32_init();
    AddVectoredExceptionHandler(0, crash);
    printf("Civilization III: Conquests recomp host\n  lifted functions in dispatch: %u\n",
           recomp_dispatch_count);

    uint32_t span = native32_map(exe_full, CIV3_IMAGE_BASE);
    if (!span) { fprintf(stderr, "cannot map %s at 0x%08X\n", exe_full, CIV3_IMAGE_BASE); return 1; }
    printf("  mapped %s: 0x%08X-0x%08X\n", exe, CIV3_IMAGE_BASE, CIV3_IMAGE_BASE + span);
    int bad = g_headless
        ? native32_bind(CIV3_IMAGE_BASE, g_headless_shims, (int)(sizeof g_headless_shims / sizeof g_headless_shims[0]))
        : native32_bind(CIV3_IMAGE_BASE, g_shims, (int)(sizeof g_shims / sizeof g_shims[0]));
    if (bad) return 1;
    printf("  guest exe %s\n", g_guest_exe);

    if (!run) {
        printf("\n(dry run: image mapped and bound; --run enters 0x%08X)\n", civ3_entry_va);
        return 0;
    }
    if (!SetCurrentDirectoryA(gd)) { fprintf(stderr, "cannot enter %s\n", gd); return 1; }
    if (g_watchdog_s) CloseHandle(CreateThread(NULL, 0, watchdog, NULL, 0, NULL));
    record_start();
    input_start();
    printf("  entering 0x%08X\n\n", civ3_entry_va);
    fflush(stdout);
    native32_call_guest(civ3_entry_va, 0, NULL);
    printf("\nentry returned eax=%08X\n", g_eax);
    record_close();
    return (int)g_eax;
}
