/* Bounded allocation arithmetic for the portable bitmap implementation.
 * The 256 MiB cap is this host's policy, not a Windows API limit. */
#pragma once
#include <stdint.h>
#include <limits.h>
#define CIV3_BITMAP_MAX_BYTES (256u * 1024u * 1024u)
static inline int civ3_bitmap_layout(int32_t width, int32_t height, uint32_t bits,
                                    int *absolute_height, int *pitch, uint32_t *bytes) {
    if (width <= 0 || height == 0 || height == INT32_MIN ||
        !(bits == 1 || bits == 4 || bits == 8 || bits == 16 || bits == 24 || bits == 32)) return 0;
    uint64_t rows = height < 0 ? (uint64_t)(-(int64_t)height) : (uint64_t)height;
    uint64_t stride = (((uint64_t)(uint32_t)width * bits + 31u) / 32u) * 4u;
    uint64_t size = stride * rows;
    if (stride > INT_MAX || size > CIV3_BITMAP_MAX_BYTES - 4u) return 0;
    if (absolute_height) *absolute_height = (int)rows;
    if (pitch) *pitch = (int)stride;
    if (bytes) *bytes = (uint32_t)size;
    return 1;
}
