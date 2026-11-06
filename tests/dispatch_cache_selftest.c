/* Synthetic immutable dispatch table. No commercial addresses/code/assets. */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "win32hle.h"
#include "host.h"

static void fn0(void) { RET(0x101u,0); }
static void fn1(void) { RET(0x202u,0); }
static void fn2(void) { RET(0x303u,0); }
static void fn3(void) { RET(0x404u,0); }
#define BASE 0x00400000u
#define COUNT 8192u
#define FN(n) ((n)%4u==0?fn0:(n)%4u==1?fn1:(n)%4u==2?fn2:fn3)
#define E(n) { BASE+(n)*16u, FN(n) }
#define E4(n) E(n),E((n)+1),E((n)+2),E((n)+3)
#define E16(n) E4(n),E4((n)+4),E4((n)+8),E4((n)+12)
#define E64(n) E16(n),E16((n)+16),E16((n)+32),E16((n)+48)
#define E256(n) E64(n),E64((n)+64),E64((n)+128),E64((n)+192)
#define E1024(n) E256(n),E256((n)+256),E256((n)+512),E256((n)+768)
#define E4096(n) E1024(n),E1024((n)+1024),E1024((n)+2048),E1024((n)+3072)
const recomp_dispatch_entry_t recomp_dispatch_table[] = {
    {0,fn3}, E4096(0), E4096(4096), {UINT32_MAX,fn2}
};
const uint32_t recomp_dispatch_count=sizeof recomp_dispatch_table/sizeof *recomp_dispatch_table;
static atomic_uint assertions, failures;
static void check(const char *label,int ok) {
    atomic_fetch_add(&assertions,1);
    if(!ok&&atomic_fetch_add(&failures,1)<8)fprintf(stderr,"FAIL: %s\n",label);
}
static recomp_func_t oracle(uint32_t va) {
    if(!va)return fn3;
    if(va==UINT32_MAX)return fn2;
    if(va>=BASE&&va<BASE+COUNT*16u&&!(va&15u))return FN((va-BASE)/16u);
    return NULL;
}
static void exercise(uint32_t seed,unsigned iterations) {
    for(unsigned i=0;i<iterations;i++) {
        seed=seed*1664525u+1013904223u;
        uint32_t va;
        switch(i%6u) {
            case 0:va=BASE+(seed%COUNT)*16u;break;
            case 1:va=BASE+(seed%COUNT)*16u+1u;break;
            case 2:va=seed;break;
            case 3:va=0;break;
            case 4:va=UINT32_MAX;break;
            default:va=0x7F000000u+(seed%64u)*16u;break;
        }
        check("cold/cached/colliding lookup matches independent oracle",recomp_lookup(va)==oracle(va));
        check("repeated key matches independent oracle",recomp_lookup(va)==oracle(va));
    }
}
static void *worker(void *argument) {
    exercise((uint32_t)(uintptr_t)argument,25000); return NULL;
}
static double cpu_time(void) {
    struct timespec now;clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&now);
    return (double)now.tv_sec+(double)now.tv_nsec/1e9;
}
static int benchmark(void) {
    uint32_t keys[32];
    for(unsigned i=0;i<16;i++) {
        keys[2*i]=BASE+((i*997u)%COUNT)*16u;
        keys[2*i+1]=keys[2*i]+1u;
    }
    const unsigned n=5000000; unsigned found=0;
    double start=cpu_time();
    for(unsigned i=0;i<n;i++)found+=(recomp_lookup(keys[i%32u])!=NULL);
    double elapsed=cpu_time()-start;
    printf("BENCH dispatch iterations=%u cpu_seconds=%.9f hits=%u\n",n,elapsed,found);
    return found==n/2u?0:1;
}
int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"--benchmark"))return benchmark();
    for(unsigned i=0;i<recomp_dispatch_count;i++) {
        check("every synthetic table entry",recomp_lookup(recomp_dispatch_table[i].address)==recomp_dispatch_table[i].func);
        if(recomp_dispatch_table[i].address!=UINT32_MAX)
            check("missing adjacent byte stays missing",recomp_lookup(recomp_dispatch_table[i].address+1u)==NULL);
    }
    exercise(12345,50000);
    pthread_t threads[4];
    for(unsigned i=0;i<4;i++)if(pthread_create(&threads[i],NULL,worker,(void*)(uintptr_t)(i+1)))return 2;
    for(unsigned i=0;i<4;i++)pthread_join(threads[i],NULL);
    /* Import registration remains dynamic. A cached dispatch miss must still
     * visit the import resolver and refresh its pending-shim side effects. */
    recomp_host_init();mach_enter();
    win32hle_shim shims[]={{"DispatchFixtureA",fn1},{"DispatchFixtureB",fn2},{NULL,NULL}};
    win32hle_register(shims);
    uint32_t a=hle_resolve("DispatchFixtureA"),b=hle_resolve("DispatchFixtureB");
    for(unsigned i=0;i<1000;i++) {
        check("imports remain absent from fixed table",!recomp_lookup(a)&&!recomp_lookup(b));
        check("first import retains fresh dispatch state",hle_call_guest(a,0,NULL)==0x202u);
        check("second import retains fresh dispatch state",hle_call_guest(b,0,NULL)==0x303u);
    }
    mach_leave();
    printf("%s: %u dispatch assertions; all entries, misses, collisions, four threads and dynamic imports; %u failures\n",
           atomic_load(&failures)?"FAIL":"PASS",atomic_load(&assertions),atomic_load(&failures));
    return atomic_load(&failures)?1:0;
}
