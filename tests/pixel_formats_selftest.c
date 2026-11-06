/* Synthetic DIBs only. Baseline mode skips deliberate guard/overflow probes. */
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "win32hle.h"
#include "host.h"
const recomp_dispatch_entry_t recomp_dispatch_table[]={{0,NULL}};
const uint32_t recomp_dispatch_count=0;
static int failures;
#define CHECK(s,c) do{if(!(c)){fprintf(stderr,"FAIL: %s\n",s);++failures;}}while(0)
static uint32_t call(const char *s,int n,const uint32_t *a){uint32_t v=hle_resolve(s);if(!v)hle_fatal("missing %s",s);return hle_call_guest(v,n,a);}
static uint32_t color(unsigned i){return ((i*53u&255)<<16)|((i*97u&255)<<8)|(i*193u&255);}
static uint32_t cr(uint32_t rgb){return (rgb&255)<<16|(rgb&0xFF00)|(rgb>>16&255);}
static unsigned index_at(int x,int y,int bpp){return (unsigned)(x+y*7)&((1u<<bpp)-1);}
/* Independent fixture encoder, never uses the implementation's helpers. */
static void encode(uint8_t *r,int x,int bpp,uint32_t v){
    if(bpp==1){unsigned shift=7u-(unsigned)x%8u;r[x/8]=(uint8_t)((r[x/8]&~(1u<<shift))|((v&1)<<shift));}
    else if(bpp==4){unsigned shift=x%2?0:4;r[x/2]=(uint8_t)((r[x/2]&~(15u<<shift))|((v&15)<<shift));}
    else for(int k=0;k<bpp/8;k++)r[x*(bpp/8)+k]=(uint8_t)(v>>(k*8));
}
static void formats(void){
    const int widths[]={1,2,3,4,7,9,17};
    const int depths[]={1,4,8,24};
    uint32_t *b=hle_alloc(1100),*bits=hle_alloc(4);if(!b||!bits)exit(2);
    for(unsigned z=0;z<4;z++)for(unsigned j=0;j<7;j++)for(int bottom=0;bottom<2;bottom++){
        int depth=depths[z],w=widths[j],h=3,pitch=(w*depth+31)/32*4,colors=depth<=8?(1<<depth):0;
        memset(b,0,1100);b[0]=40;b[1]=(uint32_t)w;b[2]=(uint32_t)(bottom?h:-h);b[3]=1u|((uint32_t)depth<<16);
        for(int i=0;i<colors;i++)b[10+i]=color((unsigned)i);
        uint32_t bitmap=call("CreateDIBSection",6,(uint32_t[]){0,hle_va(b),0,hle_va(bits),0,0});
        CHECK("DIB accepted",bitmap&&*bits);if(!bitmap||!*bits)continue;
        uint8_t *raw=hle_ptr(*bits);memset(raw,0xA5,(size_t)pitch*h);
        uint32_t expected[51],out[51],input[51];
        for(int y=0;y<h;y++)for(int x=0;x<w;x++){
            unsigned v=depth<=8?index_at(x,y,depth):(unsigned)(x+y*19+1);
            uint32_t rgb=color(v);expected[y*w+x]=rgb;input[y*w+x]=color(depth<=8?((v+1)&((1u<<depth)-1)):v+1);
            encode(raw+(bottom?h-1-y:y)*pitch,x,depth,depth<=8?v:rgb);
        }
        uint32_t dc=call("CreateCompatibleDC",1,(uint32_t[]){0});call("SelectObject",2,(uint32_t[]){dc,bitmap});
        memset(out,0xCC,sizeof out);uint32_t outdc=hle_gdi_surface_dc((uint8_t*)out,w,h,w*4,32);
        call("BitBlt",9,(uint32_t[]){outdc,0,0,(uint32_t)w,(uint32_t)h,dc,0,0,0xCC0020});
        char label[100];snprintf(label,sizeof label,"read %dbpp width=%d bottom=%d",depth,w,bottom);
        CHECK(label,!memcmp(out,expected,(size_t)w*h*4));
        uint8_t before[256],want[256];memcpy(before,raw,(size_t)pitch*h);memcpy(want,before,(size_t)pitch*h);
        for(int y=0;y<h;y++)for(int x=0;x<w;x++){
            unsigned v=depth<=8?((index_at(x,y,depth)+1)&((1u<<depth)-1)):(unsigned)(x+y*19+2);
            encode(want+(bottom?h-1-y:y)*pitch,x,depth,depth<=8?v:color(v));
        }
        uint32_t in=hle_gdi_surface_dc((uint8_t*)input,w,h,w*4,32);
        call("BitBlt",9,(uint32_t[]){dc,0,0,(uint32_t)w,(uint32_t)h,in,0,0,0xCC0020});
        snprintf(label,sizeof label,"write %dbpp width=%d bottom=%d preserves padding",depth,w,bottom);
        CHECK(label,!memcmp(raw,want,(size_t)pitch*h));
        call("SetPixel",4,(uint32_t[]){dc,0,0,cr(color(depth<=8?1:7))});
        encode(want+(bottom?h-1:0)*pitch,0,depth,depth<=8?1:color(7));
        CHECK("single pixel write preserves neighbors",!memcmp(raw,want,(size_t)pitch*h));
        hle_gdi_release_dc(in);hle_gdi_release_dc(outdc);call("DeleteDC",1,&dc);call("DeleteObject",1,&bitmap);
    }
    hle_free(bits);hle_free(b);
}
static void palette_bounds(void){
    uint32_t *b=hle_alloc(1100),*bits=hle_alloc(4),*palette=hle_alloc(1024);if(!b||!bits||!palette)exit(2);
    for(int i=0;i<256;i++)palette[i]=color((unsigned)i);
    const int depths[]={1,4,8};
    for(int k=0;k<3;k++)for(int short_table=0;short_table<2;short_table++){
        int depth=depths[k],n=short_table?2:(1<<depth);
        memset(b,0,1100);b[0]=40;b[1]=1;b[2]=UINT_MAX;b[3]=1u|((uint32_t)depth<<16);b[8]=(uint32_t)n;
        uint32_t bmp=call("CreateDIBSection",6,(uint32_t[]){0,hle_va(b),0,hle_va(bits),0,0});
        uint32_t dc=call("CreateCompatibleDC",1,(uint32_t[]){0});call("SelectObject",2,(uint32_t[]){dc,bmp});
        CHECK("palette request clamps without unsigned overflow",call("SetDIBColorTable",4,(uint32_t[]){dc,1,UINT_MAX,hle_va(palette)})==(uint32_t)n-1);
        CHECK("out of table start rejected",call("SetDIBColorTable",4,(uint32_t[]){dc,(uint32_t)n,1,hle_va(palette)})==0);
        CHECK("null palette input rejected",call("SetDIBColorTable",4,(uint32_t[]){dc,0,1,0})==0);
        CHECK("zero count is safe",call("SetDIBColorTable",4,(uint32_t[]){dc,0,0,0})==0);
        call("DeleteDC",1,&dc);call("DeleteObject",1,&bmp);
    }
    hle_free(palette);hle_free(bits);hle_free(b);
}
static void guarded(void){
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    uint8_t *memory=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);
    if(memory==MAP_FAILED||mprotect(memory+page,page,PROT_NONE))exit(2);
    uint8_t *last=memory+page-3;last[0]=0x12;last[1]=0x34;last[2]=0x56;
    uint32_t screen=0,out=hle_gdi_surface_dc((uint8_t*)&screen,1,1,4,32),in=hle_gdi_surface_dc(last,1,1,3,24);
    call("BitBlt",9,(uint32_t[]){out,0,0,1,1,in,0,0,0xCC0020});
    CHECK("24-bit final pixel reads exactly three bytes",screen==0x563412);
    screen=0xAABBCC;call("BitBlt",9,(uint32_t[]){in,0,0,1,1,out,0,0,0xCC0020});
    CHECK("24-bit final pixel writes exactly three bytes",last[0]==0xCC&&last[1]==0xBB&&last[2]==0xAA);
    hle_gdi_release_dc(in);hle_gdi_release_dc(out);munmap(memory,page*2);
}
static void palette_remap(void){
    uint32_t *b=hle_alloc(1100),*bits=hle_alloc(4),*update=hle_alloc(4);
    if(!b||!bits||!update)exit(2);
    const int depths[]={1,4,8};
    for(int z=0;z<3;z++){
        int depth=depths[z],n=1<<depth,w=n;
        memset(b,0,1100);b[0]=40;b[1]=(uint32_t)w;b[2]=UINT_MAX;b[3]=1u|((uint32_t)depth<<16);
        for(int i=0;i<n;i++)b[10+i]=color((unsigned)i);
        uint32_t src=call("CreateDIBSection",6,(uint32_t[]){0,hle_va(b),0,hle_va(bits),0,0});
        uint8_t *raw=hle_ptr(*bits);
        for(int i=0;i<n;i++)encode(raw,i,depth,(uint32_t)i);
        for(int i=0;i<n;i++)b[10+i]=color((unsigned)(n-1-i));
        uint32_t dst=call("CreateDIBSection",6,(uint32_t[]){0,hle_va(b),0,hle_va(bits),0,0});
        uint8_t *dest=hle_ptr(*bits),want[256]={0};
        for(int i=0;i<n;i++)encode(want,i,depth,(uint32_t)(n-1-i));
        uint32_t a=call("CreateCompatibleDC",1,(uint32_t[]){0}),d=call("CreateCompatibleDC",1,(uint32_t[]){0});
        call("SelectObject",2,(uint32_t[]){a,src});call("SelectObject",2,(uint32_t[]){d,dst});
        call("BitBlt",9,(uint32_t[]){d,0,0,(uint32_t)w,1,a,0,0,0xCC0020});
        CHECK("indexed copy remaps different color tables",!memcmp(dest,want,(size_t)(w*depth+7)/8));
        *update=0x123456u;
        CHECK("palette mutation succeeds",call("SetDIBColorTable",4,(uint32_t[]){a,0,1,hle_va(update)})==1);
        uint32_t pixel=0,view=hle_gdi_surface_dc((uint8_t*)&pixel,1,1,4,32);
        call("BitBlt",9,(uint32_t[]){view,0,0,1,1,a,0,0,0xCC0020});
        CHECK("palette mutation visible without reselect",pixel==*update);
        hle_gdi_release_dc(view);call("DeleteDC",1,&a);call("DeleteDC",1,&d);
        call("DeleteObject",1,&src);call("DeleteObject",1,&dst);
    }
    hle_free(update);hle_free(bits);hle_free(b);
}
static uint32_t state=0xABCDE123u;
static unsigned next_random(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
static void scalar_reference(void){
    uint8_t source[9*7*3];
    for(int i=0;i<9*7;i++)encode(source,i,24,color((unsigned)i+1));
    for(unsigned trial=0;trial<1500;trial++){
        uint32_t got[11*8],expected[11*8];for(int i=0;i<88;i++)got[i]=expected[i]=0xDA000000u+(uint32_t)i;
        int dx=(int)(next_random()%21)-10,dy=(int)(next_random()%17)-8;
        int sx=(int)(next_random()%13)-6,sy=(int)(next_random()%11)-5;
        int w=1+(int)(next_random()%25),h=1+(int)(next_random()%21);
        int sw=1+(int)(next_random()%19),sh=1+(int)(next_random()%17);
        int left=(int)(next_random()%6),top=(int)(next_random()%4),right=6+(int)(next_random()%6),bottom=4+(int)(next_random()%5);
        uint32_t in=hle_gdi_surface_dc(source,9,7,27,24),out=hle_gdi_surface_dc((uint8_t*)got,11,8,44,32);
        uint32_t clip=call("CreateRectRgn",4,(uint32_t[]){(uint32_t)left,(uint32_t)top,(uint32_t)right,(uint32_t)bottom});
        call("SelectClipRgn",2,(uint32_t[]){out,clip});
        /* Deliberately retain the original simple per-request-pixel model. */
        for(int j=0;j<h;j++)for(int i=0;i<w;i++){
            int x=dx+i,y=dy+j,xx=sx+i*sw/w,yy=sy+j*sh/h;
            if(x>=0&&x<11&&y>=0&&y<8&&x>=left&&x<right&&y>=top&&y<bottom&&xx>=0&&xx<9&&yy>=0&&yy<7)
                expected[y*11+x]=color((unsigned)(yy*9+xx)+1);
        }
        call("StretchBlt",11,(uint32_t[]){out,(uint32_t)dx,(uint32_t)dy,(uint32_t)w,(uint32_t)h,in,(uint32_t)sx,(uint32_t)sy,(uint32_t)sw,(uint32_t)sh,0xCC0020});
        CHECK("clipped scaling agrees with independent scalar reference",!memcmp(got,expected,sizeof got));
        hle_gdi_release_dc(in);hle_gdi_release_dc(out);call("DeleteObject",1,&clip);
    }
}
static void giant_clipped(void){
    uint8_t rgb[12]={1,2,3,4,5,6,7,8,9,10,11,12};uint32_t dest[4]={0};
    uint32_t in=hle_gdi_surface_dc(rgb,2,2,6,24),out=hle_gdi_surface_dc((uint8_t*)dest,2,2,8,32);
    call("StretchBlt",11,(uint32_t[]){out,0,0,INT_MAX,INT_MAX,in,0,0,2,2,0xCC0020});
    for(int i=0;i<4;i++)CHECK("huge clipped stretch only visits visible pixels",dest[i]==0x030201);
    memset(dest,0,sizeof dest);
    call("StretchBlt",11,(uint32_t[]){out,UINT_MAX,UINT_MAX,INT_MAX,INT_MAX,in,0,0,INT_MAX,INT_MAX,0xCC0020});
    CHECK("large products do not overflow mapping",dest[0]==0x0C0B0A);
    call("StretchBlt",11,(uint32_t[]){out,INT_MIN,INT_MIN,INT_MAX,INT_MAX,in,0,0,INT_MAX,INT_MAX,0xCC0020});
    hle_gdi_release_dc(in);hle_gdi_release_dc(out);
}
int main(int argc,char **argv){
    int safe=argc>1&&!strcmp(argv[1],"--safe-baseline"),guard=argc>1&&!strcmp(argv[1],"--guard-only");
    recomp_host_init();mach_enter();
    if(guard)guarded();else{formats();if(!safe){palette_bounds();palette_remap();scalar_reference();guarded();giant_clipped();}}
    mach_leave();if(!failures)puts("PASS: indexed/24-bit reads and writes, padding, palette bounds, guard page and clipped extreme sizes");
    return failures?1:0;
}
