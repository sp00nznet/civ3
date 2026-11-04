/* Synthetic pixels and text only. --benchmark times real HLE API calls. */
#define _DARWIN_C_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "win32hle.h"
#include "host.h"
const recomp_dispatch_entry_t recomp_dispatch_table[] = {{0, NULL}};
const uint32_t recomp_dispatch_count = 0;
static int fails;
#define CHECK(label,cond) do { if (!(cond)) { fprintf(stderr,"FAIL: %s\n",label); ++fails; } } while(0)
static uint32_t invoke(const char *name, int n, const uint32_t *args) {
    uint32_t va=hle_resolve(name);
    if (!va) { fprintf(stderr,"missing %s\n",name); exit(2); }
    return hle_call_guest(va,n,args);
}
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec/1e9; }
static uint32_t rgb16(uint16_t p,int rgb555) {
    uint32_t r=rgb555 ? p>>10&31 : p>>11;
    uint32_t g=rgb555 ? (p>>5&31)<<1 | (p>>9&1) : p>>5&63;
    uint32_t b=p&31;
    return (r<<3|r>>2)<<16 | (g<<2|g>>4)<<8 | (b<<3|b>>2);
}
static uint64_t hash(const void *data,size_t size) {
    const uint8_t *p=data;uint64_t h=1469598103934665603ull;
    for(size_t i=0;i<size;i++) { h^=p[i];h*=1099511628211ull; }return h;
}
static void blit(uint32_t dst,int x,int y,int w,int h,uint32_t src,int sx,int sy,int sw,int sh) {
    uint32_t a[]={dst,(uint32_t)x,(uint32_t)y,(uint32_t)w,(uint32_t)h,src,(uint32_t)sx,(uint32_t)sy,(uint32_t)sw,(uint32_t)sh,0x00cc0020u};
    invoke("StretchBlt",11,a);
}
static void check_pixels(void) {
    enum {W=19,H=13};
    uint16_t source16[W*H]; uint32_t source32[W*H],dst[W*H],expected[W*H];
    for(int i=0;i<W*H;i++) { source16[i]=(uint16_t)(i*1237+17); source32[i]=0xa5000000u+(uint32_t)i*7711; }
    /* Both pitch signs; varied clipping, source cropping and true stretching. */
    for(int depth=16;depth<=32;depth+=16) for(int bottom=0;bottom<2;bottom++)
    for(int clipped=0;clipped<2;clipped++) for(int shape=0;shape<5;shape++) {
        for(int i=0;i<W*H;i++) dst[i]=expected[i]=0x12345678u;
        uint8_t *srcbytes=depth==16?(uint8_t*)source16:(uint8_t*)source32;
        int pitch=W*(depth/8);
        if(bottom) srcbytes+=(H-1)*pitch;
        uint32_t src=hle_gdi_surface_dc(srcbytes,W,H,bottom?-pitch:pitch,depth);
        uint32_t dc=hle_gdi_surface_dc((uint8_t*)dst,W,H,W*4,32);
        uint32_t region=0;
        if(clipped) { region=invoke("CreateRectRgn",4,(uint32_t[]){3,2,14,10});invoke("SelectClipRgn",2,(uint32_t[]){dc,region}); }
        int dx=shape==1?-3:shape==2?16:1,dy=shape==3?-3:1;
        int sx=shape==2?-2:0,sy=0,w=17,h=11,sw=shape==4?9:w,sh=shape==4?7:h;
        for(int j=0;j<h;j++) for(int i=0;i<w;i++) {
            int x=dx+i,y=dy+j,xx=sx+i*sw/w,yy=sy+j*sh/h;
            if(x<0||y<0||x>=W||y>=H||xx<0||yy<0||xx>=W||yy>=H)continue;
            if(clipped&&(x<3||y<2||x>=14||y>=10))continue;
            int index=(bottom?H-1-yy:yy)*W+xx;
            expected[y*W+x]=depth==16?rgb16(source16[index],0):source32[index];
        }
        blit(dc,dx,dy,w,h,src,sx,sy,sw,sh);
        CHECK("32-bit destination matches scalar reference",memcmp(dst,expected,sizeof dst)==0);
        hle_gdi_release_dc(src);hle_gdi_release_dc(dc);
        if(region)invoke("DeleteObject",1,&region);
    }
    /* Exhaust all 65536 bit patterns for both 16-bit formats. */
    uint16_t *all=malloc(65536*2);uint32_t *rgb=malloc(65536*4);
    if(!all||!rgb)exit(2);for(int i=0;i<65536;i++)all[i]=(uint16_t)i;
    for(int is555=0;is555<2;is555++) {
        uint32_t bmp=hle_gdi_make_dib(256,256,16,(uint8_t*)all,1,NULL,0,is555);
        uint32_t z=0,src=invoke("CreateCompatibleDC",1,&z),dc=hle_gdi_surface_dc((uint8_t*)rgb,256,256,1024,32);
        invoke("SelectObject",2,(uint32_t[]){src,bmp}); blit(dc,0,0,256,256,src,0,0,256,256);
        int good=1;for(int i=0;i<65536;i++)if(rgb[i]!=rgb16(all[i],is555)){good=0;break;}
        CHECK("all RGB555/RGB565 values preserved",good);
        hle_gdi_release_dc(src);hle_gdi_release_dc(dc);invoke("DeleteObject",1,&bmp);
    }
    free(all);free(rgb);
    /* Overlapping views retain the existing forward-copy behavior: fast path must decline. */
    for(int i=0;i<W*H;i++)dst[i]=expected[i]=(uint32_t)i;
    uint32_t same=hle_gdi_surface_dc((uint8_t*)dst,W,H,W*4,32);
    for(int y=0;y<8;y++)for(int x=0;x<12;x++)expected[(y+1)*W+x+2]=expected[y*W+x];
    blit(same,2,1,12,8,same,0,0,12,8);
    CHECK("overlapping storage uses reference behavior",memcmp(dst,expected,sizeof dst)==0);
    hle_gdi_release_dc(same);
}
static void check_saved_dc(void) {
    uint32_t old[16]={0},fresh[16]={0},z=0;
    hle_gdi_window_surface((uint8_t*)old,4,4,16,32);
    uint32_t dc=invoke("GetDC",1,&z);invoke("SaveDC",1,&dc);
    hle_gdi_window_surface((uint8_t*)fresh,4,4,16,32);
    invoke("RestoreDC",2,(uint32_t[]){dc,0xffffffffu});invoke("SetPixel",4,(uint32_t[]){dc,1,1,0x000000ffu});
    CHECK("saved window DC follows replaced framebuffer",fresh[5]==0x00ff0000u&&old[5]==0);
    hle_gdi_release_dc(dc);
    uint32_t mem=invoke("CreateCompatibleDC",1,&z);
    CHECK("released DC slot reused for regression",mem==dc);
    invoke("RestoreDC",2,(uint32_t[]){mem,0xffffffffu});invoke("SetPixel",4,(uint32_t[]){mem,2,2,0x000000ffu});
    CHECK("reused DC cannot restore another window's pixels",fresh[10]==0&&old[10]==0);
    hle_gdi_release_dc(mem);hle_gdi_window_surface(NULL,0,0,0,32);
}
static void benchmark(void) {
    enum {W=1024,H=768,N=40};size_t count=W*H;
    uint16_t *s16=malloc(count*2);uint32_t *s32=malloc(count*4),*d=malloc(count*4);
    if(!s16||!s32||!d)exit(2);
    for(size_t i=0;i<count;i++){s16[i]=(uint16_t)(i*1237);s32[i]=(uint32_t)(i*7711);d[i]=0;}
    uint32_t dc=hle_gdi_surface_dc((uint8_t*)d,W,H,W*4,32);
    uint32_t a=hle_gdi_surface_dc((uint8_t*)s16,W,H,W*2,16),b=hle_gdi_surface_dc((uint8_t*)s32,W,H,W*4,32);
    for(int depth=16;depth<=32;depth+=16){uint32_t source=depth==16?a:b;
        for(int i=0;i<2;i++)blit(dc,0,0,W,H,source,0,0,W,H);
        double t=now();for(int i=0;i<N;i++)blit(dc,0,0,W,H,source,0,0,W,H);
        printf("BENCH blit%d iterations=%d seconds=%.9f hash=%016llx\n",depth,N,now()-t,(unsigned long long)hash(d,count*4));
    }
    hle_gdi_release_dc(a);hle_gdi_release_dc(b);hle_gdi_release_dc(dc);free(s16);free(s32);free(d);
}
int main(int argc,char **argv) {
    recomp_host_init();mach_enter();
    check_pixels();
    if(argc==2&&!strcmp(argv[1],"--benchmark"))benchmark();else check_saved_dc();
    mach_leave();if(!fails)puts("PASS: graphics conversion/cropping/clipping and framebuffer/DC checks");return fails?1:0;
}
