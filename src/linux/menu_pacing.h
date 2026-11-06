/* Host-only pacing after an empty, unfiltered main-menu poll. Never delay
 * gameplay, worker threads, filtered polls, queued input or PM_NOYIELD. */
#pragma once
#include <stdint.h>
static inline unsigned civ3_menu_pause_ms(unsigned configured, int on_ui_thread,
        int menu_active, uint32_t result, uint32_t low, uint32_t high, uint32_t flags) {
    return configured <= 16 && on_ui_thread && menu_active && !result &&
           !low && !high && !(flags & 2u) ? configured : 0;
}
