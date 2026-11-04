#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "framebuffer.h"
static int failures,binds;
#define CHECK(s,c) do{if(!(c)){fprintf(stderr,"FAIL: %s\n",s);++failures;}}while(0)
static void bind(const civ3_surface *s,void *context){(void)context;CHECK("bound nonempty valid surface",s->pixels&&s->width>0&&s->height>0);++binds;}
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static civ3_surface live;
static void *writer(void *unused){(void)unused;for(int n=1;n<=1000;n++){pthread_mutex_lock(&lock);if(civ3_surface_resize(&live,16+n%7,13+n%5,bind,NULL))abort();for(int i=0;i<live.width*live.height;i++)live.pixels[i]=(uint32_t)n;pthread_mutex_unlock(&lock);}return NULL;}
static void *reader(void *unused){(void)unused;civ3_surface copy={0};for(int n=0;n<1000;n++){pthread_mutex_lock(&lock);int ok=civ3_surface_snapshot(&live,&copy)==0;pthread_mutex_unlock(&lock);if(ok){for(int i=1;i<copy.width*copy.height;i++)if(copy.pixels[i]!=copy.pixels[0])abort();}}civ3_surface_dispose(&copy);return NULL;}
int main(void){
    civ3_surface s={0},copy={0};
    int invalid[][2]={{0,10},{-1,5},{8193,1},{1,8193},{INT_MAX,INT_MAX},{INT_MIN,1}};
    for(unsigned i=0;i<sizeof invalid/sizeof *invalid;i++)CHECK("invalid geometry rejected",civ3_surface_resize(&s,invalid[i][0],invalid[i][1],bind,NULL)<0&&!s.pixels);
    CHECK("normal allocation",!civ3_surface_resize(&s,2,2,bind,NULL));uint32_t *old=s.pixels;
    s.pixels[0]=0xff112233;s.pixels[1]=0xff445566;s.pixels[2]=0xff778899;s.pixels[3]=0xffaabbcc;
    CHECK("same mode retains pixels",!civ3_surface_resize(&s,2,2,bind,NULL)&&s.pixels==old&&binds==1);
    CHECK("invalid mode preserves live buffer",civ3_surface_resize(&s,INT_MAX,2,bind,NULL)<0&&s.pixels==old&&s.pixels[0]==0xff112233);
    CHECK("independent snapshot",!civ3_surface_snapshot(&s,&copy)&&copy.pixels!=s.pixels);
    for(int n=0;n<500;n++)CHECK("repeat mode switch",!civ3_surface_resize(&s,17+n%5,19+n%7,bind,NULL));
    CHECK("snapshot survives live resizing",copy.width==2&&copy.height==2&&copy.pixels[3]==0xffaabbcc);
    char directory[]="/tmp/civ3-framebuffer.XXXXXX";CHECK("temporary directory",mkdtemp(directory)!=NULL);
    char path[512];snprintf(path,sizeof path,"%s/image.ppm",directory);
    CHECK("snapshot write",!civ3_surface_write_ppm(&copy,path));
    FILE *f=fopen(path,"rb");unsigned char bytes[64]={0};size_t got=f?fread(bytes,1,sizeof bytes,f):0;if(f)fclose(f);
    static const unsigned char expected[]={ 'P','6','\n','2',' ','2','\n','2','5','5','\n',0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc};
    CHECK("PPM exact RGB bytes",got==sizeof expected&&!memcmp(bytes,expected,sizeof expected));
    CHECK("bad output path reported",civ3_surface_write_ppm(&copy,directory)<0);
    CHECK("self snapshot rejected",civ3_surface_snapshot(&s,&s)<0);
    CHECK("empty surface snapshot rejected",civ3_surface_snapshot(&(civ3_surface){0},&s)<0);
    pthread_t a,b;civ3_surface_resize(&live,16,16,bind,NULL);
    if(pthread_create(&a,NULL,writer,NULL)||pthread_create(&b,NULL,reader,NULL))abort();
    pthread_join(a,NULL);pthread_join(b,NULL);
    civ3_surface_dispose(&live);civ3_surface_dispose(&s);civ3_surface_dispose(&copy);
    CHECK("dispose clears geometry",!s.pixels&&!s.width&&!s.height);unlink(path);rmdir(directory);
    if(!failures)puts("PASS: bounded geometry, repeated resize/release, independent concurrent snapshots and exact atomic PPM output");return failures?1:0;
}
