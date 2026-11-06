/* Windows DIB row primitives. Callers validate geometry, coordinates and row
 * ownership. Indexed pixels are high-bit/high-nibble first; 24-bit is B,G,R.
 * Byte access avoids unaligned loads and never reads a fourth 24-bit byte. */
#pragma once
#include <stddef.h>
#include <stdint.h>
static inline uint32_t civ3_pixel_load(const uint8_t *row,size_t x,unsigned bpp) {
    switch(bpp) {
        case 1: return (row[x/8]>>(7-(x&7)))&1u;
        case 4: return (row[x/2]>>((x&1)?0:4))&15u;
        case 8: return row[x];
        case 16: row+=x*2;return (uint32_t)row[0]|(uint32_t)row[1]<<8;
        case 24: row+=x*3;return (uint32_t)row[0]|(uint32_t)row[1]<<8|(uint32_t)row[2]<<16;
        case 32: row+=x*4;return (uint32_t)row[0]|(uint32_t)row[1]<<8|(uint32_t)row[2]<<16|(uint32_t)row[3]<<24;
        default: return 0;
    }
}
static inline void civ3_pixel_store(uint8_t *row,size_t x,unsigned bpp,uint32_t v) {
    switch(bpp) {
        case 1: {unsigned shift=7u-(unsigned)(x&7);uint8_t mask=(uint8_t)(1u<<shift);
            row[x/8]=(uint8_t)((row[x/8]&~mask)|((v&1u)<<shift));break;}
        case 4: {unsigned shift=(x&1)?0:4;uint8_t mask=(uint8_t)(15u<<shift);
            row[x/2]=(uint8_t)((row[x/2]&~mask)|((v&15u)<<shift));break;}
        case 8: row[x]=(uint8_t)v;break;
        case 16: row+=x*2;row[0]=(uint8_t)v;row[1]=(uint8_t)(v>>8);break;
        case 24: row+=x*3;row[0]=(uint8_t)v;row[1]=(uint8_t)(v>>8);row[2]=(uint8_t)(v>>16);break;
        case 32: row+=x*4;row[0]=(uint8_t)v;row[1]=(uint8_t)(v>>8);row[2]=(uint8_t)(v>>16);row[3]=(uint8_t)(v>>24);break;
        default: break;
    }
}
