#ifndef CIV3_WAV_PCM_RATE_H
#define CIV3_WAV_PCM_RATE_H

#include <limits.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>

enum civ3_wav_rate_result {
    CIV3_WAV_RATE_ERROR = -1,
    CIV3_WAV_RATE_UNSUPPORTED = 0,
    CIV3_WAV_RATE_UNCHANGED = 1,
    CIV3_WAV_RATE_CONVERTED = 2
};

struct civ3_wav_rate_image { uint8_t *bytes; size_t length; };
struct civ3_wav_rate_format {
    size_t riff_end, format, data;
    uint32_t data_bytes, rate, channels, bits, block;
    int positioned_metadata;
};

static inline uint16_t civ3_rate_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}
static inline uint32_t civ3_rate_u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline void civ3_rate_put32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8 * i));
}

static inline int civ3_rate_list_safe(const uint8_t *chunk, size_t bytes) {
    if (bytes < 4) return -1;
    if (!memcmp(chunk, "wavl", 4)) return 0;
    if (memcmp(chunk, "adtl", 4)) return 1;
    for (size_t p = 4; p < bytes;) {
        if (bytes - p < 8) return -1;
        uint32_t n = civ3_rate_u32(chunk + p + 4);
        if (n > bytes - p - 8) return -1;
        if (!memcmp(chunk + p, "ltxt", 4)) {
            if (n < 20) return -1;
        }
        size_t span = 8u + (size_t)n + (n & 1u);
        if (span > bytes - p) return -1;
        p += span;
    }
    return 1;
}

static inline int civ3_rate_parse(const uint8_t *input, size_t length,
                                  struct civ3_wav_rate_format *wav) {
    memset(wav, 0, sizeof *wav);
    if (!input || length < 12 || length > UINT32_MAX ||
        memcmp(input, "RIFF", 4) || memcmp(input + 8, "WAVE", 4)) return 0;
    uint64_t end = (uint64_t)civ3_rate_u32(input + 4) + 8u;
    if (end < 12 || end > length) return 0;
    wav->riff_end = (size_t)end;
    for (size_t p = 12; p < wav->riff_end;) {
        if (wav->riff_end - p < 8) return 0;
        uint32_t n = civ3_rate_u32(input + p + 4);
        if (n > wav->riff_end - p - 8) return 0;
        const uint8_t *data = input + p + 8;
        if (!memcmp(input + p, "fmt ", 4)) {
            if (wav->format || n < 16 || civ3_rate_u16(data) != 1) return 0;
            wav->format = p + 8;
            wav->channels = civ3_rate_u16(data + 2);
            wav->rate = civ3_rate_u32(data + 4);
            wav->block = civ3_rate_u16(data + 12);
            wav->bits = civ3_rate_u16(data + 14);
            if ((wav->channels != 1 && wav->channels != 2) ||
                (wav->bits != 8 && wav->bits != 16) ||
                wav->block != wav->channels * (wav->bits / 8) ||
                !wav->rate || wav->rate > 384000 ||
                civ3_rate_u32(data + 8) != wav->rate * wav->block ||
                (n != 16 && (n < 18 || civ3_rate_u16(data + 16) > n - 18))) return 0;
        } else if (!memcmp(input + p, "data", 4)) {
            if (wav->data) return 0;
            wav->data = p + 8;
            wav->data_bytes = n;
        } else if (!memcmp(input + p, "cue ", 4)) {
            if (n < 4) return 0;
            uint32_t count = civ3_rate_u32(data);
            if (count > (n - 4) / 24) return 0;
            for (uint32_t i = 0; i < count; ++i) {
                const uint8_t *cue = data + 4 + (size_t)i * 24;
                if (civ3_rate_u32(cue + 4) || memcmp(cue + 8, "data", 4) ||
                    civ3_rate_u32(cue + 12) || civ3_rate_u32(cue + 16) ||
                    civ3_rate_u32(cue + 20)) wav->positioned_metadata = 1;
            }
        } else if (!memcmp(input + p, "smpl", 4)) {
            if (n < 36) return 0;
            uint32_t loops = civ3_rate_u32(data + 28);
            if (loops > (n - 36) / 24 ||
                civ3_rate_u32(data + 32) > n - 36 - (size_t)loops * 24) return 0;
            if (loops || civ3_rate_u32(data + 32)) wav->positioned_metadata = 1;
        } else if (!memcmp(input + p, "LIST", 4)) {
            int safe = civ3_rate_list_safe(data, n);
            if (safe < 0) return 0;
            if (!safe) wav->positioned_metadata = 1;
        } else if (!memcmp(input + p, "fact", 4) || !memcmp(input + p, "plst", 4) ||
                   !memcmp(input + p, "bext", 4) || !memcmp(input + p, "PEAK", 4) ||
                   !memcmp(input + p, "wsmp", 4) || !memcmp(input + p, "strc", 4)) {
            wav->positioned_metadata = 1;
        }
        size_t span = 8u + (size_t)n + (n & 1u);
        if (span > wav->riff_end - p) return 0;
        p += span;
    }
    return wav->format && wav->data && wav->data_bytes % wav->block == 0;
}

static inline int civ3_rate_compare_id(const void *left, const void *right) {
    uint32_t a = *(const uint32_t *)left, b = *(const uint32_t *)right;
    return (a > b) - (a < b);
}

static inline int civ3_rate_regions(const uint8_t *input,
                                    const struct civ3_wav_rate_format *wav) {
    size_t cues = 0;
    int regions = 0;
    for (size_t p = 12; p < wav->riff_end;) {
        uint32_t n = civ3_rate_u32(input + p + 4);
        if (!memcmp(input + p, "cue ", 4)) cues += civ3_rate_u32(input + p + 8);
        if (!memcmp(input + p, "LIST", 4) && !memcmp(input + p + 8, "adtl", 4))
            for (size_t sub = p + 12; sub < p + 8 + n;) {
                uint32_t bytes = civ3_rate_u32(input + sub + 4);
                if (!memcmp(input + sub, "ltxt", 4) && civ3_rate_u32(input + sub + 12)) regions = 1;
                sub += 8u + (size_t)bytes + (bytes & 1u);
            }
        p += 8u + (size_t)n + (n & 1u);
    }
    if (!regions) return 1;
    if (!cues || cues > SIZE_MAX / sizeof(uint32_t)) return 0;
    uint32_t *ids = (uint32_t *)malloc(cues * sizeof *ids);
    if (!ids) return -1;
    size_t used = 0;
    for (size_t p = 12; p < wav->riff_end;) {
        uint32_t n = civ3_rate_u32(input + p + 4);
        if (!memcmp(input + p, "cue ", 4))
            for (uint32_t i = 0, count = civ3_rate_u32(input + p + 8); i < count; ++i)
                ids[used++] = civ3_rate_u32(input + p + 12 + (size_t)i * 24);
        p += 8u + (size_t)n + (n & 1u);
    }
    qsort(ids, cues, sizeof *ids, civ3_rate_compare_id);
    int safe = 1;
    for (size_t p = 12; p < wav->riff_end && safe;) {
        uint32_t n = civ3_rate_u32(input + p + 4);
        if (!memcmp(input + p, "LIST", 4) && !memcmp(input + p + 8, "adtl", 4))
            for (size_t sub = p + 12; sub < p + 8 + n;) {
                uint32_t bytes = civ3_rate_u32(input + sub + 4);
                if (!memcmp(input + sub, "ltxt", 4)) {
                    uint32_t id = civ3_rate_u32(input + sub + 8), frames = civ3_rate_u32(input + sub + 12);
                    if (frames > wav->data_bytes / wav->block ||
                        (frames && !bsearch(&id, ids, cues, sizeof *ids, civ3_rate_compare_id))) safe = 0;
                }
                sub += 8u + (size_t)bytes + (bytes & 1u);
            }
        p += 8u + (size_t)n + (n & 1u);
    }
    free(ids);
    return safe;
}

/* CONVERTED transfers a malloc-owned image to the caller. Other results leave
 * output empty. Input bytes, including metadata and trailing data, never change.
 * UNSUPPORTED includes malformed input and sample-position metadata we cannot
 * preserve. ERROR reports allocation/resampler failure or a missing output. */
static inline enum civ3_wav_rate_result civ3_wav_resample_pcm(
        const uint8_t *input, size_t length, uint32_t target_rate,
        struct civ3_wav_rate_image *output) {
    struct civ3_wav_rate_format wav;
    if (!output) return CIV3_WAV_RATE_ERROR;
    output->bytes = NULL;
    output->length = 0;
    if (!target_rate || target_rate > 384000 || !civ3_rate_parse(input, length, &wav))
        return CIV3_WAV_RATE_UNSUPPORTED;
    if (target_rate == wav.rate) return CIV3_WAV_RATE_UNCHANGED;
    if (wav.positioned_metadata || !wav.data_bytes || wav.data_bytes / wav.block > INT_MAX)
        return CIV3_WAV_RATE_UNSUPPORTED;
    int region_status = civ3_rate_regions(input, &wav);
    if (region_status <= 0)
        return region_status < 0 ? CIV3_WAV_RATE_ERROR : CIV3_WAV_RATE_UNSUPPORTED;
    int frames = (int)(wav.data_bytes / wav.block);
    uint64_t expected = ((uint64_t)(unsigned)frames * target_rate + wav.rate - 1u) / wav.rate;
    if (expected > INT_MAX || expected * wav.block > UINT32_MAX - (length - wav.data_bytes))
        return CIV3_WAV_RATE_UNSUPPORTED;

    SwrContext *resampler = NULL;
    AVChannelLayout layout;
    av_channel_layout_default(&layout, (int)wav.channels);
    enum AVSampleFormat format = wav.bits == 16 ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_U8;
    int rc = swr_alloc_set_opts2(&resampler, &layout, format, (int)target_rate,
                               &layout, format, (int)wav.rate, 0, NULL);
    av_channel_layout_uninit(&layout);
    if (rc < 0 || swr_init(resampler) < 0) {
        swr_free(&resampler);
        return CIV3_WAV_RATE_ERROR;
    }
    int capacity = swr_get_out_samples(resampler, frames);
    if (capacity <= 0 || (uint64_t)(unsigned)capacity * wav.block > SIZE_MAX) {
        swr_free(&resampler);
        return CIV3_WAV_RATE_ERROR;
    }
    uint8_t *pcm = (uint8_t *)malloc((size_t)capacity * wav.block);
    if (!pcm) { swr_free(&resampler); return CIV3_WAV_RATE_ERROR; }
    const uint8_t *source[1] = {input + wav.data};
    uint8_t *dest[1] = {pcm};
    int used = swr_convert(resampler, dest, capacity, source, frames);
    if (used >= 0) {
        while (used < capacity) {
            dest[0] = pcm + (size_t)used * wav.block;
            rc = swr_convert(resampler, dest, capacity - used, NULL, 0);
            if (rc <= 0) break;
            used += rc;
        }
    }
    uint64_t floor_frames = (uint64_t)(unsigned)frames * target_rate / wav.rate;
    if (used >= 0 && rc >= 0 && (uint64_t)used < floor_frames) {
        uint64_t lookahead = (((uint64_t)wav.rate + target_rate - 1u) / target_rate) * 64u;
        rc = swr_inject_silence(resampler, (int)lookahead);
        if (rc >= 0) {
            dest[0] = pcm + (size_t)used * wav.block;
            int remaining = (int)expected - used;
            if (remaining > capacity - used) remaining = capacity - used;
            rc = swr_convert(resampler, dest, remaining, NULL, 0);
            if (rc > 0) used += rc;
        }
    }
    swr_free(&resampler);
    if (used < 0 || rc < 0 || used == capacity || (uint64_t)used < floor_frames ||
        (uint64_t)used > expected) {
        free(pcm);
        return CIV3_WAV_RATE_ERROR;
    }
    size_t bytes = (size_t)used * wav.block;
    size_t old_span = (size_t)wav.data_bytes + (wav.data_bytes & 1u);
    size_t new_span = bytes + (bytes & 1u);
    size_t new_length = length - old_span + new_span;
    if (new_length > UINT32_MAX) { free(pcm); return CIV3_WAV_RATE_ERROR; }
    uint8_t *converted = (uint8_t *)malloc(new_length);
    if (!converted) { free(pcm); return CIV3_WAV_RATE_ERROR; }
    memcpy(converted, input, wav.data);
    memcpy(converted + wav.data, pcm, bytes);
    if (bytes & 1u) converted[wav.data + bytes] =
        wav.data_bytes & 1u ? input[wav.data + wav.data_bytes] : 0;
    memcpy(converted + wav.data + new_span, input + wav.data + old_span,
           length - wav.data - old_span);
    free(pcm);
    civ3_rate_put32(converted + wav.data - 4, (uint32_t)bytes);
    civ3_rate_put32(converted + 4, (uint32_t)(wav.riff_end - old_span + new_span - 8));
    size_t fmt = wav.format > wav.data ? wav.format - old_span + new_span : wav.format;
    civ3_rate_put32(converted + fmt + 4, target_rate);
    civ3_rate_put32(converted + fmt + 8, target_rate * wav.block);
    for (size_t p = 12; p < wav.riff_end;) {
        uint32_t n = civ3_rate_u32(input + p + 4);
        if (!memcmp(input + p, "smpl", 4)) {
            size_t at = p > wav.data ? p - old_span + new_span : p;
            civ3_rate_put32(converted + at + 16, 1000000000u / target_rate);
        } else if (!memcmp(input + p, "LIST", 4) && !memcmp(input + p + 8, "adtl", 4)) {
            for (size_t sub = p + 12; sub < p + 8 + n;) {
                uint32_t sub_bytes = civ3_rate_u32(input + sub + 4);
                if (!memcmp(input + sub, "ltxt", 4)) {
                    uint32_t count = civ3_rate_u32(input + sub + 12);
                    size_t at = sub > wav.data ? sub - old_span + new_span : sub;
                    uint64_t scaled = ((uint64_t)count * target_rate + wav.rate - 1u) / wav.rate;
                    if (scaled > (unsigned)used) scaled = (unsigned)used;
                    civ3_rate_put32(converted + at + 12, (uint32_t)scaled);
                }
                sub += 8u + (size_t)sub_bytes + (sub_bytes & 1u);
            }
        }
        p += 8u + (size_t)n + (n & 1u);
    }
    output->bytes = converted;
    output->length = new_length;
    return CIV3_WAV_RATE_CONVERTED;
}
#endif
