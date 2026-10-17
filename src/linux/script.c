/*
 * Scripted input for the Linux host: the Windows host's grammar
 * (src/runtime/input.c, docs/testing.md), so the same cases drive both.
 *
 *   --move x,y@s  --click x,y@s  --open x,y@s  --key [c][s][a]+vk@s
 *   --wait VA@s   --autopilot MS@s
 *
 * s is seconds after the main menu opened. Input is posted as messages to
 * jgl's window, and the cursor and modifier state GetCursorPos and
 * GetKeyState answer is the script's. Every touch of the window manager is
 * made holding the machine (mach_enter), as the game's own threads do.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "win32hle.h"

#define WM_KEYDOWN     0x0100u
#define WM_KEYUP       0x0101u
#define WM_MOUSEMOVE   0x0200u
#define WM_LBUTTONDOWN 0x0201u
#define WM_LBUTTONUP   0x0202u
#define VK_LBUTTON 0x01u
#define VK_RETURN  0x0Du
#define VK_SHIFT   0x10u
#define VK_CONTROL 0x11u
#define VK_MENU    0x12u
#define VK_SPACE   0x20u

extern volatile int g_menu_open;          /* main.c */
int screen_changed(int snapshot);

typedef struct { char kind; int x, y; double t; } ev_t;
#define MAX_EV 256
static ev_t g_ev[MAX_EV];
static int g_nev;
static volatile uint32_t g_hwnd;

void script_set_window(uint32_t hwnd) { g_hwnd = hwnd; }

int script_arg(int argc, char **argv, int i) {
    if (i + 1 >= argc || g_nev >= MAX_EV) return 0;
    ev_t e = { 0 };
    const char *a = argv[i + 1];
    if (!strcmp(argv[i], "--move") || !strcmp(argv[i], "--click") || !strcmp(argv[i], "--open")) {
        if (sscanf(a, "%d,%d@%lf", &e.x, &e.y, &e.t) != 3) return 0;
        e.kind = argv[i][2];                             /* 'm', 'c' or 'o' */
    } else if (!strcmp(argv[i], "--autopilot")) {
        if (sscanf(a, "%d@%lf", &e.x, &e.t) != 2) return 0;
        e.kind = 'a';
    } else if (!strcmp(argv[i], "--wait")) {
        if (sscanf(a, "%i@%lf", &e.x, &e.t) != 2) return 0;
        e.kind = 'w';
    } else if (!strcmp(argv[i], "--key")) {
        const char *plus = strchr(a, '+');
        if (plus) {
            for (const char *m = a; m < plus; m++) e.y |= *m == 'c' ? 1 : *m == 's' ? 2 : *m == 'a' ? 4 : 0;
            a = plus + 1;
        }
        if (sscanf(a, "%i@%lf", &e.x, &e.t) != 2) return 0;
        e.kind = 'k';
    } else return 0;
    g_ev[g_nev++] = e;
    return 2;
}

static void msleep(uint32_t ms) { usleep(ms * 1000u); }
static void post(uint32_t m, uint32_t w, uint32_t l) { mach_enter(); hle_post_message(g_hwnd, m, w, l); mach_leave(); }
static void key_state(uint32_t vk, int down) { mach_enter(); hle_input_key_state(vk, down); mach_leave(); }

/* Until the game has read the left button twice more, up to 1 s. */
static void lb_seen(void) {
    int r0 = hle_lbutton_reads;
    for (int i = 0; i < 40 && hle_lbutton_reads - r0 < 2; i++) msleep(25);
}
/* The button messages, and the button held until the game has seen it down
 * and then up: some screens poll it instead (input.c). */
static void click(uint32_t lp) {
    key_state(VK_LBUTTON, 1);
    post(WM_LBUTTONDOWN, 1, lp);
    msleep(250);
    lb_seen();
    post(WM_LBUTTONUP, 0, lp);
    key_state(VK_LBUTTON, 0);
    lb_seen();
}
static void key(uint32_t vk) {
    post(WM_KEYDOWN, vk, 1);
    msleep(60);
    post(WM_KEYUP, vk, 0xC0000001u);
}

static void *autopilot(void *period) {
    for (;;) {
        key(VK_SPACE);
        msleep((uint32_t)(uintptr_t)period / 2);
        key(VK_RETURN);
        msleep((uint32_t)(uintptr_t)period / 2);
    }
    return NULL;
}

static int cmp_ev(const void *a, const void *b) {
    double d = ((const ev_t *)a)->t - ((const ev_t *)b)->t;
    return d < 0 ? -1 : d > 0;
}

static void *script(void *unused) {
    (void)unused;
    while (!g_menu_open) msleep(50);
    uint32_t t0 = hle_ticks_ms();
    int32_t shift = 0;
    for (int i = 0; i < g_nev; i++) {
        ev_t *e = &g_ev[i];
        int32_t wait = (int32_t)(e->t * 1000) + shift - (int32_t)(hle_ticks_ms() - t0);
        if (wait > 0) msleep((uint32_t)wait);
        if (e->kind == 'w') {
            uint32_t w0 = hle_ticks_ms();
            while (!MEM32(e->x) && hle_ticks_ms() - w0 < 300000) msleep(100);
            shift += (int32_t)(hle_ticks_ms() - w0);
            fprintf(stderr, "[input] %.1fs waited %.1fs for [0x%X] = %u\n", e->t, (hle_ticks_ms() - w0) / 1000.0, e->x, MEM32(e->x));
            continue;
        }
        if (e->kind == 'a') {
            fprintf(stderr, "[input] %.1fs autopilot every %d ms\n", e->t, e->x);
            pthread_t t;
            pthread_create(&t, NULL, autopilot, (void *)(uintptr_t)e->x);
            pthread_detach(t);
            continue;
        }
        if (e->kind == 'k') {
            static const uint32_t mvk[] = { VK_CONTROL, VK_SHIFT, VK_MENU };
            fprintf(stderr, "[input] %.1fs key %s%s%s0x%X\n", e->t, e->y & 1 ? "Ctrl-" : "", e->y & 2 ? "Shift-" : "",
                    e->y & 4 ? "Alt-" : "", e->x);
            for (int m = 0; m < 3; m++) if (e->y & 1 << m) key_state(mvk[m], 1), post(WM_KEYDOWN, mvk[m], 1);
            post(WM_KEYDOWN, (uint32_t)e->x, 1);
            msleep(150);
            post(WM_KEYUP, (uint32_t)e->x, 0xC0000001u);
            msleep(500);       /* ponytail: the handler reads the modifiers when it runs; a busy frame longer than this would miss them */
            for (int m = 0; m < 3; m++) if (e->y & 1 << m) key_state(mvk[m], 0), post(WM_KEYUP, mvk[m], 0xC0000001u);
            continue;
        }
        uint32_t lp = (uint32_t)(e->y & 0xFFFF) << 16 | (uint32_t)(e->x & 0xFFFF);
        fprintf(stderr, "[input] %.1fs %s %d,%d\n", e->t, e->kind == 'c' ? "click" : e->kind == 'o' ? "open" : "move", e->x, e->y);
        mach_enter();
        hle_input_cursor(e->x, e->y);                    /* jgl's window is at 0,0: client is screen */
        mach_leave();
        post(WM_MOUSEMOVE, 0, lp);
        if (e->kind == 'o') {                            /* click until the screen changes (input.c) */
            screen_changed(1);
            int tries = 0, c = 0;
            while (tries < 5) {
                msleep(100);
                click(lp);
                tries++;
                msleep(2000);
                if ((c = screen_changed(0)) > 50 || (c < 0 && tries == 2)) break;
            }
            shift += (tries - 1) * 2000;
            fprintf(stderr, "[input] %.1fs opened after %d click(s), %d%% changed\n", e->t, tries, c / 10);
        } else if (e->kind == 'c') {
            msleep(100);
            click(lp);
        }
    }
    return NULL;
}

void script_start(void) {
    if (!g_nev) return;
    qsort(g_ev, (size_t)g_nev, sizeof *g_ev, cmp_ev);
    pthread_t t;
    pthread_create(&t, NULL, script, NULL);
    pthread_detach(t);
}
