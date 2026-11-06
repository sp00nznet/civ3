/* Native DirectSound ownership and bounds regression.
 * Synthetic guest buffer, no game files, video or networking.
 * The before-fix runtime leaked both heap allocations and the source sample
 * block after duplicate-buffer slot exhaustion.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc/malloc.h>
#include <SDL2/SDL.h>
#include "win32hle.h"
#include "host.h"

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#include <sanitizer/allocator_interface.h>
#define CIV3_ASAN_ALLOCATOR 1
#endif
#endif

static size_t native_bytes_in_use(void) {
#ifdef CIV3_ASAN_ALLOCATOR
    /* Darwin's zone statistics include sanitizer allocator bookkeeping.
     * Ask ASan for live allocations so these remain leak assertions. */
    return __sanitizer_get_current_allocated_bytes();
#else
    malloc_statistics_t stats = {0};
    malloc_zone_statistics(malloc_default_zone(), &stats);
    return stats.size_in_use;
#endif
}

#define DS_OK 0u
#define DSERR_INVALIDPARAM 0x80070057u
#define MAX_SLOTS 256

const recomp_dispatch_entry_t recomp_dispatch_table[] = {{0, NULL}};
const uint32_t recomp_dispatch_count = 0;

static int failures;
static void check(const char *name, int ok) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", name); ++failures; }
}
static uint32_t call(const char *name, int n, const uint32_t *args) {
    uint32_t va = hle_resolve(name);
    if (!va) { fprintf(stderr, "missing shim: %s\n", name); exit(2); }
    return hle_call_guest(va, n, args);
}
static uint32_t new_source(uint32_t ds, uint32_t desc, uint32_t out) {
    uint32_t args[4] = {ds, desc, out, 0};
    return call("IDirectSound::CreateSoundBuffer", 4, args);
}
static uint32_t duplicate(uint32_t ds, uint32_t source, uint32_t out) {
    uint32_t args[3] = {ds, source, out};
    return call("IDirectSound::DuplicateSoundBuffer", 3, args);
}
static void release(uint32_t object) {
    call("IDirectSoundBuffer::Release", 1, &object);
}

int main(void) {
    /* No audible device or external asset is needed. */
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    recomp_host_init();
    mach_enter();
    uint32_t *ds_out = (uint32_t *)hle_alloc(4);
    uint32_t *out = (uint32_t *)hle_alloc(4);
    uint32_t *desc = (uint32_t *)hle_alloc(24);
    uint32_t *sample_addr = (uint32_t *)hle_alloc(4);
    uint32_t *sample_bytes = (uint32_t *)hle_alloc(4);
    check("test allocations", ds_out && out && desc && sample_addr && sample_bytes);
    if (!ds_out || !out || !desc || !sample_addr || !sample_bytes) return 2;

    uint32_t create_ds[3] = {0, hle_va(ds_out), 0};
    check("DirectSoundCreate", call("DirectSoundCreate", 3, create_ds) == DS_OK && *ds_out);
    uint32_t ds = *ds_out;
    /* The shim's DSBUFFERDESC subset uses flags at +4, bytes at +8, wfx at +16. */
    memset(desc, 0, 24);
    desc[0] = 24;
    desc[2] = 2048;
    *out = 0;
    check("create source buffer", new_source(ds, hle_va(desc), hle_va(out)) == DS_OK && *out);
    if (!*out) return 2;
    uint32_t objects[MAX_SLOTS] = {0};
    objects[0] = *out;
    uint32_t lock_args[8] = {objects[0], 0, 16, hle_va(sample_addr), hle_va(sample_bytes), 0, 0, 0};
    check("get source sample address", call("IDirectSoundBuffer::Lock", 8, lock_args) == DS_OK && *sample_addr);
    uint32_t data = *sample_addr;

    /* Exhaust every slot with legitimate duplicate handles, all sharing
     * the original sample storage and its refcount. */
    for (unsigned i = 1; i < MAX_SLOTS; ++i) {
        *out = 0;
        uint32_t result = duplicate(ds, objects[0], hle_va(out));
        check("duplicate into free slot", result == DS_OK && *out != 0);
        if (result != DS_OK || !*out) return 2;
        objects[i] = *out;
    }
    size_t before = native_bytes_in_use();
    for (unsigned i = 0; i < 4096; ++i) {
        *out = 0xABCDEF01u; /* failure must not leave a stale guest handle */
        uint32_t result = duplicate(ds, objects[0], hle_va(out));
        if (result != DSERR_INVALIDPARAM || *out) {
            if (failures < 10) fprintf(stderr, "FAIL: exhausted duplicate %u returned 0x%08X / 0x%08X\n", i, result, *out);
            failures++;
        }
    }
    int64_t growth = (int64_t)native_bytes_in_use() - (int64_t)before;
    printf("Native malloc in-use growth after 4096 rejected duplicates: %lld bytes\n", (long long)growth);
    check("rejected duplicates do not leak native heap", growth < 128 * 1024);

    /* Failure paths must not strand thousands of references, so releasing
     * all real handles frees the sample heap block. This is a small guest
     * allocation, whose freed header remains mapped in the heap slab. */
    for (unsigned i = MAX_SLOTS; i-- > 0;) release(objects[i]);
    check("failed duplicates do not leak guest sample buffer",
          !hle_heap_ok(hle_ptr(data)));

    /* The same slot must be reusable after releases. */
    *out = 0;
    check("new source after slot reuse", new_source(ds, hle_va(desc), hle_va(out)) == DS_OK && *out);
    if (*out) {
        uint32_t fresh = *out;
        uint16_t *wfx = (uint16_t *)hle_alloc(18);
        uint16_t *observed = (uint16_t *)hle_alloc(18);
        check("format test allocations", wfx && observed);
        if (!wfx || !observed) return 2;
        memset(wfx, 0, 18);
        wfx[0] = 1; /* PCM */
        wfx[1] = 2;
        *(uint32_t *)&wfx[2] = 22050u;
        wfx[6] = 4; wfx[7] = 16;
        uint32_t set_args[2] = {fresh, hle_va(wfx)};
        check("set valid PCM format", call("IDirectSoundBuffer::SetFormat", 2, set_args) == DS_OK);
        wfx[6] = 0; /* previously accepted an invalid zero block size */
        check("reject invalid SetFormat block size",
              call("IDirectSoundBuffer::SetFormat", 2, set_args) == DSERR_INVALIDPARAM);
        uint32_t get_args[4] = {fresh, hle_va(observed), 18, 0};
        check("read still-valid PCM format", call("IDirectSoundBuffer::GetFormat", 4, get_args) == DS_OK);
        check("invalid SetFormat preserved prior block size and channels",
              observed[6] == 4 && observed[1] == 2 && observed[7] == 16);
        hle_free(wfx); hle_free(observed);
        release(fresh);
    }

    /* Many ordinary sound effects are created and released during gameplay.
     * The host heap should not grow with each successful cycle either. */
    desc[2] = 2048;
    before = native_bytes_in_use();
    for (unsigned i = 0; i < 2048; ++i) {
        *out = 0;
        uint32_t result = new_source(ds, hle_va(desc), hle_va(out));
        if (result != DS_OK || !*out) {
            check("repeated sound-buffer creation succeeds", 0);
            break;
        }
        release(*out);
    }
    growth = (int64_t)native_bytes_in_use() - (int64_t)before;
    printf("Native malloc growth after 2048 normal create/release cycles: %lld bytes\n", (long long)growth);
    check("ordinary sound effects do not grow native heap", growth < 192 * 1024);

    /* Zero-byte secondary buffers have no valid play ring and must not
     * reach modulo/PCM operations later. */
    desc[2] = 0;
    *out = 0xABCDEF01u;
    check("reject zero-length secondary buffer",
          new_source(ds, hle_va(desc), hle_va(out)) == DSERR_INVALIDPARAM && !*out);

    /* Streams must check channel/rate bounds and must not accept invalid IDs. */
    check("reject zero-rate audio stream", hle_audio_stream_open(0, 2) < 0);
    check("reject invalid-channel audio stream", hle_audio_stream_open(44100, 3) < 0);
    int id = hle_audio_stream_open(44100, 2);
    check("open valid stream", id >= 0);
    if (id >= 0) {
        int16_t samples[8] = {0};
        hle_audio_stream_push(id, samples, 4);
        hle_audio_stream_close(id);
        hle_audio_stream_push(id, samples, 4); /* ignored after close */
        check("closed stream has no queued frames", hle_audio_stream_queued(id) == 0);
    }

    call("IDirectSound::Release", 1, &ds);
    mach_leave();
    if (!failures) puts("PASS: failed duplicate cleanup, shared sample lifetime, reused slots, invalid sound formats and stream lifecycle");
    return failures ? 1 : 0;
}
