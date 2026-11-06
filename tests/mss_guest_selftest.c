/* Synthetic MP3 fixture and lifted callback; no commercial game files. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc/malloc.h>
#include "win32hle.h"
#include "host.h"

#define FETCH_VA 0x00400100u
static uint8_t *mp3;
static uint32_t mp3_size;
static int failures;
static int misreport_fetch;

static void check(const char *name, int ok) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", name); ++failures; }
}

static void fetch_audio(void) {
    uint32_t offset = A32(3), count = A32(2);
    if (offset >= mp3_size) RET(0, 4);
    if (count > mp3_size - offset) count = mp3_size - offset;
    memcpy(APTR(1), mp3 + offset, count);
    RET(count + (misreport_fetch ? 1u : 0u), 4);
}

const recomp_dispatch_entry_t recomp_dispatch_table[] = {{FETCH_VA, fetch_audio}};
const uint32_t recomp_dispatch_count = 1;

static uint32_t call(const char *name, int n, const uint32_t *args) {
    uint32_t va = hle_resolve(name);
    if (!va) { fprintf(stderr, "missing shim: %s\n", name); ++failures; return 0; }
    return hle_call_guest(va, n, args);
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s synthetic.mp3\n", argv[0]); return 2; }
    FILE *file = fopen(argv[1], "rb");
    if (!file || fseek(file, 0, SEEK_END)) return 2;
    long size = ftell(file);
    if (size <= 0 || size > 1024 * 1024) { fclose(file); return 2; }
    rewind(file);
    mp3_size = (uint32_t)size;
    mp3 = malloc(mp3_size);
    if (!mp3 || fread(mp3, 1, mp3_size, file) != mp3_size) { fclose(file); free(mp3); return 2; }
    fclose(file);
    recomp_host_init();
    mach_enter();
    uint32_t zero[4] = {0};
    uint32_t dir = call("_AIL_set_redist_directory@4", 1, zero);
    check("redist directory is guest memory", dir && !strcmp(hle_ptr(dir), ""));
    check("redist pointer is stable", dir && dir == call("_AIL_set_redist_directory@4", 1, zero));
    /* Even a rejected provider lookup registers the ASI function shims. */
    call("RIB_request_interface", 4, zero);
    uint32_t error = call("mss:ASI_error", 0, NULL);
    check("error string is guest memory", error && !strcmp(hle_ptr(error), ""));
    check("error pointer is stable", error && error == call("mss:ASI_error", 0, NULL));
    uint8_t *pcm = hle_alloc(8192);
    if (!pcm) return 2;
    for (int i = 0; i < 5; ++i) {
        uint32_t args[3] = {0, FETCH_VA, mp3_size};
        uint32_t stream = call("mss:ASI_stream_open", 3, args);
        check("stream handle is guest memory", stream && hle_va(hle_ptr(stream)) == stream);
        if (!stream) break;
        memset(pcm, 0, 8192);
        uint32_t decode[3] = {stream, hle_va(pcm), 8192};
        uint32_t count = call("mss:ASI_stream_process", 3, decode);
        int nonzero = 0;
        for (uint32_t j = 0; j < count && j < 8192; ++j) nonzero |= pcm[j];
        check("MP3 decoded through guest callback", count > 0 && count <= 8192 && nonzero);
        uint32_t close_args[1] = {stream};
        call("mss:ASI_stream_close", 1, close_args);
    }
    hle_free(pcm);
    /* Repeated valid opens should release the FFmpeg decoder + ASI state. */
    malloc_statistics_t before = {0}, after = {0};
    malloc_zone_statistics(malloc_default_zone(), &before);
    for (int i = 0; i < 100; ++i) {
        uint32_t args[3] = {0, FETCH_VA, mp3_size};
        uint32_t stream = call("mss:ASI_stream_open", 3, args);
        check("repeated MP3 stream opens", stream != 0);
        if (!stream) break;
        uint32_t close_args[1] = {stream};
        call("mss:ASI_stream_close", 1, close_args);
        check("closed ASI handle returned to guest heap", !hle_heap_ok(hle_ptr(stream)));
    }
    malloc_zone_statistics(malloc_default_zone(), &after);
    int64_t growth = (int64_t)after.size_in_use - (int64_t)before.size_in_use;
    printf("Miles native heap growth after 100 open/close cycles: %lld bytes\n", (long long)growth);
    check("no accumulating FFmpeg allocations", growth < 1024 * 1024);

    /* A malformed source and a guest callback reporting more bytes than
     * requested must fail safely, never copy beyond the scratch buffer. */
    uint8_t bad[8192];
    for (size_t i = 0; i < sizeof bad; ++i) bad[i] = (uint8_t)(i * 131u + (i >> 2));
    uint8_t *original_mp3 = mp3;
    uint32_t original_size = mp3_size;
    mp3 = bad;
    mp3_size = sizeof bad;
    for (int i = 0; i < 10; ++i) {
        uint32_t args[3] = {0, FETCH_VA, mp3_size};
        uint32_t stream = call("mss:ASI_stream_open", 3, args);
        check("reject malformed MP3 source", stream == 0);
        if (stream) { uint32_t a[1] = {stream}; call("mss:ASI_stream_close", 1, a); }
    }
    mp3 = original_mp3;
    mp3_size = original_size;
    misreport_fetch = 1;
    uint32_t wrong_args[3] = {0, FETCH_VA, mp3_size};
    uint32_t invalid = call("mss:ASI_stream_open", 3, wrong_args);
    check("oversized guest read result does not open a stream", invalid == 0);
    if (invalid) { uint32_t a[1] = {invalid}; call("mss:ASI_stream_close", 1, a); }
    misreport_fetch = 0;
    mach_leave();
    free(mp3);
    if (!failures) puts("PASS: Miles handles, malformed MP3, decoder lifetime and callback bounds");
    return failures ? 1 : 0;
}
