/*
 * Scripted input for headless runs: --move x,y@s  --click x,y@s  --key [cs+]vk@s --open x,y@s
 *
 * x,y are game client pixels (1024x768 at the default resolution); s is
 * seconds after the main menu opened, so a script does not depend on how long
 * the load took. The game reads the mouse from window
 * messages that jgl.dll's window procedure forwards, plus jgl's own
 * GetCursorPos, so a script is posted messages to the hidden window and a
 * GetCursorPos (patched into jgl, record.c) that answers the scripted spot.
 * Nothing touches the real cursor or keyboard.
 *
 * The main menu takes two clicks: the first on an item only selects it, the
 * second activates it (sub_00559CE0; docs/bringup.md, 4).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "record.h"

typedef struct { char kind; int x, y; double t; char path[MAX_PATH]; } ev_t;
#define MAX_EV 256
static ev_t g_ev[MAX_EV];
static int g_nev;
static volatile LONG g_cx = -1, g_cy = -1;

int g_input_peek;
int input_arg(int argc, char** argv, int i) {
    if (!strcmp(argv[i], "--peek")) { g_input_peek = 1; return 1; }
    if (i + 1 >= argc || g_nev >= MAX_EV) return 0;
    ev_t e = { 0 };
    const char* a = argv[i + 1];
    if (!strcmp(argv[i], "--move") || !strcmp(argv[i], "--click") || !strcmp(argv[i], "--open")) {
        if (sscanf(a, "%d,%d@%lf", &e.x, &e.y, &e.t) != 3) return 0;
        e.kind = argv[i][2];                    /* 'm', 'c' or 'o' */
    } else if (!strcmp(argv[i], "--autopilot")) {
        /* --autopilot MS@s: from s on, Space then Enter every MS ms. Space
         * skips the selected unit, Enter ends the turn or takes a popup's
         * default, so turns keep rolling and the AI plays its own. */
        if (sscanf(a, "%d@%lf", &e.x, &e.t) != 2) return 0;
        e.kind = 'a';
    } else if (!strcmp(argv[i], "--dump")) {
        /* --dump s:FILE: the guest's writable memory, .data and heaps (below),
         * raw, at s. Two dumps a turn apart find a counter. */
        char* c = strchr(a, ':');
        if (!c || sscanf(a, "%lf", &e.t) != 1) return 0;
        GetFullPathNameA(c + 1, MAX_PATH, e.path, NULL);       /* the run chdirs */
        e.kind = 'd';
    } else if (!strcmp(argv[i], "--wait")) {
        /* --wait VA@s: from s on, hold the script until the dword at VA is
         * nonzero (up to 5 minutes); everything after it moves by the wait.
         * 0xA74EA4 is the turn number, nonzero once a save has loaded
         * (docs/testing.md). */
        if (sscanf(a, "%i@%lf", &e.x, &e.t) != 2) return 0;
        e.kind = 'w';
    } else if (!strcmp(argv[i], "--key")) {
        /* --key [MODS+]vk@s: MODS is any of c (Ctrl), s (Shift), a (Alt),
         * held for the key: c+0x53@s is Ctrl-S. */
        const char* plus = strchr(a, '+');
        if (plus) {
            for (const char* m = a; m < plus; m++)
                e.y |= *m == 'c' ? 1 : *m == 's' ? 2 : *m == 'a' ? 4 : 0;
            a = plus + 1;
        }
        if (sscanf(a, "%i@%lf", &e.x, &e.t) != 2) return 0;
        e.kind = 'k';
    } else return 0;
    g_ev[g_nev++] = e;
    return 2;
}

BOOL WINAPI input_GetCursorPos(POINT* p) {
    if (g_cx < 0 || !g_game_hwnd) return GetCursorPos(p);
    p->x = g_cx; p->y = g_cy;
    ClientToScreen(g_game_hwnd, p);
    return TRUE;
}

/* The game reads Ctrl and Shift with GetKeyState/GetAsyncKeyState (the exe's
 * imports; host.c and oracle.c route them here). A scripted run answers from
 * the script, so the console's real keyboard never leaks into a test. */
static volatile LONG g_mods, g_lb_reads;
SHORT input_key_state(int vk) {
    if (!g_nev) return GetKeyState(vk);
    if (vk == VK_LBUTTON) InterlockedIncrement(&g_lb_reads);
    int bit = vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ? 1
            : vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ? 2
            : vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ? 4
            : vk == VK_LBUTTON ? 8 : 0;    /* click() */
    if (!bit) return GetKeyState(vk);
    return (g_mods & bit) ? (SHORT)0x8000 : 0;
}

/* --peek: the UI globals after each scripted event (bring-up, docs/bringup.md 4). */
extern int g_input_peek;
#define W32(a) (*(volatile uint32_t*)(uintptr_t)(a))
static void tree(uint32_t w, int depth) {
    if (!w || depth > 6 || IsBadReadPtr((void*)(uintptr_t)w, 0x230)) return;
    fprintf(stderr, "[peek] %*s%08X vt %08X style %08X flags %08X r1 %d,%d,%d,%d r2 %d,%d,%d,%d kids %d owner %08X\n",
            depth * 2, "", w, W32(w), W32(w + 0x9C), W32(w + 0xA0),
            W32(w + 0x1AC), W32(w + 0x1B0), W32(w + 0x1B4), W32(w + 0x1B8),
            W32(w + 0x1BC), W32(w + 0x1C0), W32(w + 0x1C4), W32(w + 0x1C8), (int)W32(w + 0x22C), W32(w + 0xB0));
    uint32_t n = W32(w + 0x22C), kids = W32(w + 0x224);
    for (uint32_t i = 0; i < n && i < 64 && kids; i++) tree(W32(kids + 4 * i), depth + 1);
}

/* Function-entry histograms for two windows (idle, after the click), via the
 * trace build's per-entry hook. What only the click window ran is the click. */
extern void (*recomp_trace_extra)(uint32_t va);
#define HN 65536
static uint32_t g_hva[2][HN], g_hcnt[2][HN];
static volatile int g_hphase = -1;
static void hist_hook(uint32_t va) {
    int p = g_hphase;
    if (p < 0) return;
    uint32_t h = (va * 2654435761u) >> 16;
    while (g_hva[p][h] && g_hva[p][h] != va) h = (h + 1) & (HN - 1);
    g_hva[p][h] = va;
    g_hcnt[p][h]++;
}
static uint32_t hist_get(int p, uint32_t va) {
    uint32_t h = (va * 2654435761u) >> 16;
    while (g_hva[p][h] && g_hva[p][h] != va) h = (h + 1) & (HN - 1);
    return g_hva[p][h] == va ? g_hcnt[p][h] : 0;
}
static void hist_report(void) {
    for (int i = 0; i < HN; i++)
        if (g_hva[1][i] && !hist_get(0, g_hva[1][i]))
            fprintf(stderr, "[hist] click-only sub_%08X x%u\n", g_hva[1][i], g_hcnt[1][i]);
}

static void regions(const char* what, uint32_t list) {
    uint32_t a = W32(list + 0x50), n = W32(list + 0x58);
    fprintf(stderr, "[peek] %s %08X: %u regions\n", what, list, n);
    for (uint32_t i = 0; i < n && i < 40 && a; i++) {
        uint32_t e = a + 32 * i;
        fprintf(stderr, "[peek]   %2u: %d,%d,%d,%d id %08X val %08X\n", i, W32(e + 4), W32(e + 8),
                W32(e + 12), W32(e + 16), W32(e + 0x14), W32(e + 0x18));
    }
}

static void peek(const char* when) {
    static const uint32_t va[] = { 0xCCFBE4, 0xCCFC38, 0xCCFC44, 0xCCFC4C, 0xCCFC60, 0xCCFC68,
                                   0xCCFC78, 0xCCFC7C, 0xCCFC88, 0xCCFC8C, 0xCCFC98, 0xCCFC9C, 0xCCFCE4 };
    fprintf(stderr, "[peek] %s:", when);
    for (int i = 0; i < (int)(sizeof va / sizeof va[0]); i++) {
        uint32_t v = *(volatile uint32_t*)(uintptr_t)va[i];
        fprintf(stderr, " %X=%08X", va[i], v);
        if (v >= 0x00400000 && v < 0x10000000 && !IsBadReadPtr((void*)(uintptr_t)v, 4))
            fprintf(stderr, "(vt %08X)", *(uint32_t*)(uintptr_t)v);
    }
    fprintf(stderr, "\n");
    for (uint32_t i = 0; i < W32(0xCCFCDC) && i < 32; i++) tree(W32(0xCCF420 + 4 * i), 0);
    { uint32_t r = W32(0xCCF420);
      fprintf(stderr, "[peek] root 4d74=%08X 4d78=%02X 4e21=%02X 4ecc=%08X 4ed0=%02X 749268=%02X b64e1e=%02X 9ea05c=%08X\n",
              W32(r + 0x4D74), *(volatile uint8_t*)(uintptr_t)(r + 0x4D78), *(volatile uint8_t*)(uintptr_t)(r + 0x4E21),
              W32(r + 0x4ECC), *(volatile uint8_t*)(uintptr_t)(r + 0x4ED0), *(volatile uint8_t*)(uintptr_t)0x749268,
              *(volatile uint8_t*)(uintptr_t)0xB64E1E, W32(0x9EA05C)); }
    regions("root hover", W32(0xCCF420) + 0xBC);
    regions("root click", W32(0xCCF420) + 0x328);
}

static int cmp_ev(const void* a, const void* b) {
    double d = ((const ev_t*)a)->t - ((const ev_t*)b)->t;
    return d < 0 ? -1 : d > 0;
}

/* Milestones for tools/conformance.py, read from game state rather than
 * pixels: [0xB64E1E] is set while the main menu's modal loop runs (sub_0055A76C
 * sets and clears it around sub_0055A370), so 1 -> 0 means a menu choice was
 * taken. Polled from a host thread; guest memory is mapped 1:1. */
volatile LONG g_menu_open;

static DWORD WINAPI milestones(LPVOID unused) {
    (void)unused;
    volatile uint8_t* menu = (volatile uint8_t*)(uintptr_t)0xB64E1E;
    while (!*menu) Sleep(100);
    InterlockedExchange(&g_menu_open, 1);
    fprintf(stderr, "[game] main menu open\n");
    while (*menu) Sleep(100);
    fprintf(stderr, "[game] main menu closed: a menu choice was taken\n");
    return 0;
}

/* Wait until the game has read the left button twice more, up to 1 s. */
static void lb_seen(void) {
    LONG r0 = g_lb_reads;
    for (int i = 0; i < 40 && g_lb_reads - r0 < 2; i++) Sleep(25);
}

/* A click is the button messages and, for the screens that poll the button
 * instead (the main menu after a screen closes), the button held until the
 * game has seen it down and then up. A fixed 250 ms hold was missed when six
 * runs at once stretched a frame past it. */
static void click(HWND h, LPARAM lp) {
    InterlockedOr(&g_mods, 8);
    PostMessageA(h, WM_LBUTTONDOWN, MK_LBUTTON, lp);
    Sleep(250);
    lb_seen();
    PostMessageA(h, WM_LBUTTONUP, 0, lp);
    InterlockedAnd(&g_mods, ~8);
    lb_seen();
}

static void key(HWND h, int vk) {
    PostMessageA(h, WM_KEYDOWN, vk, 1);
    Sleep(60);
    PostMessageA(h, WM_KEYUP, vk, 0xC0000001);
}

static DWORD WINAPI autopilot(LPVOID period) {
    for (;;) {
        key(g_game_hwnd, VK_SPACE);
        Sleep((DWORD)(intptr_t)period / 2);
        key(g_game_hwnd, VK_RETURN);
        Sleep((DWORD)(intptr_t)period / 2);
    }
}

static DWORD WINAPI script(LPVOID unused) {
    (void)unused;
    while (!g_menu_open) Sleep(50);
    if (g_input_peek) { recomp_trace_extra = hist_hook; Sleep(3000); g_hphase = 0; Sleep(3000); g_hphase = -1; }
    DWORD t0 = GetTickCount();
    LONG shift = 0;                     /* ms the script runs late: retries of --open */
    for (int i = 0; i < g_nev; i++) {
        ev_t* e = &g_ev[i];
        LONG wait = (LONG)(e->t * 1000) + shift - (LONG)(GetTickCount() - t0);
        if (wait > 0) Sleep(wait);
        HWND h = g_game_hwnd;
        if (e->kind == 'd') {
            /* Every committed writable region below 0x60000000 (the host is
             * linked above): the guest's globals and its heaps. Any writable
             * protection, not just PAGE_READWRITE, which left .data/.bss out.
             * Each region as uint32 address, uint32 size, bytes. */
            FILE* f = fopen(e->path, "wb");
            MEMORY_BASIC_INFORMATION m;
            for (uintptr_t p = 0x10000; f && p < 0x60000000u && VirtualQuery((void*)p, &m, sizeof m);
                 p = (uintptr_t)m.BaseAddress + m.RegionSize) {
                if (m.State == MEM_COMMIT && (m.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) && !(m.Protect & PAGE_GUARD) && m.Type != MEM_MAPPED) {
                    uint32_t hdr[2] = { (uint32_t)(uintptr_t)m.BaseAddress, (uint32_t)m.RegionSize };
                    fwrite(hdr, 4, 2, f);
                    fwrite(m.BaseAddress, 1, m.RegionSize, f);
                }
            }
            if (f) fclose(f);
            fprintf(stderr, "[input] %.1fs dump -> %s\n", e->t, e->path);
            continue;
        }
        if (e->kind == 'w') {
            DWORD w0 = GetTickCount();
            while (!W32(e->x) && GetTickCount() - w0 < 300000) Sleep(100);
            shift += (LONG)(GetTickCount() - w0);
            fprintf(stderr, "[input] %.1fs waited %.1fs for [0x%X] = %u\n", e->t,
                    (GetTickCount() - w0) / 1000.0, e->x, W32(e->x));
            continue;
        }
        if (e->kind == 'a') {
            fprintf(stderr, "[input] %.1fs autopilot every %d ms\n", e->t, e->x);
            CloseHandle(CreateThread(NULL, 0, autopilot, (LPVOID)(intptr_t)e->x, 0, NULL));
            continue;
        }
        if (e->kind == 'k') {
            static const int mvk[] = { VK_CONTROL, VK_SHIFT, VK_MENU };
            fprintf(stderr, "[input] %.1fs key %s%s%s0x%X\n", e->t, e->y & 1 ? "Ctrl-" : "",
                    e->y & 2 ? "Shift-" : "", e->y & 4 ? "Alt-" : "", e->x);
            for (int m = 0; m < 3; m++)
                if (e->y & 1 << m) PostMessageA(h, WM_KEYDOWN, mvk[m], 1);
            InterlockedExchange(&g_mods, e->y);
            PostMessageA(h, WM_KEYDOWN, e->x, 1);
            Sleep(150);
            PostMessageA(h, WM_KEYUP, e->x, 0xC0000001);
            Sleep(500);         /* ponytail: the handler reads the modifiers when it runs; a busy frame longer than this would miss them */
            InterlockedExchange(&g_mods, 0);
            for (int m = 0; m < 3; m++)
                if (e->y & 1 << m) PostMessageA(h, WM_KEYUP, mvk[m], 0xC0000001);
            continue;
        }
        LPARAM lp = MAKELPARAM(e->x, e->y);
        fprintf(stderr, "[input] %.1fs %s %d,%d\n", e->t, e->kind == 'c' ? "click" : e->kind == 'o' ? "open" : "move", e->x, e->y);
        InterlockedExchange(&g_cx, e->x);
        InterlockedExchange(&g_cy, e->y);
        PostMessageA(h, WM_MOUSEMOVE, 0, lp);
        if (e->kind == 'o') {
            /* Click until the screen changes: whether the main menu needs one
             * click or two depends on whether it took the hover first (a
             * race; docs/testing.md), and a click too many lands on the next
             * screen. 5% of the pixels changed is a new screen or dialog (Load
             * Game is 17%); a highlight is under 1%. Each retry delays the
             * rest of the script by 2 s. */
            record_changed(1);
            int tries = 0, c = 0;
            while (tries < 5) {
                Sleep(100);
                click(h, lp);
                tries++;
                Sleep(2000);
                if ((c = record_changed(0)) > 50 || (c < 0 && tries == 2)) break;
            }
            shift += (tries - 1) * 2000;
            fprintf(stderr, "[input] %.1fs opened after %d click(s), %d%% changed\n", e->t, tries, c / 10);
            continue;
        }
        if (e->kind == 'c') {
            if (g_input_peek) g_hphase = 1;
            Sleep(100);
            click(h, lp);
        }
        if (g_input_peek && e->kind == 'c') { Sleep(2000); g_hphase = -1; hist_report(); }
        if (g_input_peek) { Sleep(1500); peek(e->kind == 'c' ? "after click" : "after move"); }
    }
    return 0;
}

void input_start(void) {
    CloseHandle(CreateThread(NULL, 0, milestones, NULL, 0, NULL));
    if (!g_nev) return;
    qsort(g_ev, g_nev, sizeof *g_ev, cmp_ev);
    CloseHandle(CreateThread(NULL, 0, script, NULL, 0, NULL));
}
