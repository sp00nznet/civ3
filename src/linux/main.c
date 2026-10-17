/*
 * Civilization III: Conquests - the native Linux host.
 *
 * The same lifted C as the Windows build, on pcrecomp's runtime/win32hle
 * instead of runtime/native32: every Win32 call is answered by win32hle's
 * own implementation (files, windows, GDI, DirectSound on SDL2, Bink on
 * ffmpeg), so there is no Windows and no Wine.
 *
 * On Windows only Civ3Conquests.exe is lifted and the game's DLLs run as
 * themselves. Here nothing can run their machine code, so jgl.dll (the
 * renderer) and sound.dll are lifted too (run_lift.py --module) and loaded
 * by LoadLibraryA below: mapped at the base they were lifted at, their
 * imports bound to win32hle, their lifted DllMain run. The rest (Steam, the
 * force-feedback mouse, OpenGL) are answered here as absent, and Miles by
 * win32hle's mss.c.
 *
 * Linked non-PIE at 0x20000000 (CMakeLists.txt), so the guest's
 * 0x00400000..0x00CF7000 and the DLLs at 0x10000000 are free.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <unistd.h>
#include <limits.h>
#include <pthread.h>
#include <sys/mman.h>
#include <execinfo.h>

#include "win32hle.h"
#include "pe_loader.h"
#include "host.h"

#define CIV3_IMAGE_BASE 0x00400000u
#define CIV3_GUEST_EXE  "C:\\Conquests\\Civ3Conquests.exe"
#define MENU_FLAG_VA    0x00B64E1Eu   /* set while the main menu's modal loop runs (input.c) */
#define TURN_VA         0x00A74EA4u   /* the turn number, 0 at the menu (docs/testing.md) */

int  script_arg(int argc, char **argv, int i);   /* script.c */
void script_start(void);
void script_set_window(uint32_t hwnd);

int g_headless;
static unsigned g_watchdog_s, g_play_s;
volatile int g_menu_open;

/* ---- the screen: what jgl's window DCs draw into ------------------------- */

static uint32_t *g_screen;
static int g_screen_w, g_screen_h;
static void screen_mode(int w, int h) {
    if (w == g_screen_w && h == g_screen_h) return;
    uint32_t *px = (uint32_t *)calloc((size_t)w * h, 4);
    hle_gdi_window_surface((uint8_t *)px, w, h, w * 4, 32);
    hle_set_screen_size(w, h);
    g_screen = px, g_screen_w = w, g_screen_h = h;   /* ponytail: the old one leaks; modes change once or twice a run */
    fprintf(stderr, "[screen] %dx%d\n", w, h);
}

/* Per mille of sampled pixels changed since the last snapshot (--open). */
int screen_changed(int snapshot) {
    static uint32_t snap[4096];
    if (!g_screen) return -1;
    int changed = 0;
    for (int i = 0; i < 4096; i++) {
        uint32_t v = g_screen[(size_t)(i * 7919u) % ((size_t)g_screen_w * g_screen_h)];
        if (snapshot) snap[i] = v;
        else changed += snap[i] != v;
    }
    return changed * 1000 / 4096;
}

/* jgl looks the mode it wants (1024x768 at 16 bpp) up in this list, then
 * sets it. Every mode is offered at 16 and 32 bpp, and setting one only
 * resizes the screen: blits convert from jgl's DIBs to it. */
static const int k_modes[][2] = { { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1152, 864 },
                                  { 1280, 1024 }, { 1600, 1200 }, { 1920, 1080 } };
static void put_mode(uint32_t dm, int w, int h, int bpp) {
    MEM32(dm + 40) = 0x001C0000u | 0x00400000u;   /* DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY */
    MEM32(dm + 104) = (uint32_t)bpp, MEM32(dm + 108) = (uint32_t)w, MEM32(dm + 112) = (uint32_t)h;
    MEM32(dm + 120) = 60;
}
static void c_EnumDisplaySettingsA(void) {             /* (device, mode, DEVMODEA*) */
    int32_t n = (int32_t)A32(1);
    uint32_t dm = A32(2);
    int count = (int)(sizeof k_modes / sizeof *k_modes);
    if (n < 0) { put_mode(dm, g_screen_w, g_screen_h, 32); RET(1, 3); }   /* ENUM_CURRENT/REGISTRY_SETTINGS */
    if (n >= 2 * count) RET(0, 3);
    put_mode(dm, k_modes[n / 2][0], k_modes[n / 2][1], n & 1 ? 32 : 16);
    RET(1, 3);
}
static void c_ChangeDisplaySettingsA(void) {           /* (DEVMODEA*, flags) */
    uint32_t dm = A32(0);
    if (dm) {
        fprintf(stderr, "[screen] ChangeDisplaySettingsA %ux%u %u bpp\n", MEM32(dm + 108), MEM32(dm + 112), MEM32(dm + 104));
        screen_mode((int)MEM32(dm + 108), (int)MEM32(dm + 112));
    }
    RET(0, 2);                                         /* DISP_CHANGE_SUCCESSFUL */
}
static void c_UnregisterClassA(void) { RET(1, 2); }

/* ---- the lifted DLLs ------------------------------------------------------ */

/* Their ranges are held from the start (main): the game loads them after
 * threads and file mappings have had the chance to land there. */
static struct { const char *name; uint32_t base, span; char file[PATH_MAX]; int loaded, held; } g_dll[] = {
    { "jgl.dll",   0x10000000u, 0x77000u, "", 0, 0 },
    { "sound.dll", 0x10080000u, 0xBB000u, "", 0, 0 },  /* rebased: jgl has its 0x10000000 */
};
#define NDLL (int)(sizeof g_dll / sizeof *g_dll)

static recomp_func_t hle_fn(const win32hle_shim *t, const char *name) {
    for (; t->name; t++) if (!strcmp(t->name, name)) return t->fn;
    return NULL;
}
static recomp_func_t g_hle_LoadLibraryA, g_hle_GetProcAddress, g_hle_CreateWindowExA;

static int lifted_dll(const char *name) {
    const char *b = strrchr(name, '\\');
    b = b ? b + 1 : name;
    for (int i = 0; i < NDLL; i++) if (!strcasecmp(b, g_dll[i].name)) return i;
    return -1;
}
static int dll_of_base(uint32_t h) {
    for (int i = 0; i < NDLL; i++) if (g_dll[i].loaded && g_dll[i].base == h) return i;
    return -1;
}

static void c_LoadLibraryA(void) {
    const char *name = ASTR(0);
    int i = name ? lifted_dll(name) : -1;
    if (i < 0) { g_hle_LoadLibraryA(); return; }
    if (!g_dll[i].loaded) {
        pe_image img;
        /* ponytail: another thread could map into the gap between these two
         * calls; a pe_loader call that maps over the hold would close it */
        if (g_dll[i].held) munmap((void *)(uintptr_t)g_dll[i].base, g_dll[i].span);
        if (recomp_pe_map(g_dll[i].file, &img) != 0 || img.base != g_dll[i].base)
            hle_fatal("cannot map %s at 0x%08X (run_lift.py --module, README)", g_dll[i].file, g_dll[i].base);
        char guest[64];
        snprintf(guest, sizeof guest, "C:\\Conquests\\%s", g_dll[i].name);
        hle_module_add(guest, img.base, img.resource_rva, img.resource_size, 0);
        int unresolved = recomp_pe_bind(&img, hle_resolve_or_stub);
        g_dll[i].loaded = 1;
        uint32_t args[3] = { img.base, 1, 0 };          /* DLL_PROCESS_ATTACH */
        uint32_t ok = hle_call_guest(img.entry, 3, args);
        fprintf(stderr, "[load] %s at 0x%08X, %d imports not implemented, DllMain -> %u\n", name, img.base, unresolved, ok);
    }
    RET(g_dll[i].base, 1);
}

/* A lifted DLL's export, by name or ordinal, from its export directory. */
static uint32_t export_of(uint32_t base, uint32_t which) {
    uint32_t rva = MEM32(base + MEM32(base + 0x3C) + 0x78);
    if (!rva) return 0;
    uint32_t ed = base + rva, nfun = MEM32(ed + 20), nnames = MEM32(ed + 24);
    uint32_t funcs = base + MEM32(ed + 28), names = base + MEM32(ed + 32), ords = base + MEM32(ed + 36);
    uint32_t idx = which - MEM32(ed + 16);
    if (which >= 0x10000u) {
        idx = ~0u;
        for (uint32_t k = 0; k < nnames; k++)
            if (!strcmp((const char *)(uintptr_t)(base + MEM32(names + 4 * k)), (const char *)(uintptr_t)which)) { idx = MEM16(ords + 2 * k); break; }
    }
    return idx < nfun ? base + MEM32(funcs + 4 * idx) : 0;
}
static void c_GetProcAddress(void) {
    if (dll_of_base(A32(0)) < 0) { g_hle_GetProcAddress(); return; }
    uint32_t va = export_of(A32(0), A32(1));
    if (!va) hle_set_last_error(127);                   /* ERROR_PROC_NOT_FOUND */
    RET(va, 2);
}

/* As on Windows (host.c): the game unloads jgl in WinMain and a static
 * destructor still reads an object jgl allocated, so nothing is unloaded. */
static void c_FreeLibrary(void) { RET(1, 1); }

/* jgl's window is the game's: the one scripts post to. sound.dll makes a
 * hidden one of its own, so the window is the top-level one jgl creates. */
static void c_CreateWindowExA(void) {
    uint32_t parent = A32(8), from = g_cur_func;
    uint32_t style = A32(3);
    if (g_headless) MEM32(g_esp + 4 + 4 * 3) = style & ~0x10000000u;   /* WS_VISIBLE */
    g_hle_CreateWindowExA();
    fprintf(stderr, "[window] CreateWindowExA style 0x%08X from sub_%08X -> 0x%08X\n", style, from, g_eax);
    if (g_eax && !parent && from >= 0x10000000u && from < 0x10080000u) script_set_window(g_eax);
}

/* ---- what the game links and does not need -------------------------------
 * Steam, the force-feedback mouse (IFC23) and the OpenGL overlay answer as
 * absent: SteamAPI_Init fails, as with no Steam client running (--nosteam on
 * Windows), and every device creation fails, which the game takes as "none
 * fitted". (Miles, sound.dll's music, is win32hle's mss.c.) */
#define CDECL0(fn)      static void fn(void) { RET(0, 0); }   /* 0, the caller pops */
#define STDCALL(fn, n)  static void fn(void) { RET(0, n); }   /* 0, n arguments popped */
CDECL0(c_cdecl0)
STDCALL(c_ret0_0, 0) STDCALL(c_ret0_1, 1) STDCALL(c_ret0_2, 2) STDCALL(c_ret0_3, 3) STDCALL(c_ret0_4, 4) STDCALL(c_ret0_12, 12)
static void c_ImmProject_ctor(void) { RET(g_ecx, 0); }  /* thiscall: this back */

static void c_MessageBoxA(void) {
    uint32_t buttons = A32(3) & 0xF;
    uint32_t answer = buttons == 4 || buttons == 3 ? (g_headless ? 7u : 6u) : 1u;   /* No headless, else Yes; OK */
    fprintf(stderr, "[messagebox] %s: %s -> %u\n", ASTR(2) ? ASTR(2) : "", ASTR(1) ? ASTR(1) : "", answer);
    RET(answer, 4);
}

static const win32hle_shim g_civ3_shims[] = {
    { "LoadLibraryA", c_LoadLibraryA }, { "GetProcAddress", c_GetProcAddress }, { "FreeLibrary", c_FreeLibrary },
    { "CreateWindowExA", c_CreateWindowExA }, { "MessageBoxA", c_MessageBoxA },
    { "EnumDisplaySettingsA", c_EnumDisplaySettingsA }, { "ChangeDisplaySettingsA", c_ChangeDisplaySettingsA },
    { "UnregisterClassA", c_UnregisterClassA },
    { "SteamAPI_Init", c_cdecl0 }, { "SteamAPI_RestartAppIfNecessary", c_cdecl0 }, { "SteamAPI_Shutdown", c_cdecl0 },
    { "SteamAPI_RunCallbacks", c_cdecl0 }, { "SteamAPI_RegisterCallback", c_cdecl0 },
    { "SteamAPI_UnregisterCallback", c_cdecl0 }, { "SteamAPI_RegisterCallResult", c_cdecl0 },
    { "SteamAPI_UnregisterCallResult", c_cdecl0 }, { "SteamFriends", c_cdecl0 }, { "SteamNetworking", c_cdecl0 },
    { "SteamMatchmaking", c_cdecl0 }, { "SteamUser", c_cdecl0 }, { "SteamController", c_cdecl0 },
    { "??0CImmProject@@QAE@XZ", c_ImmProject_ctor }, { "??1CImmProject@@QAE@XZ", c_cdecl0 },
    { "?CreateDevice@CImmDevice@@SAPAV1@PAUHINSTANCE__@@PAUHWND__@@@Z", c_cdecl0 },
    { "?OpenFile@CImmProject@@QAEHPBDPAVCImmDevice@@@Z", c_ret0_2 },
    { "?Start@CImmProject@@QAEHPBDKKPAVCImmDevice@@@Z", c_ret0_4 },
    { "?Stop@CImmProject@@QAEHPBD@Z", c_ret0_1 },
    { "?CreateEffect@CImmProject@@QAEPAVCImmCompoundEffect@@PBDPAVCImmDevice@@K@Z", c_ret0_3 },
    { "wglCreateContext", c_ret0_1 }, { "wglDeleteContext", c_ret0_1 }, { "wglMakeCurrent", c_ret0_2 },
    { "ChoosePixelFormat", c_ret0_2 }, { "SetPixelFormat", c_ret0_3 },
    { "glMatrixMode", c_ret0_1 }, { "glBlendFunc", c_ret0_2 }, { "glOrtho", c_ret0_12 }, { "glLoadIdentity", c_ret0_0 },
    { "glDisable", c_ret0_1 }, { "glEnable", c_ret0_1 }, { "glLineStipple", c_ret0_2 }, { "glLineWidth", c_ret0_1 },
    { "glColor4f", c_ret0_4 }, { "glBegin", c_ret0_1 }, { "glVertex2i", c_ret0_2 }, { "glEnd", c_ret0_0 }, { "glFlush", c_ret0_0 },
    { "glHint", c_ret0_2 },
    { 0, 0 }
};

/* ---- faults, the watchdog, the lift's hooks ------------------------------ */

void recomp_not_lifted(uint32_t va) {
    fprintf(stderr, "\n[not-lifted] sub_%08X  (called from 0x%08X)\n", va, g_cur_func);
    hle_exit(2);
}

static void report(const char *why) {
    fprintf(stderr, "\n=== %s ===\n  in lifted sub_%08X, last import %s\n", why, g_cur_func,
            g_cur_import ? g_cur_import : "(none)");
    fprintf(stderr, "  eax=%08X ecx=%08X edx=%08X ebx=%08X esp=%08X ebp=%08X esi=%08X edi=%08X\n",
            g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi);
    fprintf(stderr, "last indirect calls (newest first):\n");
    for (int i = 1, shown = 0; shown < 24 && i <= ICALL_TRACE_SIZE && i <= (int)g_icall_trace_idx; i++) {
        uint32_t k = (g_icall_trace_idx - i) & (ICALL_TRACE_SIZE - 1);
        const char *nm = hle_name(g_icall_trace[k]);
        if (g_icall_from[k] >= 0x10080000u && g_icall_from[k] < 0x10140000u) continue;   /* sound.dll's thread, always busy */
        shown++;
        fprintf(stderr, "  0x%08X  from 0x%08X  %s\n", g_icall_trace[k], g_icall_from[k], nm ? nm : "");
    }
}
static void on_fault(int sig, siginfo_t *si, void *uc) {
    static volatile int once;
    (void)uc;
    if (__sync_lock_test_and_set(&once, 1)) _exit(3);
    char why[96];
    snprintf(why, sizeof why, "fault: signal %d, address %p", sig, si->si_addr);
    report(why);
    void *frames[32];                                    /* the host's own stack: addr2line -e civ3 turns it into lines */
    fprintf(stderr, "host stack:\n");
    backtrace_symbols_fd(frames, backtrace(frames, 32), 2);
    _exit(3);
}

/* --shot FILE: the screen as a PPM when the watchdog ends the run. */
static char g_shot[PATH_MAX];
static void shot(void) {
    FILE *f = g_shot[0] && g_screen ? fopen(g_shot, "wb") : NULL;
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", g_screen_w, g_screen_h);
    for (size_t i = 0, n = (size_t)g_screen_w * g_screen_h; i < n; i++) {
        uint8_t rgb[3] = { (uint8_t)(g_screen[i] >> 16), (uint8_t)(g_screen[i] >> 8), (uint8_t)g_screen[i] };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    fprintf(stderr, "[shot] %s\n", g_shot);
}

/* --watchdog S from the start; --play S from the main menu (as on Windows). */
static void *watchdog(void *unused) {
    (void)unused;
    if (g_play_s) {
        uint32_t t0 = hle_ticks_ms();
        while (!g_menu_open && !(g_watchdog_s && hle_ticks_ms() - t0 >= g_watchdog_s * 1000u)) usleep(50000);
        if (g_menu_open) g_watchdog_s = g_play_s;
        else fprintf(stderr, "\n[watchdog] no main menu after %u s\n", g_watchdog_s);
    }
    if (!g_play_s || g_menu_open) sleep(g_watchdog_s);
    fprintf(stderr, "\n[watchdog] %u s: in sub_%08X, last import %s, %u indirect calls\n",
            g_watchdog_s, g_cur_func, g_cur_import ? g_cur_import : "(none)", g_icall_count);
    fprintf(stderr, "[game] turn %u\n", MEM32(TURN_VA));
    shot();
    hle_exit(4);
    return NULL;
}

/* The harness's milestones, from game state (input.c on Windows). */
static void *milestones(void *unused) {
    (void)unused;
    while (!MEM8(MENU_FLAG_VA)) usleep(100000);
    g_menu_open = 1;
    fprintf(stderr, "[game] main menu open\n");
    while (MEM8(MENU_FLAG_VA)) usleep(100000);
    fprintf(stderr, "[game] main menu closed: a menu choice was taken\n");
    return NULL;
}

/* ---- --record: the screen to ffmpeg, as on Windows ----------------------- */
static char g_record[PATH_MAX];
static unsigned g_fps = 15;
static long g_frames;
static FILE *g_ffmpeg;
static pthread_mutex_t g_rec_lock = PTHREAD_MUTEX_INITIALIZER;
static void record_close(void) {
    pthread_mutex_lock(&g_rec_lock);
    if (g_ffmpeg) pclose(g_ffmpeg), g_ffmpeg = NULL, fprintf(stderr, "[record] -> %s\n", g_record);
    pthread_mutex_unlock(&g_rec_lock);
}
static void on_exit_close(int code) { (void)code; record_close(); }
static void *recorder(void *unused) {
    (void)unused;
    while (!g_menu_open || !g_screen) usleep(50000);
    int w = g_screen_w, h = g_screen_h;
    char cmd[PATH_MAX + 256];
    snprintf(cmd, sizeof cmd, "ffmpeg -y -loglevel error -f rawvideo -pix_fmt bgr0 -s %dx%d -r %u -i - "
             "-c:v libx264 -pix_fmt yuv420p '%s'", w, h, g_fps, g_record);
    pthread_mutex_lock(&g_rec_lock);
    g_ffmpeg = popen(cmd, "w");
    pthread_mutex_unlock(&g_rec_lock);
    uint32_t *frame = (uint32_t *)malloc((size_t)w * h * 4);
    for (long n = 0; g_ffmpeg; n++) {
        usleep(1000000u / g_fps);
        if (g_screen_w == w && g_screen_h == h) memcpy(frame, g_screen, (size_t)w * h * 4);   /* else the last frame again */
        pthread_mutex_lock(&g_rec_lock);
        if (g_ffmpeg && fwrite(frame, 4, (size_t)w * h, g_ffmpeg) != (size_t)w * h) {
            fprintf(stderr, "[record] ffmpeg is not taking frames: not recording\n");
            pclose(g_ffmpeg), g_ffmpeg = NULL;
        }
        pthread_mutex_unlock(&g_rec_lock);
        if (g_frames && n + 1 == g_frames) { record_close(); hle_exit(0); }
    }
    return NULL;
}

static void start_thread(void *(*fn)(void *)) {
    pthread_t t;
    pthread_create(&t, NULL, fn, NULL);
    pthread_detach(t);
}

int main(int argc, char **argv) {
    const char *exe = "work/Civ3Conquests.exe", *game = "game", *work = "work";
    int run = 0;
    setvbuf(stderr, NULL, _IONBF, 0);
    for (int i = 1; i < argc; i++) {
        int n = script_arg(argc, argv, i);
        if (n) { i += n - 1; continue; }
        if (!strcmp(argv[i], "--run")) run = 1;
        else if (!strcmp(argv[i], "--headless")) g_headless = 1;
        else if (!strcmp(argv[i], "--mute")) hle_dsound_set_master(0.0f);
        else if (!strcmp(argv[i], "--exe") && i + 1 < argc) exe = argv[++i];
        else if (!strcmp(argv[i], "--game") && i + 1 < argc) game = argv[++i];
        else if (!strcmp(argv[i], "--work") && i + 1 < argc) work = argv[++i];
        else if (!strcmp(argv[i], "--watchdog") && i + 1 < argc) g_watchdog_s = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--play") && i + 1 < argc) g_play_s = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--trace")) win32hle_trace = 1;
        else if (!strcmp(argv[i], "--nosteam")) ;           /* the Windows host's: Steam is always absent here */
        else if (!strcmp(argv[i], "--record") && i + 1 < argc) {
            char cwd[PATH_MAX];                        /* absolute: the run chdirs into the game folder */
            const char *p = argv[++i];
            if (p[0] == '/') snprintf(g_record, sizeof g_record, "%s", p);
            else snprintf(g_record, sizeof g_record, "%s/%s", getcwd(cwd, sizeof cwd) ? cwd : ".", p);
        }
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) {
            char cwd[PATH_MAX];
            const char *p = argv[++i];
            if (p[0] == '/') snprintf(g_shot, sizeof g_shot, "%s", p);
            else snprintf(g_shot, sizeof g_shot, "%s/%s", getcwd(cwd, sizeof cwd) ? cwd : ".", p);
        }
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) g_frames = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--fps") && i + 1 < argc) g_fps = (unsigned)strtoul(argv[++i], NULL, 0);
        else {
            printf("usage: civ3 [--run] [--headless] [--mute] [--record out.mp4] [--frames N] [--fps N]\n"
                   "            [--move x,y@s] [--click x,y@s] [--open x,y@s] [--key [c][s][a]+vk@s] [--wait VA@s] [--autopilot MS@s]\n"
                   "            [--exe work/Civ3Conquests.exe] [--game game] [--work work] [--watchdog S] [--play S] [--shot out.ppm] [--trace]\n");
            return !strcmp(argv[i], "--help") || !strcmp(argv[i], "-h") ? 0 : 1;
        }
    }
    char game_full[PATH_MAX], exe_full[PATH_MAX], work_full[PATH_MAX];
    if (!realpath(game, game_full)) { fprintf(stderr, "no game folder at %s\n", game); return 1; }
    {   /* the install's folder, or its Conquests folder (the one with the exe) as the Windows host takes it */
        char exe_in[PATH_MAX + 32];
        snprintf(exe_in, sizeof exe_in, "%s/Civ3Conquests.exe", game_full);
        char *slash = strrchr(game_full, '/');
        if (access(exe_in, F_OK) == 0 && slash && slash != game_full) *slash = 0;
    }
    if (!realpath(exe, exe_full)) { fprintf(stderr, "no %s (README, Step by step)\n", exe); return 1; }
    if (!realpath(work, work_full)) { fprintf(stderr, "no folder %s\n", work); return 1; }
    for (int i = 0; i < NDLL; i++) {
        snprintf(g_dll[i].file, sizeof g_dll[i].file, "%s/%s", work_full, g_dll[i].name);
        if (access(g_dll[i].file, R_OK)) { fprintf(stderr, "no %s: run_lift.py --module (README, Linux)\n", g_dll[i].file); return 1; }
    }
    hle_set_drive('C', game_full);
    for (int i = 0; i < NDLL; i++)
        if (mmap((void *)(uintptr_t)g_dll[i].base, g_dll[i].span, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == MAP_FAILED)
            fprintf(stderr, "cannot hold 0x%08X for %s\n", g_dll[i].base, g_dll[i].name);
        else g_dll[i].held = 1;

    signal(SIGPIPE, SIG_IGN);
    { void *f[1]; backtrace(f, 1); }                     /* loads libgcc now, not in the fault handler */
    {   /* faults report on their own stack: a guest stack overflow is one */
        static uint8_t alt[1 << 16];
        stack_t ss = { .ss_sp = alt, .ss_size = sizeof alt };
        sigaltstack(&ss, NULL);
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_sigaction = on_fault;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGBUS, &sa, NULL);
        sigaction(SIGFPE, &sa, NULL);
        sigaction(SIGILL, &sa, NULL);
    }

    g_hle_LoadLibraryA = hle_fn(win32hle_module, "LoadLibraryA");
    g_hle_GetProcAddress = hle_fn(win32hle_module, "GetProcAddress");
    g_hle_CreateWindowExA = hle_fn(win32hle_user32, "CreateWindowExA");
    win32hle_register(g_civ3_shims);   /* first: the game's own answers win */
    recomp_host_init();
    printf("Civilization III: Conquests recomp host (native Linux)\n  lifted functions in dispatch: %u\n",
           recomp_dispatch_count);

    pe_image img;
    if (recomp_pe_map(exe_full, &img) != 0 || img.base != CIV3_IMAGE_BASE) {
        fprintf(stderr, "cannot map %s at 0x%08X\n", exe_full, CIV3_IMAGE_BASE);
        return 1;
    }
    hle_module_add(CIV3_GUEST_EXE, img.base, img.resource_rva, img.resource_size, 1);
    int unresolved = recomp_pe_bind(&img, hle_resolve_or_stub);
    printf("  mapped %s: 0x%08X-0x%08X, %d imports not implemented yet\n", exe, img.base, img.base + img.span, unresolved);
    hle_set_command_line("\"" CIV3_GUEST_EXE "\"");
    if (!run) {
        printf("\n(dry run: image mapped and bound; --run enters 0x%08X)\n", img.entry);
        return 0;
    }
    char conquests[PATH_MAX + 16];
    snprintf(conquests, sizeof conquests, "%s/Conquests", game_full);
    if (chdir(conquests) != 0) { fprintf(stderr, "cannot enter %s\n", conquests); return 1; }

    screen_mode(1024, 768);
    hle_screen_cursor_hook = hle_cursor_shown;          /* the system cursors, unless the game hides them */
    if (hle_screen_open("Civilization III: Conquests", 0, g_headless) != 0) return 1;
    hle_on_exit = on_exit_close;
    start_thread(milestones);
    if (g_watchdog_s || g_play_s) start_thread(watchdog);
    if (g_record[0]) start_thread(recorder);
    script_start();
    printf("  entering 0x%08X\n\n", img.entry);
    fflush(stdout);
    uint32_t rc = hle_call_guest(img.entry, 0, NULL);
    printf("\nentry returned eax=%08X\n", rc);
    hle_exit((int)rc);
    return 0;
}
