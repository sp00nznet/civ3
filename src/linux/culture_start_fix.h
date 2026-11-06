/* Startup compatibility for the supported Steam image only.
 * Leaders are initialized after civilization selection. During selection,
 * compare pending choices, not the still-unassigned Leader.RaceID (-1).
 * This helper is read-only; it neither chooses a race nor alters RNG state.
 */
#ifndef CIV3_CULTURE_START_FIX_H
#define CIV3_CULTURE_START_FIX_H
#include <stdint.h>
#ifndef CIV3_CULTURE_READ32
#define CIV3_CULTURE_READ32(address) MEM32(address)
#endif

static inline uint32_t civ3_generation_race(uint32_t stack, uint32_t player,
                                           uint32_t current, int scenario) {
    uint32_t count = CIV3_CULTURE_READ32(0x009E65B4u);
    if (current < count) return current; /* Preserve already-valid identities. */
    uint32_t last = CIV3_CULTURE_READ32(stack + 0xD8u);
    if (!player || player > 31u || last > 31u || player > last) return UINT32_MAX;
    uint32_t race;
    if (scenario) {
        race = CIV3_CULTURE_READ32(0x009321F4u + player * 0x644u);
    } else {
        uint32_t human = CIV3_CULTURE_READ32(0x00A1FCBCu);
        if (!human || human > last) return UINT32_MAX;
        if (player == human) {
            race = CIV3_CULTURE_READ32(stack + 0xE0u);
        } else {
            /* The opponent vector omits the barbarian and human slots. */
            uint32_t index = player - 1u - (player > human ? 1u : 0u);
            uint32_t choices = CIV3_CULTURE_READ32(stack + 0xDCu);
            if (!choices || choices > UINT32_MAX - index * 4u - 3u) return UINT32_MAX;
            race = CIV3_CULTURE_READ32(choices + index * 4u);
        }
    }
    /* Random/unassigned sentinels are not valid indices into the rules. */
    return race < count ? race : UINT32_MAX;
}
#endif
