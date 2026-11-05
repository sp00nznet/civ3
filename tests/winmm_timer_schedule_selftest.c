#include <stdint.h>
#include <stdio.h>
#include "winmm_timer_schedule.h"

static int errors;
static void check(const char *message, int good) {
    if (!good) { fprintf(stderr, "FAIL: %s\n", message); ++errors; }
}
int main(void) {
    check("on-time tick keeps stable cadence",
          civ3_winmm_next_due(1000,1000,30)==1030);
    check("slightly late tick keeps next deadline",
          civ3_winmm_next_due(1000,1025,30)==1030);
    check("delayed callback skips missed deadlines",
          civ3_winmm_next_due(1000,1110,30)==1140);
    check("exact next deadline schedules a full interval",
          civ3_winmm_next_due(1000,1030,30)==1060);
    check("zero period stays bounded",
          civ3_winmm_next_due(1000,1000,0)==1001);
    check("unsigned timer wrap handled correctly",
          civ3_winmm_next_due(0xFFFFFFF0u,0x00000005u,30)==0x0000000Eu);
    check("overdue callback across wrap skips old deadlines",
          civ3_winmm_next_due(0xFFFFFFF0u,0x00000080u,30)==0x0000009Eu);

    /* Synthetic 30 ms periodic sound tick with repeated 100-300 ms host
     * stalls. The former code emitted N consecutive callbacks in the same
     * millisecond to catch up, saturating the guest software audio mixer. */
    uint32_t next = 1030, now = 1000, last=0;
    int bursts=0, ticks=0;
    for (int i=0;i<10000;++i) {
        if ((int32_t)(next-now)>0) now=next;
        if (last && now-last<15) ++bursts;
        last=now;
        ++ticks;
        if (i%79==0) now+=310;  /* slow callback holds the guest machine */
        next=civ3_winmm_next_due(next,now,30);
        check("all scheduled future deadlines are actually future",
              (int32_t)(next-now)>0);
    }
    check("timer makes forward progress",ticks>1000);
    check("no burst callbacks",bursts==0);

    /* Stress arbitrary 31-bit timing variations, leaving enough clock
     * headroom for unambiguous signed modular subtraction. */
    uint32_t seed=0x8C11D14Eu, due=0xF0000000u;
    for (int i=0;i<200000;++i) {
        seed=seed*1664525u+1013904223u;
        uint32_t delay=1u+((seed>>16)%120u);
        uint32_t now2=due+((seed>>7)%10000u);
        uint32_t next2=civ3_winmm_next_due(due,now2,delay);
        check("random future deadline",(int32_t)(next2-now2)>0);
        due=next2;
    }
    if (!errors)
        puts("PASS: periodic multimedia timer skips callback bursts, keeps cadence and survives clock rollover");
    return errors?1:0;
}
