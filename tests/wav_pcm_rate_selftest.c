#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wav_pcm_rate.h"

static int failures;
static void check(const char *name, int okay) {
    if (!okay) { fprintf(stderr, "FAIL: %s\n", name); ++failures; }
}
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8 * i));
}
struct fixture { uint8_t *bytes; size_t length, capacity; };
static void chunk(struct fixture *f, const char *id, const void *bytes, uint32_t n) {
    size_t at = f->length;
    if (at + n + 9 > f->capacity) abort();
    memcpy(f->bytes + at, id, 4);
    put32(f->bytes + at + 4, n);
    if (n) memcpy(f->bytes + at + 8, bytes, n);
    if (n & 1) f->bytes[at + 8 + n] = 0xA5;
    f->length += 8u + n + (n & 1u);
    put32(f->bytes + 4, (uint32_t)(f->length - 8));
}
static uint8_t *find_chunk(const struct fixture *f, const char *id) {
    size_t end = (size_t)u32(f->bytes + 4) + 8;
    for (size_t p = 12; p + 8 <= end; p += 8u + u32(f->bytes + p + 4) + (u32(f->bytes + p + 4) & 1))
        if (!memcmp(f->bytes + p, id, 4)) return f->bytes + p;
    return NULL;
}
static void metadata(struct fixture *f, uint32_t rate, uint32_t frames, unsigned flags) {
    uint8_t cue[52] = {0}, sampler[60] = {0}, list[64] = {0};
    uint32_t cue_count = flags & 32 ? 2 : 1;
    put32(cue, cue_count); put32(cue + 4, 7); memcpy(cue + 12, "data", 4);
    if (cue_count == 2) { put32(cue + 28, 3); memcpy(cue + 36, "data", 4); }
    put32(sampler + 8, 1000000000u / rate);
    put32(sampler + 12, 60);
    if (flags & 4) put32(sampler + 28, 1);
    memcpy(list, "adtl", 4);
    size_t at = 4;
    if (flags & 16) {
        memcpy(list + at, "ltxt", 4); put32(list + at + 4, 20);
        put32(list + at + 8, 7); put32(list + at + 12, frames);
        memcpy(list + at + 16, "rgn ", 4); at += 28;
    }
    memcpy(list + at, "labl", 4); put32(list + at + 4, 9);
    put32(list + at + 8, 7); memcpy(list + at + 12, "mark", 5); list[at + 17] = 0x6B;
    chunk(f, "JUNK", "abc", 3);
    chunk(f, "cue ", cue, 4 + cue_count * 24);
    chunk(f, "LIST", list, (uint32_t)at + 18);
    chunk(f, "smpl", sampler, flags & 4 ? 60 : 36);
}
static struct fixture make_wave(uint32_t rate, unsigned channels, unsigned bits,
                                uint32_t frames, double left, double right, unsigned flags) {
    unsigned block = channels * (bits / 8);
    struct fixture f = {calloc(1, 1024u + (size_t)frames * block), 12,
                        1024u + (size_t)frames * block};
    if (!f.bytes) abort();
    memcpy(f.bytes, "RIFF", 4); memcpy(f.bytes + 8, "WAVE", 4);
    uint8_t fmt[18] = {1, 0}; fmt[2] = (uint8_t)channels;
    put32(fmt + 4, rate); put32(fmt + 8, rate * block);
    fmt[12] = (uint8_t)block; fmt[14] = (uint8_t)bits;
    uint8_t *pcm = calloc(1, (size_t)frames * block + 1);
    if (!pcm) abort();
    for (uint32_t i = 0; i < frames; ++i) for (unsigned c = 0; c < channels; ++c) {
        double value = sin(6.283185307179586 * (c ? right : left) * i / rate);
        size_t at = ((size_t)i * channels + c) * (bits / 8);
        if (bits == 16) {
            int16_t encoded = (int16_t)(4096 * value);
            pcm[at] = (uint8_t)encoded; pcm[at + 1] = (uint8_t)((uint16_t)encoded >> 8);
        } else pcm[at] = (uint8_t)(128 + (int)(32 * value));
    }
    if (!(flags & 2)) chunk(&f, "fmt ", fmt, sizeof fmt);
    if (flags & 1) metadata(&f, rate, frames, flags);
    chunk(&f, "data", pcm, frames * block);
    if (flags & 2) chunk(&f, "fmt ", fmt, sizeof fmt);
    if (flags & 8) metadata(&f, rate, frames, flags);
    memcpy(f.bytes + f.length, "TAIL", 4); f.length += 4;
    free(pcm);
    return f;
}
static enum civ3_wav_rate_result convert(const struct fixture *input, uint32_t rate,
                                         struct fixture *output) {
    uint8_t *copy = malloc(input->length);
    if (!copy) abort();
    memcpy(copy, input->bytes, input->length);
    struct civ3_wav_rate_image image = {(uint8_t *)1, 99};
    enum civ3_wav_rate_result result = civ3_wav_resample_pcm(input->bytes, input->length, rate, &image);
    check("conversion leaves all input bytes unchanged", !memcmp(copy, input->bytes, input->length));
    free(copy);
    if (result != CIV3_WAV_RATE_CONVERTED)
        check("non-conversion leaves no output allocation", !image.bytes && !image.length);
    *output = (struct fixture){image.bytes, image.length, image.length};
    return result;
}
static double sample(const uint8_t *pcm, size_t frame, unsigned channels, unsigned bits, unsigned channel) {
    size_t at = (frame * channels + channel) * (bits / 8);
    if (bits == 8) return ((int)pcm[at] - 128) * 256.0;
    uint16_t value = (uint16_t)(pcm[at] | (uint16_t)pcm[at + 1] << 8);
    return (int16_t)value;
}
static double rms(const struct fixture *f, unsigned channel) {
    uint8_t *fmt = find_chunk(f, "fmt ") + 8, *data = find_chunk(f, "data") + 8;
    unsigned rate = u32(fmt + 4), channels = fmt[2], bits = fmt[14];
    size_t count = u32(data - 4) / (channels * (bits / 8));
    size_t first = count / 4, end = count * 3 / 4;
    (void)rate;
    double energy = 0;
    for (size_t i = first; i < end; ++i) { double x = sample(data, i, channels, bits, channel); energy += x * x; }
    return sqrt(energy / (end - first));
}
static void check_tone(const struct fixture *f, unsigned channel, double hz) {
    uint8_t *fmt = find_chunk(f, "fmt ") + 8, *data = find_chunk(f, "data") + 8;
    unsigned rate = u32(fmt + 4), channels = fmt[2], bits = fmt[14];
    size_t frames = u32(data - 4) / (channels * (bits / 8));
    size_t first = frames / 4, end = frames * 3 / 4, crossings = 0;
    for (size_t i = first + 1; i < end; ++i)
        crossings += sample(data, i - 1, channels, bits, channel) <= 0 && sample(data, i, channels, bits, channel) > 0;
    double measured = crossings * (double)rate / (end - first);
    check("tone retains its frequency", fabs(measured - hz) < 5);
    check("tone retains nonzero PCM energy", rms(f, channel) > 2000);
}
static void tones_and_duration(void) {
    for (unsigned bits = 8; bits <= 16; bits += 8) for (unsigned channels = 1; channels <= 2; ++channels) {
        struct fixture input = make_wave(44100, channels, bits, 44101, 1000, 2000, 0), out;
        check("resample mono/stereo 8-/16-bit PCM", convert(&input, 22050, &out) == CIV3_WAV_RATE_CONVERTED);
        if (out.bytes) {
            uint8_t *fmt = find_chunk(&out, "fmt ") + 8, *data = find_chunk(&out, "data");
            uint32_t frames = u32(data + 4) / (channels * (bits / 8));
            check("odd frame count preserves duration within one output sample", frames >= 22050 && frames <= 22051);
            check("target rate and byte rate agree", u32(fmt + 4) == 22050 && u32(fmt + 8) == 22050 * channels * (bits / 8));
            check_tone(&out, 0, 1000);
            if (channels == 2) check_tone(&out, 1, 2000);
            free(out.bytes);
        }
        free(input.bytes);
    }
    struct fixture input = make_wave(22050, 2, 16, 22051, 1000, 2000, 0), out;
    check("upsample to a higher mixer rate", convert(&input, 44100, &out) == CIV3_WAV_RATE_CONVERTED);
    if (out.bytes) {
        check("upsampling flushes every output frame", u32(find_chunk(&out, "data") + 4) == 44102 * 4);
        check_tone(&out, 0, 1000); check_tone(&out, 1, 2000); free(out.bytes);
    }
    check("already-target WAV remains byte-identical", convert(&input, 22050, &out) == CIV3_WAV_RATE_UNCHANGED);
    free(input.bytes);
}
static void filter_aliases(void) {
    struct fixture low = make_wave(44100, 1, 16, 44100, 1000, 1000, 0);
    struct fixture high = make_wave(44100, 1, 16, 44100, 13000, 13000, 0), lo, hi;
    check("convert in-band reference", convert(&low, 22050, &lo) == CIV3_WAV_RATE_CONVERTED);
    check("convert above-Nyquist reference", convert(&high, 22050, &hi) == CIV3_WAV_RATE_CONVERTED);
    if (lo.bytes && hi.bytes) check("downsampling rejects aliases by at least 40 dB", rms(&hi, 0) < rms(&lo, 0) * .01);
    free(low.bytes); free(high.bytes); free(lo.bytes); free(hi.bytes);
}
static void preserve_metadata(void) {
    const unsigned cases[] = {17, 26, 49, 58};
    for (unsigned c = 0; c < sizeof cases / sizeof *cases; ++c) {
        unsigned flags = cases[c];
        struct fixture input = make_wave(44100, 1, 8, 44101, 1000, 1000, flags), out;
        check("convert WAV with metadata around data", convert(&input, 22050, &out) == CIV3_WAV_RATE_CONVERTED);
        if (out.bytes) {
            for (unsigned i = 0; i < 2; ++i) {
                const char *ids[] = {"cue ", "JUNK"};
                uint8_t *before = find_chunk(&input, ids[i]), *after = find_chunk(&out, ids[i]);
                size_t n = u32(before + 4);
                check("preserve opaque chunks and their padding", !memcmp(before, after, 8u + n + (n & 1u)));
            }
            uint8_t *smpl = find_chunk(&out, "smpl"), *old = find_chunk(&input, "smpl");
            check("sampler period follows target rate", u32(smpl + 16) == 1000000000u / 22050);
            check("other sampler metadata remains intact", !memcmp(old, smpl, 16) && !memcmp(old + 20, smpl + 20, 24));
            uint8_t *list = find_chunk(&out, "LIST"), *old_list = find_chunk(&input, "LIST");
            uint32_t output_frames = u32(find_chunk(&out, "data") + 4);
            check("region length follows converted frame count", u32(list + 24) == output_frames);
            check("region ID, purpose and labels remain intact", !memcmp(old_list, list, 24) &&
                  !memcmp(old_list + 28, list + 28, u32(old_list + 4) + 8 - 28));
            check("RIFF size excludes preserved trailing bytes", (size_t)u32(out.bytes + 4) + 8 == out.length - 4);
            check("preserve trailing bytes", !memcmp(out.bytes + out.length - 4, "TAIL", 4));
            uint8_t *data = find_chunk(&out, "data");
            check("preserve an existing odd PCM padding byte", !(u32(data + 4) & 1) || data[8 + u32(data + 4)] == 0xA5);
            free(out.bytes);
        }
        free(input.bytes);
    }
}
static void rejects_invalid_and_positioned(void) {
    struct fixture input = make_wave(44100, 2, 16, 1001, 1000, 2000, 1), out;
    uint8_t *copy = malloc(input.capacity);
    if (!copy) abort();
    memcpy(copy, input.bytes, input.length);
    size_t original_length = input.length;
#define RESTORE() do { input.length = original_length; memcpy(input.bytes, copy, original_length); } while (0)
#define REJECT(name) check(name, convert(&input, 22050, &out) == CIV3_WAV_RATE_UNSUPPORTED)
    put32(input.bytes + 4, UINT32_MAX); REJECT("reject RIFF length beyond file"); RESTORE();
    put32(find_chunk(&input, "data") + 4, UINT32_MAX); REJECT("reject data length beyond RIFF"); RESTORE();
    put32(find_chunk(&input, "data") + 4, 3); REJECT("reject partial PCM frame"); RESTORE();
    put32(find_chunk(&input, "fmt ") + 12, 0); REJECT("reject zero source rate"); RESTORE();
    put32(find_chunk(&input, "fmt ") + 16, 1); REJECT("reject inconsistent byte rate"); RESTORE();
    find_chunk(&input, "fmt ")[8] = 3; REJECT("reject non-PCM encoding"); RESTORE();
    find_chunk(&input, "fmt ")[10] = 3; REJECT("reject unsupported channels"); RESTORE();
    find_chunk(&input, "fmt ")[22] = 24; REJECT("reject unsupported sample width"); RESTORE();
    put32(find_chunk(&input, "cue ") + 8, UINT32_MAX); REJECT("reject truncated cue records"); RESTORE();
    put32(find_chunk(&input, "cue ") + 32, 3); REJECT("reject nonzero cue frame indices during conversion"); RESTORE();
    input.length -= 4;
    uint8_t *fmt = find_chunk(&input, "fmt "); chunk(&input, "fmt ", fmt + 8, u32(fmt + 4));
    REJECT("reject duplicate format chunks"); RESTORE();
    input.length -= 4; chunk(&input, "data", "abcd", 4); REJECT("reject duplicate data chunks"); RESTORE();
    check("reject zero target rate", convert(&input, 0, &out) == CIV3_WAV_RATE_UNSUPPORTED);
    check("reject excessive target rate", convert(&input, 384001, &out) == CIV3_WAV_RATE_UNSUPPORTED);
    check("reject truncated input", civ3_wav_resample_pcm(input.bytes, 11, 22050, &(struct civ3_wav_rate_image){0}) == CIV3_WAV_RATE_UNSUPPORTED);
    check("reject missing input", civ3_wav_resample_pcm(NULL, 44, 22050, &(struct civ3_wav_rate_image){0}) == CIV3_WAV_RATE_UNSUPPORTED);
    check("report invalid output argument", civ3_wav_resample_pcm(input.bytes, input.length, 22050, NULL) == CIV3_WAV_RATE_ERROR);
    free(copy); free(input.bytes);
    input = make_wave(44100, 1, 16, 1001, 1000, 1000, 5);
    REJECT("reject sampler loops requiring position remapping");
    check("already-target loop metadata needs no rewrite", convert(&input, 44100, &out) == CIV3_WAV_RATE_UNCHANGED);
    free(input.bytes);
    input = make_wave(44100, 1, 16, 1001, 1000, 1000, 17);
    put32(find_chunk(&input, "LIST") + 24, 1002); REJECT("reject region longer than source PCM");
    put32(find_chunk(&input, "LIST") + 24, 1001);
    put32(find_chunk(&input, "LIST") + 20, 8); REJECT("reject region with unknown cue origin");
    free(input.bytes);
    input = make_wave(44100, 1, 16, 1001, 1000, 1000, 0);
    input.length -= 4; chunk(&input, "fact", "abcd", 4);
    REJECT("reject unsupported sample-index metadata");
    check("already-target indexed metadata remains byte-identical", convert(&input, 44100, &out) == CIV3_WAV_RATE_UNCHANGED);
    free(input.bytes);
#undef RESTORE
#undef REJECT
}
static void short_clips_and_truncation(void) {
    for (unsigned frames = 1; frames <= 65; ++frames) {
        struct fixture input = make_wave(44100, 1, 16, frames, 1000, 1000, 0), out;
        check("convert clips shorter than the resampling filter", convert(&input, 22050, &out) == CIV3_WAV_RATE_CONVERTED);
        if (out.bytes) {
            unsigned produced = u32(find_chunk(&out, "data") + 4) / 2;
            check("short clip keeps its declared duration", produced >= frames / 2 && produced <= (frames + 1) / 2);
            free(out.bytes);
        }
        free(input.bytes);
    }
    struct fixture input = make_wave(44100, 1, 16, 65, 1000, 1000, 17);
    size_t riff_end = (size_t)u32(input.bytes + 4) + 8;
    for (size_t n = 0; n < riff_end; ++n) {
        uint8_t *prefix = malloc(n ? n : 1);
        if (!prefix) abort();
        memcpy(prefix, input.bytes, n);
        struct civ3_wav_rate_image output;
        check("every truncated prefix is rejected", civ3_wav_resample_pcm(prefix, n, 22050, &output) == CIV3_WAV_RATE_UNSUPPORTED);
        check("truncated prefix owns no output", !output.bytes && !output.length);
        free(prefix);
    }
    free(input.bytes);
}
int main(void) {
    tones_and_duration(); filter_aliases(); preserve_metadata(); rejects_invalid_and_positioned(); short_clips_and_truncation();
    if (!failures) puts("PASS: PCM rate, duration, channel separation, alias filtering, metadata and transactional validation");
    return failures ? 1 : 0;
}
