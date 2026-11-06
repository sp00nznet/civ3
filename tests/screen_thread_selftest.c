/* Synthetic window: background guest requests must wait for the owner pump. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <SDL2/SDL.h>
#include "win32hle.h"
#include "host.h"
const recomp_dispatch_entry_t recomp_dispatch_table[] = {{0, NULL}};
const uint32_t recomp_dispatch_count = 0;
static void *worker(void *unused) {
    (void)unused;
    for (int i = 0; i < 100; ++i) {
        hle_screen_title("queued worker title");
        hle_screen_pump();
        if (hle_gdi_window_hook) hle_gdi_window_hook();
        if (hle_cursor_hook) hle_cursor_hook(i, i);
    }
    return NULL;
}
int main(void) {
    recomp_host_init();
    if (hle_screen_open("owner title", 0, 0)) return 2;
    /* SDL2-compat can allocate internal windows before the game's window. */
    SDL_Window *window = NULL;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_WINDOWEVENT) {
            SDL_Window *candidate = SDL_GetWindowFromID(event.window.windowID);
            if (candidate && !strcmp(SDL_GetWindowTitle(candidate), "owner title")) window = candidate;
        }
    }
    if (!window) { fprintf(stderr, "Test window not found\n"); return 2; }
    pthread_t thread;
    if (pthread_create(&thread, NULL, worker, NULL)) return 2;
    pthread_join(thread, NULL);
    int unchanged = !strcmp(SDL_GetWindowTitle(window), "owner title");
    hle_screen_pump();
    int applied = !strcmp(SDL_GetWindowTitle(window), "queued worker title");
    SDL_Quit();
    if (!unchanged || !applied) {
        fprintf(stderr, "FAIL: title unchanged before owner pump=%d; applied afterward=%d\n", unchanged, applied);
        return 1;
    }
    puts("PASS: worker pump/draw/cursor/title requests stay off the SDL owner thread; title is applied by owner");
    return 0;
}
