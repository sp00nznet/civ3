/* Synthetic RIFF/direct-I/O regression. No commercial game data. */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "win32hle.h"
#include "host.h"

const recomp_dispatch_entry_t recomp_dispatch_table[] = {{0, NULL}};
const uint32_t recomp_dispatch_count = 0;
static int failures;
static void check(const char *name, int ok) {
    if (!ok) { fprintf(stderr,"FAIL: %s\n",name); ++failures; }
}
static uint32_t invoke(const char *name,int n,uint32_t *args) {
    uint32_t va=hle_resolve(name);
    if (!va) hle_fatal("missing MMIO shim %s",name);
    return hle_call_guest(va,n,args);
}
static uint32_t info(uint32_t h,uint32_t *state) {
    uint32_t a[3]={h,hle_va(state),0};
    return invoke("mmioGetInfo",3,a);
}
static uint32_t advance(uint32_t h,uint32_t *state) {
    uint32_t a[3]={h,hle_va(state),0};
    return invoke("mmioAdvance",3,a);
}
static uint32_t set_buffer(uint32_t h,uint32_t address,uint32_t bytes) {
    uint32_t a[4]={h,address,bytes,0};
    return invoke("mmioSetBuffer",4,a);
}
static uint32_t seek(uint32_t h,uint32_t offset,uint32_t origin) {
    uint32_t a[3]={h,offset,origin};
    return invoke("mmioSeek",3,a);
}
int main(void) {
    char path[]="/tmp/civ3-mmio-buffer-XXXXXX";
    int fd=mkstemp(path);
    if(fd<0)return 2;
    uint8_t wav[44+260]={0};
    memcpy(wav,"RIFF",4);memcpy(wav+8,"WAVEfmt ",8);
    wav[16]=16;wav[20]=1;wav[22]=1;wav[24]=0x22;wav[25]=0x56;
    wav[28]=0x44;wav[29]=0xAC;wav[32]=2;wav[34]=16;
    memcpy(wav+36,"data",4);wav[40]=4;wav[41]=1;
    wav[4]=40;wav[5]=1;
    for(unsigned i=44;i<sizeof wav;++i)wav[i]=(uint8_t)(i-44);
    if(write(fd,wav,sizeof wav)!=sizeof wav){close(fd);unlink(path);return 2;}
    close(fd);
    recomp_host_init();mach_enter();
    char guest[256];snprintf(guest,sizeof guest,"Z:%s",path);
    uint32_t name=hle_guest_strdup(guest),*state=hle_alloc(72);
    uint32_t args[3]={name,0,0};
    uint32_t h=invoke("mmioOpenA",3,args);
    check("open synthetic WAV",h!=0);
    if(!h){mach_leave();unlink(path);return 2;}
    check("seek to PCM payload",seek(h,44,0)==44);
    check("allocate a 32-byte read buffer",set_buffer(h,0,32)==0);
    check("read buffer information",info(h,state)==0);
    check("requested capacity honored",state[5]==32);
    check("fresh buffer starts empty",state[6]&&state[7]==state[6]&&state[8]==state[6]);
    if(state[5]!=32 || !state[6])goto finish;
    uint32_t buffer=state[6];
    check("reject interior pointer into owned buffer",set_buffer(h,buffer+1,8)!=0);
    check("reject growing aliased owned buffer",set_buffer(h,buffer,64)!=0);
    check("retain a smaller view of owned storage",set_buffer(h,buffer,16)==0);
    check("reject pointer inside allocation beyond smaller view",set_buffer(h,buffer+20,8)!=0);
    check("restore owned capacity",set_buffer(h,buffer,32)==0);
    unsigned copied=0;
    for(unsigned turn=0;turn<10;++turn) {
        check("advance direct PCM window",advance(h,state)==0);
        check("stable buffer allocation",state[6]==buffer);
        check("new window starts at buffer base",state[7]==buffer);
        uint32_t count=state[8]-state[7];
        check("bounded buffer pointers",count<=32);
        if(count>32)break;
        if(!count)break;
        check("window begins at current payload offset",state[10]==44+copied);
        check("disk offset follows valid bytes",state[11]==44+copied+count);
        check("no header, repeated or skipped PCM bytes",
              copied+count<=260&&!memcmp(hle_ptr(state[7]),wav+44+copied,count));
        copied+=count;
        state[7]=state[8];
    }
    check("every PCM byte delivered once",copied==260);
    check("EOF is an empty successful advance",advance(h,state)==0&&state[7]==state[8]);
    check("EOF preserves prior buffer bytes",!memcmp(hle_ptr(buffer),wav+44+256,4));

    check("seek back to payload",seek(h,44,0)==44);
    check("advance after seek",advance(h,state)==0);
    check("PCM start after seek",!memcmp(hle_ptr(state[7]),wav+44,32));
    state[7]+=5;
    uint32_t setinfo[3]={h,hle_va(state),0};
    check("commit direct read cursor",invoke("mmioSetInfo",3,setinfo)==0);
    check("relative seek observes direct cursor",seek(h,0,1)==49);
    check("GetInfo retains direct cursor",info(h,state)==0&&state[7]==buffer+5);
    state[7]=buffer-1;
    check("reject pointer before I/O buffer",invoke("mmioSetInfo",3,setinfo)!=0);
    check("invalid pointer preserved cursor",seek(h,0,1)==49);

    uint8_t *borrowed=hle_alloc(24);memset(borrowed,0xA5,24);
    check("install caller-owned buffer",set_buffer(h,hle_va(borrowed+8),8)==0);
    check("replaced internal allocation freed",!hle_heap_ok(hle_ptr(buffer)));
    check("refresh caller buffer information",info(h,state)==0);
    check("advance caller-owned buffer",advance(h,state)==0);
    check("borrowed buffer bytes",!memcmp(borrowed+8,wav+49,8));
    check("borrowed buffer guards",borrowed[7]==0xA5&&borrowed[16]==0xA5);
    check("reject negative capacity",set_buffer(h,0,UINT32_MAX)!=0);
    check("failure retains caller buffer",info(h,state)==0&&state[6]==hle_va(borrowed+8)&&state[5]==8);
    check("disable buffering",set_buffer(h,0,0)==0);
    check("disabled buffer reports no pointers",info(h,state)==0&&!state[5]&&!state[6]&&!state[7]&&!state[8]);
    check("cannot advance unbuffered file",advance(h,state)!=0);
    check("caller allocation still live",hle_heap_ok(borrowed));
    hle_free(borrowed);
finish:
    {uint32_t close_args[2]={h,0};check("close WAV",invoke("mmioClose",2,close_args)==0);}
    hle_free(hle_ptr(name));hle_free(state);mach_leave();unlink(path);
    if(!failures)puts("PASS: payload-only buffered reads, window refill, seek/cursor, EOF and caller ownership");
    return failures?1:0;
}
