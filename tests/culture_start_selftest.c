/* Synthetic guest addresses only; no commercial game code or data. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct { uint32_t address, value; } memory[256];
static unsigned used, reads, failures;
static void put(uint32_t address, uint32_t value) {
    for (unsigned i=0; i<used; ++i) if (memory[i].address==address) { memory[i].value=value; return; }
    if (used == 256) exit(2);
    memory[used].address=address; memory[used++].value=value;
}
static uint32_t read32(uint32_t address) {
    ++reads;
    for (unsigned i=0; i<used; ++i) if (memory[i].address==address) return memory[i].value;
    fprintf(stderr,"Unexpected guest read at %08X\n",address); ++failures; return UINT32_MAX;
}
#define CIV3_CULTURE_READ32(address) read32(address)
#include "culture_start_fix.h"
#define SP 0x20000000u
#define CHOICES 0x30000000u
static void reset(uint32_t human, uint32_t last) {
    used=reads=0;
    put(0x9E65B4u,32); put(SP+0xD8u,last); put(0xA1FCBCu,human);
    put(SP+0xE0u,12); put(SP+0xDCu,CHOICES);
    for(uint32_t i=0;i<30;++i) put(CHOICES+i*4u,i+1u);
}
static void check(const char *label,int ok) { if(!ok){fprintf(stderr,"FAIL: %s\n",label);++failures;} }
int main(void) {
    reset(1,7);
    check("valid stored race unchanged",civ3_generation_race(SP,2,8,0)==8);
    check("valid stored race short circuits",reads==1);
    check("human pending identity",civ3_generation_race(SP,1,UINT32_MAX,0)==12);
    check("first AI follows human",civ3_generation_race(SP,2,UINT32_MAX,0)==1);
    check("last active AI",civ3_generation_race(SP,7,UINT32_MAX,0)==6);
    /* Exercise all 31 human positions and all non-human opponent slots. */
    for(uint32_t human=1;human<=31;++human) {
        reset(human,31);
        for(uint32_t player=1;player<=31;++player) {
            uint32_t expected=player==human ? 12 : player-(player>human?1u:0u);
            check("opponent vector excludes human/barbarian",civ3_generation_race(SP,player,UINT32_MAX,0)==expected);
        }
    }
    reset(1,7);
    put(0x9321F4u+2*0x644u,23);
    check("scenario uses pending scenario slot",civ3_generation_race(SP,2,UINT32_MAX,1)==23);
    put(0x9321F4u+2*0x644u,32);
    check("scenario random sentinel skipped",civ3_generation_race(SP,2,UINT32_MAX,1)==UINT32_MAX);
    put(CHOICES,UINT32_MAX);
    check("unassigned opponent skipped",civ3_generation_race(SP,2,UINT32_MAX,0)==UINT32_MAX);
    put(CHOICES,32);
    check("random opponent sentinel skipped",civ3_generation_race(SP,2,UINT32_MAX,0)==UINT32_MAX);
    check("barbarian slot not mapped to opponent vector",civ3_generation_race(SP,0,UINT32_MAX,0)==UINT32_MAX);
    check("inactive slot not read",civ3_generation_race(SP,8,UINT32_MAX,0)==UINT32_MAX);
    check("invalid slot not read",civ3_generation_race(SP,UINT32_MAX,UINT32_MAX,0)==UINT32_MAX);
    put(SP+0xDCu,0);
    check("null choices rejected",civ3_generation_race(SP,2,UINT32_MAX,0)==UINT32_MAX);
    put(SP+0xDCu,UINT32_MAX-2);
    check("overflowing choices rejected",civ3_generation_race(SP,2,UINT32_MAX,0)==UINT32_MAX);
    put(0xA1FCBCu,0);
    check("invalid human rejected",civ3_generation_race(SP,2,UINT32_MAX,0)==UINT32_MAX);
    reset(1,32);
    check("oversized player count rejected",civ3_generation_race(SP,2,UINT32_MAX,0)==UINT32_MAX);
    if(!failures) puts("PASS: pending race lookup, all 961 player/human combinations, scenario path and invalid-index guards");
    return failures?1:0;
}
