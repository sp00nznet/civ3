/* Smooth saturation curve for the old software effects ring only.
 * Leaves ordinary, low-amplitude effects unchanged. Never modifies the
 * independent MP3 soundtrack. No allocations, locks or system calls.
 */
#ifndef CIV3_SFX_SOFTLIMIT_H
#define CIV3_SFX_SOFTLIMIT_H

static inline float civ3_sfx_soft_limit(float x) {
    const float threshold = 0.80f, headroom = 0.20f;
    if (x > threshold) {
        float over = x - threshold;
        return threshold + headroom * over / (headroom + over);
    }
    if (x < -threshold) {
        float over = -x - threshold;
        return -(threshold + headroom * over / (headroom + over));
    }
    return x;
}
#endif
