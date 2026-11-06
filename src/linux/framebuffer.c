#define _POSIX_C_SOURCE 200809L
#include "framebuffer.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
/* Explicit geometry cap bounds allocations and pitch arithmetic. */
static int valid(int w,int h) { return w>0 && h>0 && w<=8192 && h<=8192; }
void civ3_surface_dispose(civ3_surface *s) {
    if(s){free(s->pixels);s->pixels=NULL;s->width=s->height=0;}
}
int civ3_surface_resize(civ3_surface *s,int w,int h,civ3_surface_bind bind,void *context) {
    if(!s||!valid(w,h)){errno=EINVAL;return -1;}
    if(s->pixels && s->width==w && s->height==h)return 0;
    uint32_t *next=calloc((size_t)w*(size_t)h,sizeof *next);
    if(!next)return -1;
    uint32_t *old=s->pixels;
    s->pixels=next;s->width=w;s->height=h;
    /* Consumers rebind while old storage is still alive. Saved DCs are
     * refreshed by the companion gdidc patch before the old buffer is freed. */
    if(bind)bind(s,context);
    free(old);return 0;
}
int civ3_surface_snapshot(const civ3_surface *s,civ3_surface *copy) {
    if(!s||!copy||s==copy||!s->pixels||!valid(s->width,s->height)){errno=EINVAL;return -1;}
    size_t bytes=(size_t)s->width*(size_t)s->height*sizeof *s->pixels;
    uint32_t *pixels=malloc(bytes);if(!pixels)return -1;
    memcpy(pixels,s->pixels,bytes);
    civ3_surface_dispose(copy);
    copy->pixels=pixels;copy->width=s->width;copy->height=s->height;return 0;
}
int civ3_surface_write_ppm(const civ3_surface *s,const char *path) {
    if(!s||!s->pixels||!valid(s->width,s->height)||!path||!*path){errno=EINVAL;return -1;}
    size_t n=strlen(path);if(n>SIZE_MAX-16){errno=ENAMETOOLONG;return -1;}
    char *temporary=malloc(n+16);uint8_t *row=malloc((size_t)s->width*3);
    if(!temporary||!row){free(temporary);free(row);errno=ENOMEM;return -1;}
    snprintf(temporary,n+16,"%s.tmp.XXXXXX",path);
    int fd=mkstemp(temporary);if(fd<0){int e=errno;free(temporary);free(row);errno=e;return -1;}
    FILE *f=fdopen(fd,"wb");
    if(!f){int e=errno;close(fd);unlink(temporary);free(temporary);free(row);errno=e;return -1;}
    int failed=fprintf(f,"P6\n%d %d\n255\n",s->width,s->height)<0;
    for(int y=0;y<s->height&&!failed;y++) {
        const uint32_t *source=s->pixels+(size_t)y*s->width;
        for(int x=0;x<s->width;x++){row[x*3]=(uint8_t)(source[x]>>16);row[x*3+1]=(uint8_t)(source[x]>>8);row[x*3+2]=(uint8_t)source[x];}
        failed=fwrite(row,3,(size_t)s->width,f)!=(size_t)s->width;
    }
    int saved=failed?(errno?errno:EIO):0;
    if(fclose(f)&&!saved)saved=errno?errno:EIO;
    if(!saved&&rename(temporary,path))saved=errno;
    if(saved)unlink(temporary);
    free(temporary);free(row);if(saved){errno=saved;return -1;}return 0;
}
