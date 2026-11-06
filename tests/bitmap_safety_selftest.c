/* Synthetic headers and pixels only. --safe-baseline avoids dangerous inputs
 * on the old implementation while proving invalid-header and table failures. */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "win32hle.h"
#include "host.h"
#include "bitmap_layout.h"
const recomp_dispatch_entry_t recomp_dispatch_table[] = {{0, NULL}};
const uint32_t recomp_dispatch_count = 0;
static int failures;
#define CHECK(label, condition) do { if (!(condition)) { fprintf(stderr,"FAIL: %s\n",label); ++failures; } } while (0)
static uint32_t call(const char *name, int count, const uint32_t *args) {
    uint32_t target=hle_resolve(name);
    if(!target) hle_fatal("missing bitmap test shim %s",name);
    return hle_call_guest(target,count,args);
}
static void destroy(uint32_t h) { if(h) call("DeleteObject",1,&h); }
static void header(uint32_t *b, int32_t w, int32_t h, unsigned depth) {
    memset(b,0,1100);b[0]=40;b[1]=(uint32_t)w;b[2]=(uint32_t)h;b[3]=1u|(depth<<16);
}
static uint32_t dib(uint32_t *b,uint32_t *pixels) {
    *pixels=0xFFFFFFFFu;
    return call("CreateDIBSection",6,(uint32_t[]){0,hle_va(b),0,hle_va(pixels),0,0});
}
static void reject(const char *name,uint32_t *b,uint32_t *pixels) {
    uint32_t h=dib(b,pixels);CHECK(name,h==0&&*pixels==0);destroy(h);
}
int main(int argc,char **argv) {
    int safe=argc>1&&!strcmp(argv[1],"--safe-baseline");
    int h,pitch;uint32_t size;
    CHECK("layout 24-bit DWORD row padding",civ3_bitmap_layout(3,-2,24,&h,&pitch,&size)&&h==2&&pitch==12&&size==24);
    CHECK("layout overflow rejected",!civ3_bitmap_layout(INT_MAX,INT_MAX,32,&h,&pitch,&size));
    CHECK("layout signed-min height rejected",!civ3_bitmap_layout(2,INT_MIN,16,&h,&pitch,&size));
    CHECK("layout over host budget rejected",!civ3_bitmap_layout(8192,8192,32,&h,&pitch,&size));
    recomp_host_init();mach_enter();
    uint32_t *b=hle_alloc(1100),*pixels=hle_alloc(4);
    if(!b||!pixels)return 2;
    header(b,0,2,16);reject("zero-width DIB returns NULL and clears bits",b,pixels);
    header(b,2,0,16);reject("zero-height DIB returns NULL and clears bits",b,pixels);
    header(b,2,2,16);b[0]=12;reject("short bitmap header rejected",b,pixels);
    header(b,2,2,16);b[3]=2u|(16u<<16);reject("multiple planes rejected",b,pixels);
    header(b,2,2,16);b[4]=1;reject("compressed section rejected",b,pixels);
    if(!safe) {
        header(b,-2,2,16);reject("negative width rejected",b,pixels);
        header(b,2,INT_MIN,16);reject("INT_MIN height rejected",b,pixels);
        header(b,INT_MAX,INT_MAX,32);reject("overflow geometry rejected",b,pixels);
        header(b,2,2,8);b[8]=UINT_MAX;reject("oversized palette rejected",b,pixels);
        header(b,2,2,3);reject("unsupported depth rejected",b,pixels);
        header(b,2,2,16);b[0]=UINT_MAX;reject("oversized header rejected",b,pixels);
        *pixels=UINT_MAX;
        CHECK("null header rejected",call("CreateDIBSection",6,(uint32_t[]){0,0,0,hle_va(pixels),0,0})==0&&*pixels==0);
    }
    for(unsigned depth=1;depth<=32;depth++) {
        if(!(depth==1||depth==4||depth==8||depth==16||depth==24||depth==32))continue;
        for(int top=0;top<2;top++) {
            header(b,7,top?-3:3,depth);uint32_t handle=dib(b,pixels);
            CHECK("valid DIB accepted",handle&&*pixels);
            if(handle&&*pixels) {
                CHECK("known layout",civ3_bitmap_layout(7,3,depth,&h,&pitch,&size));
                CHECK("allocation covers padded image",hle_heap_size(hle_ptr(*pixels))>=size);
                ((uint8_t*)hle_ptr(*pixels))[size-1]=0xA5;
            }
            destroy(handle);
        }
    }
    if(!safe) {
        uint32_t object=call("CreateBitmap",5,(uint32_t[]){257,3,1,24,0});
        CHECK("normalized 24-bit owned bitmap",object!=0);
        uint32_t dc=call("CreateCompatibleDC",1,(uint32_t[]){0});
        call("SelectObject",2,(uint32_t[]){dc,object});
        call("PatBlt",6,(uint32_t[]){dc,0,0,257,3,0x00FF0062u});
        CHECK("last owned pixel safely writable",call("GetPixel",3,(uint32_t[]){dc,256,2})==0xFFFFFFu);
        call("DeleteDC",1,&dc);destroy(object);
        CHECK("negative owned geometry rejected",call("CreateBitmap",5,(uint32_t[]){UINT_MAX,2,1,32,0})==0);
        CHECK("huge compatible bitmap rejected",call("CreateCompatibleBitmap",3,(uint32_t[]){0,INT_MAX,INT_MAX})==0);
    }
    uint32_t handles[9000];unsigned count=0;
    for(;count<9000;count++) { handles[count]=call("CreateSolidBrush",1,(uint32_t[]){count});if(!handles[count])break; }
    CHECK("object table has a bounded capacity",count>8000&&count<9000);
    header(b,2,2,16);reject("full table must not return a fabricated bitmap handle",b,pixels);
    CHECK("owned allocation fails cleanly at table limit",call("CreateBitmap",5,(uint32_t[]){2,2,1,32,0})==0);
    for(unsigned i=0;i<count;i++)destroy(handles[i]);
    header(b,2,2,16);uint32_t restored=dib(b,pixels);CHECK("table reusable after cleanup",restored&&*pixels);destroy(restored);
    if(!safe) for(unsigned i=0;i<10000;i++) { header(b,2,2,16);uint32_t q=dib(b,pixels);if(!q){CHECK("repeated DIB recycling",0);break;}destroy(q); }
    hle_free(pixels);hle_free(b);mach_leave();
    if(!failures)puts("PASS: bitmap layout, invalid headers, format bounds, table exhaustion and repeated recycling");
    return failures?1:0;
}
