/* Synthetic register patterns only. Exercise the same context save/load paths
 * used by guest threads, callbacks and nonblocking host snapshots. */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "win32hle.h"
#include "host.h"
#include "machine_snapshot.h"

static atomic_int failures, stage;
static atomic_uint checks;
static void check(const char *label, int value) {
    atomic_fetch_add(&checks, 1);
    if (!value && atomic_fetch_add(&failures, 1) < 12)
        fprintf(stderr, "FAIL: %s\n", label);
}
static void wait_stage(int expected) {
    while (atomic_load(&stage) < expected) {
        struct timespec pause = {0, 100000}; nanosleep(&pause, NULL);
    }
}
static void pattern(unsigned seed) {
    g_eax=0x11110000u+seed; g_ecx=0x22220000u+seed; g_edx=0x33330000u+seed;
    g_ebx=0x44440000u+seed; g_esi=0x55550000u+seed; g_edi=0x66660000u+seed; g_ebp=0x77770000u+seed;
    g_cur_func=0x00402000u+seed;
    g_flag_k=seed%16u; g_flag_a=seed*13u; g_flag_b=seed*17u; g_flag_cf=seed&1u;
    g_gs_base=0x60000000u+seed*0x1000u;
    g_seg_cs=(uint16_t)(seed+1); g_seg_ds=(uint16_t)(seed+2); g_seg_es=(uint16_t)(seed+3);
    g_seg_fs=(uint16_t)(seed+4); g_seg_gs=(uint16_t)(seed+5); g_seg_ss=(uint16_t)(seed+6);
    g_fp_top=(int)(seed%8u); g_fpu_cw=(uint16_t)(0x027Fu|((seed&3u)<<10));
    g_mxcsr=0x1F80u|((seed&3u)<<13);
    for(unsigned i=0;i<8;i++) {
        g_st[i]=(double)seed+(double)i/8.0;
        g_mm[i]=((uint64_t)seed<<32)|(i*31337u);
        for(unsigned j=0;j<16;j++) g_xmm[i].u8[j]=(uint8_t)(seed*31u+i*7u+j);
    }
}
static void preserved(unsigned seed) {
    check("general register context",g_eax==0x11110000u+seed&&g_ecx==0x22220000u+seed&&g_edx==0x33330000u+seed&&
          g_ebx==0x44440000u+seed&&g_esi==0x55550000u+seed&&g_edi==0x66660000u+seed&&g_ebp==0x77770000u+seed);
    check("guest program location",g_cur_func==0x00402000u+seed);
    check("returned arithmetic flags",g_flag_k==seed%16u&&g_flag_a==seed*13u&&g_flag_b==seed*17u&&g_flag_cf==(seed&1u));
    check("GS base",g_gs_base==0x60000000u+seed*0x1000u);
    check("segment selectors",g_seg_cs==seed+1&&g_seg_ds==seed+2&&g_seg_es==seed+3&&g_seg_fs==seed+4&&g_seg_gs==seed+5&&g_seg_ss==seed+6);
    check("x87 control and stack position",g_fp_top==(int)(seed%8u)&&g_fpu_cw==(uint16_t)(0x027Fu|((seed&3u)<<10)));
    check("SSE control word",g_mxcsr==(0x1F80u|((seed&3u)<<13)));
    int expected=(seed&3u)==0u||(seed&3u)==2u?2:1;
    check("SSE conversion follows this thread's rounding",sse_cvt_i32(1.5,0)==expected);
    int st=1,mm=1,xmm=1;
    for(unsigned i=0;i<8;i++) {
        st &= g_st[i]==(double)seed+(double)i/8.0;
        mm &= g_mm[i]==(((uint64_t)seed<<32)|(i*31337u));
        for(unsigned j=0;j<16;j++) xmm &= g_xmm[i].u8[j]==(uint8_t)(seed*31u+i*7u+j);
    }
    check("x87 values",st); check("MMX values",mm); check("all 128 XMM bytes",xmm);
}
static void nested_callback(void) { pattern(19); RET(0xAABBCCDDu,0); }
static void outer_callback(void) {
    uint32_t arg=A32(0), sp=g_esp, fs=g_fs_base;
    check("callback argument",arg==0x55AA55AAu);
    pattern(11);
    check("nested callback return",hle_call_guest(0x00401100u,0,NULL)==0xAABBCCDDu);
    preserved(11);
    check("callback stack/TIB",g_esp==sp&&g_fs_base==fs);
    RET(0x1234ABCDu,1);
}
const recomp_dispatch_entry_t recomp_dispatch_table[]={{0x00401000u,outer_callback},{0x00401100u,nested_callback}};
const uint32_t recomp_dispatch_count=2;

static void stress(unsigned first) {
    unsigned last=first;
    for(unsigned i=0;i<2000;i++) {
        mach_enter(); preserved(last);
        unsigned next=first+2u+(i%8u)*2u; pattern(next);
        mach_enter(); preserved(next); mach_leave();
        hle_block_begin(); sched_yield(); hle_block_end(); preserved(next);
        mach_leave(); last=next;
    }
}
static void *worker(void *unused) {
    (void)unused; mach_enter();
    check("new thread uses default SSE control",g_mxcsr==0x1F80u);
    unsigned char zero[sizeof g_xmm]={0};
    check("new thread does not inherit vector contents",!memcmp(g_xmm,zero,sizeof zero));
    check("new thread has clean arithmetic flags",!g_flag_k&&!g_flag_a&&!g_flag_b&&!g_flag_cf);
    check("new thread has clean segments and GS base",!g_seg_cs&&!g_seg_ds&&!g_seg_es&&!g_seg_fs&&!g_seg_gs&&!g_seg_ss&&!g_gs_base);
    uint32_t sp=g_esp,fs=g_fs_base; pattern(2); mach_leave(); atomic_store(&stage,1);
    wait_stage(2);
    check("worker snapshot acquisition",mach_try_enter()); preserved(2);
    check("worker stack/TIB restored",g_esp==sp&&g_fs_base==fs);
    pattern(4); mach_leave(); atomic_store(&stage,3);
    wait_stage(4); stress(4); return NULL;
}
int main(void) {
    recomp_host_init(); mach_enter(); pattern(1);
    uint32_t sp=g_esp,fs=g_fs_base; mach_leave();
    pthread_t thread; if(pthread_create(&thread,NULL,worker,NULL))return 2;
    wait_stage(1); mach_enter(); preserved(1);
    uint32_t arg=0x55AA55AAu;
    check("outer callback return",hle_call_guest(0x00401000u,1,&arg)==0x1234ABCDu);
    preserved(1); check("owner stack/TIB after callbacks",g_esp==sp&&g_fs_base==fs);
    hle_block_begin(); atomic_store(&stage,2); wait_stage(3); hle_block_end(); preserved(1);
    check("owner stack/TIB after handoff",g_esp==sp&&g_fs_base==fs);
    mach_leave(); atomic_store(&stage,4); stress(1); pthread_join(thread,NULL);
    printf("%s: %u state assertions across callbacks, fresh threads, snapshots and 4000 handoffs; %d failures\n",
           atomic_load(&failures)?"FAIL":"PASS",atomic_load(&checks),atomic_load(&failures));
    return atomic_load(&failures)?1:0;
}
