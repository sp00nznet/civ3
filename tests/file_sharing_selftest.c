/* Synthetic sharing and mapped-file preservation; no commercial game data. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "win32hle.h"
#include "host.h"
#define READ 0x80000000u
#define WRITE 0x40000000u
#define INVALID 0xFFFFFFFFu
const recomp_dispatch_entry_t recomp_dispatch_table[]={{0,NULL}};
const uint32_t recomp_dispatch_count=0;
static int failures;
static void check(const char *label,int ok) { if(!ok) { fprintf(stderr,"FAIL: %s\n",label);++failures; } }
static uint32_t call(const char *name,int n,const uint32_t *args) {
    uint32_t va=hle_resolve(name); if(!va) { fprintf(stderr,"Missing shim: %s\n",name);exit(2); }
    return hle_call_guest(va,n,args);
}
static uint32_t open_file(uint32_t path,uint32_t access,uint32_t share,uint32_t disposition) {
    uint32_t args[]={path,access,share,0,disposition,0,0};return call("CreateFileA",7,args);
}
static void close_file(uint32_t h) { if(h && h!=INVALID) call("CloseHandle",1,&h); }
static void denied(const char *label,uint32_t h) {
    check(label,h==INVALID && hle_get_last_error()==32);if(h!=INVALID)close_file(h);
}
int main(void) {
    char dir[]="/tmp/civ3-sharing.XXXXXX";if(!mkdtemp(dir))return 2;
    char path[512],alias[512];snprintf(path,sizeof path,"%s/save0.tmp",dir);snprintf(alias,sizeof alias,"%s/alias.tmp",dir);
    unsigned char data[32768];for(size_t i=0;i<sizeof data;i++)data[i]=(unsigned char)(i*37u+11u);
    memcpy(data+8643,"GAME",4);
    FILE *file=fopen(path,"wb");if(!file)return 2;
    if(fwrite(data,1,sizeof data,file)!=sizeof data)return 2;fclose(file);
    if(link(path,alias))return 2;
    recomp_host_init();mach_enter();
    uint32_t p=hle_guest_strdup(path),a=hle_guest_strdup(alias);
    uint32_t reader=open_file(p,READ,1,3);check("first reader opens",reader!=INVALID);
    if(reader==INVALID)return 2;
    uint32_t map_args[]={reader,0,2,0,0,0};uint32_t map=call("CreateFileMappingA",6,map_args);
    check("mapping created",map!=0);if(!map)return 2;
    uint32_t view_args[]={map,4,0,0,0};uint32_t view=call("MapViewOfFile",5,view_args);
    check("view created",view!=0);if(!view)return 2;
    uint32_t other=open_file(a,READ,3,3);check("compatible reader via alias",other!=INVALID);
    denied("CREATE_ALWAYS cannot truncate shared reader",open_file(p,WRITE,3,2));
    denied("TRUNCATE_EXISTING cannot truncate shared reader",open_file(a,WRITE,3,5));
    denied("existing writer denied through hard link",open_file(a,WRITE,3,3));
    denied("incoming share mode must allow existing reader",open_file(p,READ,0,3));
    struct stat st;check("denied opens preserved file size",stat(path,&st)==0 && st.st_size==(off_t)sizeof data);
    check("mapped save bytes preserved",memcmp(hle_ptr(view),data,sizeof data)==0);
    close_file(other);
    call("UnmapViewOfFile",1,&view);close_file(map);close_file(reader);
    uint32_t writer=open_file(p,WRITE,3,3);check("closing readers releases restriction",writer!=INVALID);
    denied("new reader must share existing writer",open_file(a,READ,1,3));
    other=open_file(a,READ,3,3);check("read/write sharing is bidirectional",other!=INVALID);
    close_file(other);close_file(writer);
    uint32_t exclusive=open_file(p,READ,0,3);check("exclusive open after closes",exclusive!=INVALID);
    denied("exclusive reader blocks reader",open_file(a,READ,3,3));close_file(exclusive);
    writer=open_file(p,WRITE,3,2);check("truncate succeeds once restrictions close",writer!=INVALID);
    check("successful truncate sets size zero",stat(path,&st)==0 && st.st_size==0);close_file(writer);
    for(int i=0;i<32;i++) {
        uint32_t h=open_file(p,READ|WRITE,0,3);check("repeated open/close does not leak restrictions",h!=INVALID);close_file(h);
    }
    mach_leave();unlink(alias);unlink(path);rmdir(dir);
    if(!failures)puts("PASS: bidirectional sharing, inode aliases, safe truncation, mapping preservation, and close/reopen lifetime");
    return failures?1:0;
}
