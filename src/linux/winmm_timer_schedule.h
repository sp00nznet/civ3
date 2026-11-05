/* Deadline selection for Windows multimedia periodic timers.
 *
 * Previous POSIX timing could invoke several late callbacks in one
 * millisecond after the guest machine was busy; Civ3's sound.dll mixes
 * audio during each callback.
 *
 * Return the next future deadline, skipping missed intervals. Keep the
 * existing cadence when there was no miss. Monotonic uint32 tick wrap uses
 * signed differences (valid when intervals stay below 2^31 ms).
 */
#ifndef CIV3_WINMM_TIMER_SCHEDULE_H
#define CIV3_WINMM_TIMER_SCHEDULE_H
#include <stdint.h>

static inline uint32_t civ3_winmm_next_due(uint32_t previous_due,
                                           uint32_t now,
                                           uint32_t period_ms) {
    if (!period_ms) period_ms = 1;
    uint32_t next = previous_due + period_ms;
    /* If the next deadline has already passed, schedule one full period
     * from the present instead of firing the missing ticks immediately. */
    if ((int32_t)(now - next) >= 0)
        next = now + period_ms;
    return next;
}

#endif
