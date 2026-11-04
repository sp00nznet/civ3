/* Synthetic MP3 fixture and lifted callback; no commercial game files. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "win32hle.h"
#include "host.h"

#define FETCH_VA 0x00400100u
static uint8_t *mp3;
static uint32_t mp3_size;
static int failures;

static void check(const char *name, int ok) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", name); ++failures; }
}

static void fetch_audio(void) {
    uint32_t offset = A32(3), count = A32(2);
    if (offset >= mp3_size) RET(0, 4);
    if (count > mp3_size - offset) count = mp3_size - offset;
    memcpy(APTR(1), mp3 + offset, count);
    RET(count, 4);
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
    mach_leave();
    free(mp3);
    if (!failures) puts("PASS: Miles strings and five MP3 open/decode/close cycles in 64-bit guest memory");
    return failures ? 1 : 0;
}
