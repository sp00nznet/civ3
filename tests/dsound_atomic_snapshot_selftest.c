/* DirectSound Lock/Unlock semantics: a guest can write a buffer while the
 * host audio callback is running. The playable snapshot must not contain torn
 * or partially written samples before a complete Unlock transaction.
 * Synthetic game-free test; SDL uses a dummy output device.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <SDL2/SDL.h>
#include "win32hle.h"
#include "host.h"

const recomp_dispatch_entry_t recomp_dispatch_table[] = {{0, NULL}};
const uint32_t recomp_dispatch_count = 0;

extern int hle_dsound_snapshot_for_test(uint32_t self, uint32_t offset,
                                        uint8_t *dst, uint32_t n);

#define DS_OK 0
static int failures;
static void require(const char *msg, int ok) {
    if (!ok) { ++failures; fprintf(stderr, "FAIL: %s\n", msg); }
}
static uint32_t invoke(const char *name, int argc, uint32_t *args) {
    uint32_t address=hle_resolve(name);
    if (!address) {fprintf(stderr, "Missing shim %s\n", name);exit(2);}
    return hle_call_guest(address, argc, args);
}
static uint32_t do_lock(uint32_t buffer, uint32_t offset, uint32_t bytes,
                        uint32_t *p1, uint32_t *n1, uint32_t *p2,uint32_t *n2) {
    uint32_t args[8]={buffer,offset,bytes,hle_va(p1),hle_va(n1),hle_va(p2),hle_va(n2),0};
    return invoke("IDirectSoundBuffer::Lock",8,args);
}
static uint32_t do_unlock(uint32_t buffer,uint32_t p1,uint32_t n1,uint32_t p2,uint32_t n2) {
    uint32_t args[5]={buffer,p1,n1,p2,n2};
    return invoke("IDirectSoundBuffer::Unlock",5,args);
}
static void release(uint32_t buffer) {
    invoke("IDirectSoundBuffer::Release",1,&buffer);
}

typedef struct {
    uint32_t buffer;
    atomic_int stop;
    atomic_uint reads, torn;
} reader_state;

static void *snapshot_reader(void *arg) {
    reader_state *state = arg;
    uint8_t pcm[128];
    while (!atomic_load(&state->stop)) {
        if (!hle_dsound_snapshot_for_test(state->buffer,0,pcm,sizeof pcm)) {
            atomic_fetch_add(&state->torn,1);
            break;
        }
        for (unsigned i=1;i<sizeof pcm;++i) {
            if (pcm[i]!=pcm[0]) {
                atomic_fetch_add(&state->torn,1);
                break;
            }
        }
        atomic_fetch_add(&state->reads,1);
        sched_yield();
    }
    return NULL;
}

int main(void) {
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    recomp_host_init();
    mach_enter();
    uint32_t *ds=hle_alloc(4), *out=hle_alloc(4),*desc=hle_alloc(24);
    uint32_t *p1=hle_alloc(4),*p2=hle_alloc(4),*n1=hle_alloc(4),*n2=hle_alloc(4);
    require("test allocations",ds&&out&&desc&&p1&&p2&&n1&&n2);
    if (!ds||!out||!desc||!p1||!p2||!n1||!n2) return 2;
    uint32_t create[3]={0,hle_va(ds),0};
    require("DirectSound create",invoke("DirectSoundCreate",3,create)==DS_OK && *ds);
    memset(desc,0,24);
    desc[0]=24;desc[2]=128;
    uint32_t create_buffer[4]={*ds,hle_va(desc),hle_va(out),0};
    require("secondary buffer",invoke("IDirectSound::CreateSoundBuffer",4,create_buffer)==DS_OK && *out);
    uint32_t original=*out;
    uint8_t observed[128]={0};
    require("initial playback snapshot",hle_dsound_snapshot_for_test(original,0,observed,128));
    for (unsigned i=0;i<128;i++) require("initial PCM zero",observed[i]==0);

    *p1=*p2=*n1=*n2=0;
    require("Lock first region",do_lock(original,0,32,p1,n1,p2,n2)==DS_OK && *n1==32 && *n2==0);
    memset(hle_ptr(*p1),0x5A,*n1);
    memset(observed,0xBB,sizeof observed);
    require("read playback snapshot before Unlock",hle_dsound_snapshot_for_test(original,0,observed,32));
    for(unsigned i=0;i<32;i++) require("write not heard before Unlock",observed[i]==0);
    require("Unlock completes atomic commit",do_unlock(original,*p1,*n1,*p2,*n2)==DS_OK);
    require("read playback snapshot after Unlock",hle_dsound_snapshot_for_test(original,0,observed,32));
    for(unsigned i=0;i<32;i++)require("new PCM available after Unlock",observed[i]==0x5A);

    *p1=*p2=*n1=*n2=0;
    require("wrap Lock",do_lock(original,120,16,p1,n1,p2,n2)==DS_OK && *n1==8 && *n2==8);
    memset(hle_ptr(*p1),0x11,8);
    memset(hle_ptr(*p2),0x22,8);
    require("wrap pre-commit private",
            hle_dsound_snapshot_for_test(original,120,observed,8));
    for(unsigned i=0;i<8;i++) require("tail unchanged until Unlock",observed[i]==0);
    require("commit wrapped PCM",do_unlock(original,*p1,*n1,*p2,*n2)==DS_OK);
    require("read tail",hle_dsound_snapshot_for_test(original,120,observed,8));
    for(unsigned i=0;i<8;i++) require("committed tail",observed[i]==0x11);
    require("read wrapped head",hle_dsound_snapshot_for_test(original,0,observed,8));
    for(unsigned i=0;i<8;i++) require("committed head",observed[i]==0x22);

    /* Deliberately pass an out-of-range guest pointer. It may not be used for
     * any copy and the correct old PCM must remain intact. */
    require("reject invalid pointer",
            do_unlock(original,*p1+90000,8,0,0)!=DS_OK);
    require("reject oversized first region",
            do_unlock(original,*p1,129,0,0)!=DS_OK);
    require("reject oversized wrapped region",
            do_unlock(original,*p1,8,*p2,129)!=DS_OK);
    require("reject wrapped pointer outside buffer start",
            do_unlock(original,*p1,8,*p2+1,8)!=DS_OK);
    require("bad Unlock left snapshot unchanged",
            hle_dsound_snapshot_for_test(original,120,observed,8));
    for(unsigned i=0;i<8;i++) require("original tail preserved",observed[i]==0x11);

    uint32_t duplicate_args[3]={*ds,original,hle_va(out)};
    require("duplicate shared PCM",invoke("IDirectSound::DuplicateSoundBuffer",3,duplicate_args)==DS_OK && *out);
    uint32_t duplicate=*out;
    require("duplicate sees committed PCM",hle_dsound_snapshot_for_test(duplicate,0,observed,8));
    for(unsigned i=0;i<8;i++) require("shared playback bytes",observed[i]==0x22);
    release(original);
    require("duplicate retains snapshot after original release",
            hle_dsound_snapshot_for_test(duplicate,120,observed,8));
    for(unsigned i=0;i<8;i++) require("duplicate bytes live",observed[i]==0x11);

    /* The reader uses the same mixer mutex as SDL. It never takes the guest
     * machine lock, so it races with staged guest writes and Unlock copies.
     * Each wrapped commit must become visible as one complete transaction. */
    require("full wrapped Lock",do_lock(duplicate,64,128,p1,n1,p2,n2)==DS_OK &&
            *n1==64 && *n2==64);
    memset(hle_ptr(*p1),0,*n1); memset(hle_ptr(*p2),0,*n2);
    require("initialize uniform snapshot",do_unlock(duplicate,*p1,*n1,*p2,*n2)==DS_OK);
    reader_state state = {.buffer=duplicate};
    pthread_t reader;
    int created=pthread_create(&reader,NULL,snapshot_reader,&state)==0;
    require("concurrent snapshot reader created",created);
    if (created) {
        while (!atomic_load(&state.reads)) sched_yield();
        for (unsigned i=1;i<=2000;++i) {
            memset(hle_ptr(*p1),(uint8_t)i,*n1);
            sched_yield();
            memset(hle_ptr(*p2),(uint8_t)i,*n2);
            require("atomic wrapped commit under contention",
                    do_unlock(duplicate,*p1,*n1,*p2,*n2)==DS_OK);
        }
        atomic_store(&state.stop,1);
        pthread_join(reader,NULL);
        require("reader observed committed snapshots",atomic_load(&state.reads)>0);
        require("reader never observed a torn transaction",!atomic_load(&state.torn));
        printf("Concurrent snapshot reads: %u; torn transactions: %u\n",
               atomic_load(&state.reads),atomic_load(&state.torn));
    }
    release(duplicate);
    uint32_t ds_handle=*ds;
    invoke("IDirectSound::Release",1,&ds_handle);
    mach_leave();
    if (!failures) puts("PASS: atomic DirectSound Unlock commit, wrapped PCM, bad pointer safety, duplicate buffer lifetime");
    return failures?1:0;
}
