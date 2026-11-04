/* Original synthetic guest for the real Civ3 host; contains no game code.
 * It exercises startup, both lifted DLLs, import binding, name/ordinal
 * exports and nested calls through the 32-bit guest ABI on a 64-bit host. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "win32hle.h"

#define CHECK(condition, message) do { \
    if (!(condition)) hle_fatal("host smoke: %s", message); \
} while (0)

static unsigned jgl_attaches, sound_attaches;

static void jgl_main(void) {
    CHECK(A32(0) == 0x10000000u && A32(1) == 1, "jgl DllMain arguments");
    jgl_attaches++;
    RET(1, 3);
}
static void sound_main(void) {
    CHECK(A32(0) == 0x10080000u && A32(1) == 1, "sound DllMain arguments");
    sound_attaches++;
    RET(1, 3);
}
static void jgl_export(void) { RET(42, 0); }
static void sound_export(void) { RET(84, 0); }

static void fixture_entry(void) {
    char *strings = hle_alloc(128);
    CHECK(strings != NULL, "guest allocation");
    uint32_t va = hle_va(strings);
    CHECK(hle_ptr(va) == strings, "guest pointer round trip");
#ifndef HLE_FLAT
    CHECK((uintptr_t)strings > UINT32_MAX, "test must exercise high host addresses");
#endif
    strcpy(strings, "jgl.dll");
    strcpy(strings + 16, ".\\sound.dll");
    strcpy(strings + 40, "FixtureExport");
    strcpy(strings + 64, "MissingExport");

    uint32_t load = hle_resolve("LoadLibraryA");
    uint32_t getproc = hle_resolve("GetProcAddress");
    uint32_t args[2] = { va, 0 };
    uint32_t jgl = hle_call_guest(load, 1, args);
    CHECK(jgl == 0x10000000u, "jgl mapped at the lifted address");
    CHECK(hle_call_guest(load, 1, args) == jgl, "repeat LoadLibrary");
    CHECK(jgl_attaches == 1, "DllMain called once");
    args[0] = va + 16;
    uint32_t sound = hle_call_guest(load, 1, args);
    CHECK(sound == 0x10080000u && sound_attaches == 1, "sound mapping and attach");

    for (unsigned i = 0; i < 2; i++) {
        uint32_t base = i ? sound : jgl;
        CHECK(MEM32(base + 0x1050) == hle_resolve("GetTickCount"), "DLL import binding");
        args[0] = base;
        args[1] = va + 40;
        uint32_t symbol = hle_call_guest(getproc, 2, args);
        CHECK(symbol == base + 0x1200, "named export uses translated guest strings");
        CHECK(hle_call_guest(symbol, 0, NULL) == (i ? 84u : 42u), "lifted export dispatch");
        args[1] = 1;
        CHECK(hle_call_guest(getproc, 2, args) == symbol, "ordinal export");
        args[1] = va + 64;
        CHECK(hle_call_guest(getproc, 2, args) == 0, "missing export returns NULL");
    }
    hle_free(strings);
    puts("civ3 host smoke: startup, guest pointers, DLL imports and exports passed");
    RET(0, 0);
}

const recomp_dispatch_entry_t recomp_dispatch_table[] = {
    { 0x0066B0A9u, fixture_entry },
    { 0x10001000u, jgl_main },
    { 0x10001200u, jgl_export },
    { 0x10081000u, sound_main },
    { 0x10081200u, sound_export },
};
const uint32_t recomp_dispatch_count = sizeof recomp_dispatch_table / sizeof *recomp_dispatch_table;
