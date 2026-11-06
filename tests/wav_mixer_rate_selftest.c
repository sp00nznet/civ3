#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <SDL2/SDL.h>
#include "win32hle.h"
#include "host.h"
#include "wav_mixer_rate.h"

const recomp_dispatch_entry_t recomp_dispatch_table[] = {{0, NULL}};
const uint32_t recomp_dispatch_count = 0;
static int failures;
static void check(const char *name, int ok) {
    if (!ok) { ++failures; fprintf(stderr, "FAIL: %s\n", name); }
}
static uint32_t invoke(const char *name, int n, uint32_t *args) {
    uint32_t address = hle_resolve(name);
    if (!address) hle_fatal("missing shim %s", name);
    return hle_call_guest(address, n, args);
}
static void put16(uint8_t *p, uint16_t n) { p[0] = n; p[1] = n >> 8; }
static void put32(uint8_t *p, uint32_t n) {
    p[0] = n; p[1] = n >> 8; p[2] = n >> 16; p[3] = n >> 24;
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint32_t buffer(uint32_t device, uint32_t rate, uint32_t flags) {
    uint8_t *format = hle_alloc(18);
    uint32_t *desc = hle_alloc(24), *out = hle_alloc(4);
    memset(format, 0, 18); put16(format, 1); put16(format + 2, 2);
    put32(format + 4, rate); put32(format + 8, rate * 4);
    put16(format + 12, 4); put16(format + 14, 16);
    memset(desc, 0, 24); desc[0] = 24; desc[1] = flags; desc[2] = 65536; desc[4] = hle_va(format);
    uint32_t args[4] = {device, hle_va(desc), hle_va(out), 0};
    check("create native audio buffer", invoke("IDirectSound::CreateSoundBuffer", 4, args) == 0 && *out);
    uint32_t result = *out;
    hle_free(format); hle_free(desc); hle_free(out);
    return result;
}
static void play(uint32_t b) {
    uint32_t args[4] = {b, 0, 0, 1};
    check("loop native audio buffer", invoke("IDirectSoundBuffer::Play", 4, args) == 0);
}
static void release(uint32_t b) { invoke("IDirectSoundBuffer::Release", 1, &b); }
static uint32_t open_wave(uint32_t name) {
    uint32_t args[3] = {name, 0, 0};
    uint32_t h = invoke("mmioOpenA", 3, args);
    check("open synthetic WAV", h != 0);
    return h;
}
static void close_wave(uint32_t h) {
    uint32_t args[2] = {h, 0}; invoke("mmioClose", 2, args);
}
static uint8_t *image(uint32_t h, uint32_t *state) {
    uint32_t args[3] = {h, hle_va(state), 0};
    check("read WAV memory information", invoke("mmioGetInfo", 3, args) == 0);
    return hle_ptr(state[6]);
}
int main(void) {
    char directory[] = "/tmp/civ3-wav-rate-XXXXXX", path[256], guest[260];
    if (!mkdtemp(directory)) return 2;
    snprintf(path, sizeof path, "%s/fixture.wav", directory);
    size_t length = 44 + 44100 * 2;
    uint8_t *wav = calloc(1, length), *unchanged = malloc(length);
    if (!wav || !unchanged) return 2;
    memcpy(wav, "RIFF", 4); put32(wav + 4, length - 8); memcpy(wav + 8, "WAVEfmt ", 8);
    put32(wav + 16, 16); put16(wav + 20, 1); put16(wav + 22, 1);
    put32(wav + 24, 44100); put32(wav + 28, 88200); put16(wav + 32, 2); put16(wav + 34, 16);
    memcpy(wav + 36, "data", 4); put32(wav + 40, 88200);
    for (unsigned i = 0; i < 44100; ++i)
        put16(wav + 44 + 2*i, (uint16_t)(int16_t)(4096 * sin(6.283185307179586 * 1000 * i / 44100)));
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(wav, 1, length, f) != length) return 2;
    fclose(f);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    recomp_host_init(); mach_enter();
    uint32_t *state = hle_alloc(72), *device = hle_alloc(4);
    snprintf(guest, sizeof guest, "Z:%s", path);
    uint32_t name = hle_guest_strdup(guest);
    check("no mixer before effects initialization", civ3_sfx_mixer_rate() == 0);
    uint32_t h = open_wave(name);
    uint8_t *bytes = image(h, state);
    check("generic MMIO preserves file rate without an effects mixer", get32(bytes + 24) == 44100);
    check("generic MMIO preserves file bytes", state[5] == length && !memcmp(bytes, wav, length));
    close_wave(h);
    uint32_t args[3] = {0, hle_va(device), 0};
    check("create DirectSound", invoke("DirectSoundCreate", 3, args) == 0 && *device);
    uint32_t music = buffer(*device, 44100, 0x10080); play(music);
    check("music stream does not select WAV working rate", civ3_sfx_mixer_rate() == 0);
    uint32_t effects = buffer(*device, 22050, 0x10088); play(effects);
    check("effects working rate selected", civ3_sfx_mixer_rate() == 22050);
    h = open_wave(name); bytes = image(h, state);
    check("WAV rate follows effects mixer", get32(bytes + 24) == 22050);
    check("WAV duration stays one second", get32(bytes + 40) == 22050 * 2);
    unsigned crossings = 0;
    for (unsigned i = 1; i < 22050; ++i) {
        int16_t before = (int16_t)((uint16_t)bytes[44 + 2*(i-1)] | (uint16_t)bytes[45 + 2*(i-1)] << 8);
        int16_t after = (int16_t)((uint16_t)bytes[44 + 2*i] | (uint16_t)bytes[45 + 2*i] << 8);
        crossings += before <= 0 && after > 0;
    }
    check("converted WAV retains 1 kHz pitch", crossings >= 999 && crossings <= 1001);
    uint32_t set[4] = {h, 0, 32, 0}; invoke("mmioSetBuffer", 4, set);
    uint32_t seek[3] = {h, 44, 0}; invoke("mmioSeek", 3, seek);
    uint32_t advance[3] = {h, hle_va(state), 0}; invoke("mmioGetInfo", 3, advance);
    check("buffered converted PCM refills", invoke("mmioAdvance", 3, advance) == 0 && state[8] - state[7] == 32);
    check("converted window begins after RIFF header", memcmp(hle_ptr(state[7]), "RIFF", 4) != 0);
    close_wave(h);
    uint32_t frequency[2] = {effects, 44100}; invoke("IDirectSoundBuffer::SetFrequency", 2, frequency);
    check("working rate is read from current buffer state", civ3_sfx_mixer_rate() == 44100);
    h = open_wave(name); bytes = image(h, state);
    check("already matching input remains byte-identical", state[5] == length && !memcmp(bytes, wav, length));
    close_wave(h);
    uint32_t other = buffer(*device, 22050, 0x10088); play(other);
    check("ambiguous working rates do not select one arbitrarily", civ3_sfx_mixer_rate() == 0);
    release(other); check("release restores unambiguous rate", civ3_sfx_mixer_rate() == 44100);
    release(effects); release(music);
    check("released rings leave no stale rate", civ3_sfx_mixer_rate() == 0);
    invoke("IDirectSound::Release", 1, device);
    hle_free(state); hle_free(device); hle_free(hle_ptr(name)); mach_leave();
    f = fopen(path, "rb");
    check("original file still present", f != NULL);
    if (f) { check("original file unchanged", fread(unchanged, 1, length, f) == length && !memcmp(unchanged, wav, length)); fclose(f); }
    free(unchanged); free(wav); unlink(path); rmdir(directory);
    if (!failures) puts("PASS: WAV conversion follows the effects ring and preserves original files");
    return failures ? 1 : 0;
}
