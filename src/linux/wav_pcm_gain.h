/* Defensive, in-memory PCM gain reduction for legacy Civ III WAV mixing.
 * Never modifies the user's actual asset, WAV metadata, or non-PCM files.
 * The game's legacy effects mixer accumulates otherwise-valid voices until it
 * overflows/clips. Gain reduction before that accumulation preserves music/SFX.
 */
#ifndef CIV3_WAV_PCM_GAIN_H
#define CIV3_WAV_PCM_GAIN_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

static inline uint16_t civ3_wav_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}
static inline uint32_t civ3_wav_u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Return processed PCM samples, 0 for an unsupported/malformed WAV.
 * divisor 20 means 5% input gain, before the software mixer's accumulation.
 * Accept only uncompressed 8-/16-bit mono/stereo PCM and valid RIFF bounds.
 */
static inline size_t civ3_wav_reduce_pcm(uint8_t *file, size_t len,
                                         unsigned divisor) {
    if (!file || divisor <= 1u || len < 36u ||
        memcmp(file, "RIFF", 4) || memcmp(file + 8, "WAVE", 4)) return 0;
    uint64_t riff_end64 = (uint64_t)civ3_wav_u32(file + 4) + 8u;
    if (riff_end64 > len || riff_end64 < 36u) return 0;
    size_t end = (size_t)riff_end64;
    size_t p = 12;
    unsigned bits = 0, channels = 0, block = 0;
    /* Validate every chunk *before* modifying any bytes. */
    while (p + 8 <= end) {
        uint32_t n = civ3_wav_u32(file + p + 4);
        if ((uint64_t)n > end - p - 8) return 0;
        if (!memcmp(file + p, "fmt ", 4) && n >= 16) {
            if (civ3_wav_u16(file + p + 8) != 1) return 0;
            channels = civ3_wav_u16(file + p + 10);
            bits = civ3_wav_u16(file + p + 22);
            block = civ3_wav_u16(file + p + 20);
            if ((channels != 1 && channels != 2) ||
                (bits != 8 && bits != 16) || block != channels * bits / 8)
                return 0;
        }
        size_t advance = 8u + (size_t)n + ((size_t)n & 1u);
        if (advance > end - p) {
            if ((size_t)n + 8u == end - p) p = end; /* missing final odd pad */
            else return 0;
        } else p += advance;
    }
    if (!bits || !channels || !block) return 0;
    size_t samples = 0;
    for (p = 12; p + 8 <= end;) {
        uint32_t n = civ3_wav_u32(file + p + 4);
        if (!memcmp(file + p, "data", 4)) {
            uint8_t *samples_bytes = file + p + 8;
            size_t bytes = (size_t)n;
            if (bits == 16) {
                for (size_t i = 0; i + 1 < bytes; i += 2) {
                    uint16_t encoded = civ3_wav_u16(samples_bytes + i);
                    int16_t v = (int16_t)encoded;
                    int16_t reduced = (int16_t)(v / (int)divisor);
                    uint16_t u = (uint16_t)reduced;
                    samples_bytes[i] = (uint8_t)u;
                    samples_bytes[i + 1] = (uint8_t)(u >> 8);
                    ++samples;
                }
            } else {
                for (size_t i = 0; i < bytes; ++i) {
                    int v = (int)samples_bytes[i] - 128;
                    samples_bytes[i] = (uint8_t)(128 + v / (int)divisor);
                    ++samples;
                }
            }
        }
        size_t step = 8u + (size_t)n + ((size_t)n & 1u);
        if (step > end - p) break; /* final odd length without pad */
        p += step;
    }
    return samples;
}
#endif
