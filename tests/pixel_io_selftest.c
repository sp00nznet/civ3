/* Dependency-free pixel primitive checks; safe to run under ASan/UBSan. */
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "pixel_io.h"
static unsigned failures,checks;
static uint32_t rng=0xC193A54Fu;
static uint32_t random_value(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
#define CHECK(name,c) do{++checks;if(!(c)){fprintf(stderr,"FAIL: %s\n",name);if(++failures>10)return 1;}}while(0)
/* Independent bit-by-bit oracle, not the byte/nibble implementation. */
static void reference(uint8_t *row,size_t x,unsigned depth,uint32_t v){
    for(unsigned bit=0;bit<depth;bit++){
        size_t position=x*depth+bit;
        unsigned source=depth<8?depth-1-bit:bit;
        unsigned target=depth<8?7u-(unsigned)(position%8):(unsigned)(position%8);
        uint8_t mask=(uint8_t)(1u<<target);
        row[position/8]=(uint8_t)((row[position/8]&~mask)|(((v>>source)&1u)<<target));
    }
}
int main(void){
    const unsigned depths[]={1,4,8,16,24,32};
    uint8_t actual[1100],expected[1100];
    for(unsigned d=0;d<6;d++)for(size_t width=1;width<=257;width++){
        unsigned depth=depths[d];
        memset(actual,0xA5,sizeof actual);memcpy(expected,actual,sizeof actual);
        /* Deliberately unaligned row starts for 16/32-bit access. */
        for(unsigned k=0;k<101;k++){
            size_t x=k==0?width-1:random_value()%width;
            uint32_t v=random_value();
            civ3_pixel_store(actual+1,x,depth,v);reference(expected+1,x,depth,v);
            CHECK("exact storage and unchanged neighbors",!memcmp(actual,expected,sizeof actual));
            uint32_t mask=(uint32_t)((1ull<<depth)-1ull);
            CHECK("stored value round trip",civ3_pixel_load(actual+1,x,depth)==(v&mask));
        }
    }
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    uint8_t *m=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);
    if(m==MAP_FAILED||mprotect(m+page,page,PROT_NONE)){perror("guard page");return 2;}
    for(unsigned d=0;d<6;d++){
        unsigned depth=depths[d];size_t bytes=(depth+7)/8;
        uint8_t *row=m+page-bytes;memset(row,0xA5,bytes);
        civ3_pixel_store(row,0,depth,0x12345678u);
        CHECK("last guarded pixel exact access",civ3_pixel_load(row,0,depth)==(0x12345678u&(uint32_t)((1ull<<depth)-1ull)));
    }
    munmap(m,page*2);
    memset(actual,0xA5,sizeof actual);memcpy(expected,actual,sizeof actual);
    civ3_pixel_store(actual,0,7,0);CHECK("invalid depth does not write",!memcmp(actual,expected,sizeof actual));
    CHECK("invalid depth reads no data",civ3_pixel_load(NULL,0,7)==0);
    printf("%s: %u pixel checks, packed neighbors, unaligned rows and guard-page boundaries\n",failures?"FAIL":"PASS",checks);
    return failures?1:0;
}
