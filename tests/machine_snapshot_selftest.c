#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <time.h>
#include "win32hle.h"
#include "host.h"
#include "machine_snapshot.h"
const recomp_dispatch_entry_t recomp_dispatch_table[]={{0,NULL}};
const uint32_t recomp_dispatch_count=0;
static int failures;static atomic_int release_main;static atomic_int checked;
#define CHECK(s,c) do{if(!(c)){fprintf(stderr,"FAIL: %s\n",s);++failures;}}while(0)
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void *worker(void *unused){
    (void)unused;double start=now();int blocked=0;
    for(int i=0;i<10000;i++){if(mach_try_enter()){mach_leave();++blocked;}}
    CHECK("contended try never takes lock",blocked==0);CHECK("contended tries return promptly",now()-start<1.0);
    atomic_store(&checked,1);while(!atomic_load(&release_main)){struct timespec ts={0,1000000};nanosleep(&ts,NULL);}
    CHECK("worker acquires after release",mach_try_enter());g_eax=0xabcdef12u;
    CHECK("nested try acquisition",mach_try_enter());mach_leave();CHECK("nested leave preserves registers",g_eax==0xabcdef12u);mach_leave();
    CHECK("thread state preserved across reacquire",mach_try_enter()&&g_eax==0xabcdef12u);mach_leave();return NULL;
}
int main(void){
    recomp_host_init();mach_enter();g_eax=0x12345678u;
    pthread_t thread;if(pthread_create(&thread,NULL,worker,NULL))return 2;
    while(!atomic_load(&checked)){struct timespec t={0,1000000};nanosleep(&t,NULL);}
    CHECK("failed worker tries preserve owner state",g_eax==0x12345678u);mach_leave();atomic_store(&release_main,1);pthread_join(thread,NULL);
    CHECK("owner context restored",mach_try_enter()&&g_eax==0x12345678u);mach_leave();
    if(!failures)puts("PASS: nonblocking machine snapshots, contention, nested acquisition and register preservation");return failures?1:0;
}
