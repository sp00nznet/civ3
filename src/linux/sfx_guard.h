/* Mac native host: attenuate sustained corrupted software-mixed audio.
 * Applies only to the legacy software-effects ring. Unlike the MP3 stream,
 * some in-game WAV music shares this ring, so NEVER fade it all the way to
 * silence. This is a fail-safe, not a repair of guest mixing arithmetic.
 *
 * No I/O, allocation or blocking: safe inside SDL's realtime audio callback.
 */
#ifndef CIV3_SFX_GUARD_H
#define CIV3_SFX_GUARD_H

#include <stdint.h>

typedef struct {
    unsigned hot_ms, quiet_ms;
    int muted;
    float gain;
} civ3_sfx_guard;

/* Observe ONE callback's raw effects PCM, normalized to [-1, 1].
 * Return nonzero only when mute status changes. The gain is applied by the
 * caller to that buffer ONLY and is smoothed to avoid audible pops.
 */
static inline int civ3_sfx_guard_step(civ3_sfx_guard *s, float rms,
                                      float clipped_fraction,
                                      unsigned frames, unsigned sample_rate)
{
    if (!s || !frames || !sample_rate) return 0;
    unsigned ms = (unsigned)(((uint64_t)frames * 1000u + sample_rate - 1u) / sample_rate);
    if (!ms) ms = 1;
    if (ms > 1000u) ms = 1000u;

    const int hot = rms > 0.40f && clipped_fraction > 0.025f;
    const int quiet = rms < 0.17f && clipped_fraction < 0.002f;
    int changed = 0;
    if (hot) {
        s->hot_ms = s->hot_ms >= 2000u - ms ? 2000u : s->hot_ms + ms;
        s->quiet_ms = 0;
    } else if (quiet) {
        s->hot_ms = 0;
        s->quiet_ms = s->quiet_ms >= 5000u - ms ? 5000u : s->quiet_ms + ms;
    } else {
        /* Intermittent low-level effects cannot keep an old failure armed. */
        if (s->hot_ms > ms) s->hot_ms -= ms; else s->hot_ms = 0;
        s->quiet_ms = 0;
    }
    if (!s->muted && s->hot_ms >= 750u) {
        s->muted = 1;
        changed = 1;
    } else if (s->muted && s->quiet_ms >= 2700u) {
        s->muted = 0;
        s->hot_ms = 0;
        changed = -1;
    }

    /* ~100 ms fade-out and ~200 ms recovery at 44.1 kHz.
     * The guard does not change guest bytes or underlying cursor timing. */
    const float increment = (float)ms / (s->muted ? 100.0f : 200.0f);
    if (s->muted) {
        /* Retain a low-volume path for music carried by the same legacy
         * SFX ring. Total mute also mutes gameplay music for Civ3. */
        const float floor = 0.15f;
        s->gain = s->gain > floor + increment ? s->gain - increment : floor;
    } else {
        s->gain = s->gain + increment < 1.0f ? s->gain + increment : 1.0f;
    }
    return changed;
}

#endif
