/* Standalone source-only tests for safe, in-memory legacy WAV input gain. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "wav_pcm_gain.h"
static int failures;
static void check(const char *msg,int ok){if(!ok){fprintf(stderr,"FAIL %s\n",msg);++failures;}}
static void put16(uint8_t *p,unsigned v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static void header(uint8_t *p,size_t size,unsigned channels,unsigned bits) {
    memset(p,0,size);
    memcpy(p,"RIFF",4);put32(p+4,(uint32_t)size-8);
    memcpy(p+8,"WAVEfmt ",8);put32(p+16,16);
    put16(p+20,1);put16(p+22,channels);put32(p+24,22050);
    put16(p+32,channels*bits/8);put16(p+34,bits);
    memcpy(p+36,"data",4);put32(p+40,(uint32_t)size-44);
}
int main(void){
    uint8_t pcm16[52];
    header(pcm16,sizeof pcm16,2,16);
    put16(pcm16+44,30000);put16(pcm16+46,(uint16_t)-30000);
    put16(pcm16+48,200);put16(pcm16+50,(uint16_t)-200);
    uint8_t before[52];memcpy(before,pcm16,sizeof pcm16);
    check("16-bit four samples",civ3_wav_reduce_pcm(pcm16,sizeof pcm16,10)==4);
    check("16-bit positive",civ3_wav_u16(pcm16+44)==3000);
    check("16-bit negative",(int16_t)civ3_wav_u16(pcm16+46)==-3000);
    check("16-bit quiet samples",(int16_t)civ3_wav_u16(pcm16+48)==20 &&
          (int16_t)civ3_wav_u16(pcm16+50)==-20);
    check("wave header remains exact",memcmp(before,pcm16,44)==0);
    uint8_t pcm8[48];header(pcm8,sizeof pcm8,1,8);
    pcm8[44]=0;pcm8[45]=128;pcm8[46]=255;pcm8[47]=140;
    check("8-bit four samples",civ3_wav_reduce_pcm(pcm8,sizeof pcm8,10)==4);
    check("8-bit silence stays centered",pcm8[45]==128);
    check("8-bit signed bias preserved",pcm8[44]==116 && pcm8[46]==140 && pcm8[47]==129);

    /* Malformed file must stay unchanged, never touch beyond allocation. */
    uint8_t guard[54];memset(guard,0xA5,sizeof guard);
    memcpy(guard+1,before,sizeof before);
    guard[1+4]=0xFF;guard[1+5]=0xFF;guard[1+6]=0xFF;guard[1+7]=0x7F;
    uint8_t saved[54];memcpy(saved,guard,sizeof guard);
    check("huge forged RIFF length refused",civ3_wav_reduce_pcm(guard+1,52,10)==0);
    check("failed WAV validation did not change bytes",memcmp(guard,saved,sizeof guard)==0);
    check("guard bytes unchanged",guard[0]==0xA5 && guard[53]==0xA5);
    memcpy(guard+1,before,52);
    guard[1+32]=0; /* invalid block alignment */
    memcpy(saved,guard,sizeof guard);
    check("invalid PCM layout refused",civ3_wav_reduce_pcm(guard+1,52,10)==0);
    check("invalid PCM layout untouched",memcmp(saved,guard,sizeof guard)==0);
    memcpy(guard+1,before,52);
    put32(guard+1+40,0xFFFFFFFFu);
    memcpy(saved,guard,sizeof guard);
    check("invalid chunk length refused",civ3_wav_reduce_pcm(guard+1,52,10)==0);
    check("invalid chunk unchanged",memcmp(saved,guard,sizeof guard)==0);

    check("NULL input safely rejected",civ3_wav_reduce_pcm(NULL,52,10)==0);
    check("zero gain divisor not applied",civ3_wav_reduce_pcm(pcm16,52,0)==0);
    check("already unity not modified",civ3_wav_reduce_pcm(pcm16,52,1)==0);
    /* Randomly malformed headers must not read or write past allocation. */
    uint32_t seed=0x8C1994D2;
    for (int i=0;i<50000;++i){
        uint8_t bytes[64];
        for (int j=0;j<64;++j){seed=seed*1664525u+1013904223u;bytes[j]=(uint8_t)(seed>>24);}
        if(i%2==0) memcpy(bytes,"RIFF",4);
        if(i%3==0) memcpy(bytes+8,"WAVE",4);
        (void)civ3_wav_reduce_pcm(bytes,64,10);
    }
    if(!failures)puts("PASS: WAV PCM gain 8/16-bit formats, RIFF bounds, header preservation and 50k malformed cases");
    return failures?1:0;
}
