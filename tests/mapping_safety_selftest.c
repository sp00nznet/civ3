/* Synthetic Win32 file-mapping tests, no commercial game data.
 * The old implementation reserved a 65th view but did not record it,
 * making UnmapViewOfFile impossible for that allocation.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "win32hle.h"
#include "host.h"

#define INVALID_HANDLE 0xFFFFFFFFu
#define VIEWS 64

const recomp_dispatch_entry_t recomp_dispatch_table[] = {{0, NULL}};
const uint32_t recomp_dispatch_count = 0;

static int failures;
static void check(const char *label, int valid) {
    if (!valid) { fprintf(stderr, "FAIL: %s\n", label); failures++; }
}
static uint32_t call(const char *name, int argc, const uint32_t *argv) {
    uint32_t proc = hle_resolve(name);
    if (!proc) { fprintf(stderr, "Missing Win32 shim: %s\n", name); return 0; }
    return hle_call_guest(proc, argc, argv);
}
static uint32_t new_mapping(uint32_t high, uint32_t low) {
    uint32_t args[6] = {INVALID_HANDLE, 0, 4, high, low, 0};
    return call("CreateFileMappingA", 6, args);
}
static uint32_t view(uint32_t handle, uint32_t high, uint32_t low, uint32_t bytes) {
    uint32_t args[5] = {handle, 2, high, low, bytes};
    return call("MapViewOfFile", 5, args);
}
static uint32_t unmap(uint32_t address) {
    return call("UnmapViewOfFile", 1, &address);
}
static void close_handle(uint32_t handle) {
    call("CloseHandle", 1, &handle);
}

int main(int argc, char **argv) {
    int table_only = argc == 2 && strcmp(argv[1], "--table-only") == 0;
    recomp_host_init();
    mach_enter();
    uint32_t mapping = new_mapping(0, 65536);
    check("create anonymous mapping", mapping != 0);
    if (!mapping) return 2;
    uint32_t addresses[VIEWS] = {0};
    for (unsigned i = 0; i < VIEWS; i++) {
        addresses[i] = view(mapping, 0, 0, 65536);
        if (!addresses[i]) { check("all 64 view slots available", 0); break; }
        MEM32(addresses[i]) = 0xC0FFEE00u + i;
    }
    uint32_t beyond = view(mapping, 0, 0, 65536);
    check("65th view tracked beyond legacy 64-slot table", beyond != 0);
    if (beyond) {
        check("65th view has a valid release path", unmap(beyond) == 1);
        check("65th view no longer exists after release", unmap(beyond) == 0);
    }
    for (unsigned i = 0; i < VIEWS; i++) {
        if (!addresses[i]) continue;
        check("mapped guest view readable", MEM32(addresses[i]) == 0xC0FFEE00u + i);
        check("unmap succeeds exactly once", unmap(addresses[i]) == 1);
        check("double unmap safely rejected", unmap(addresses[i]) == 0);
    }
    /* Repeat to catch a mapping slot table that never clears during a
     * long-lived game save/load session. */
    for (int repeat = 0; repeat < 20; repeat++) {
        for (unsigned i = 0; i < VIEWS; i++) {
            uint32_t va = view(mapping, 0, 0, 65536);
            if (!va) { check("repeated mapping table reuse", 0); break; }
            check("repeated view release", unmap(va) == 1);
        }
    }
    close_handle(mapping);

    if (!table_only) {
    /* On a 32-bit guest, the view length must not wrap at 4 GiB.
     * These calls must be rejected before mmap or vm_reserve. */
    uint32_t huge = new_mapping(1, 4096);
    check("create 4GB+ virtual map object", huge != 0);
    if (huge) {
        uint32_t dangerous = view(huge, 0, 0, 0); /* would truncate 4GB+ to 4096 */
        check("oversized whole mapping rejected", dangerous == 0);
        if (dangerous) unmap(dangerous);
        close_handle(huge);
    }
    mapping = new_mapping(0, 65536);
    if (mapping) {
        check("past-end view rejected", view(mapping, 0, 0, 131072) == 0);
        check("past-end offset rejected", view(mapping, 0, 61440, 8192) == 0);
        check("no wrap-around of offset+length",
              view(mapping, 0xFFFFFFFFu, 0xFFFF0000u, 65536) == 0);
        close_handle(mapping);
    }

    }
    /* A read-only file mapping must reject FILE_MAP_WRITE before returning
     * an unreadable address that guest code will attempt to write. */
    uint32_t ro_args[6] = {INVALID_HANDLE, 0, 2, 0, 65536, 0};
    uint32_t ro = call("CreateFileMappingA", 6, ro_args);
    check("create read-only mapping", ro != 0);
    if (ro) {
        uint32_t unauthorized = view(ro, 0, 0, 4096); /* view() requests FILE_MAP_WRITE */
        check("reject write view of read-only mapping", unauthorized == 0);
        if (unauthorized) unmap(unauthorized);
        uint32_t read_args[5] = {ro, 4, 0, 0, 4096};
        uint32_t readable = call("MapViewOfFile", 5, read_args);
        check("read-only mapping still permits FILE_MAP_READ", readable != 0);
        if (readable) check("readable view can be unmapped", unmap(readable) == 1);
        close_handle(ro);
    }
    mach_leave();
    if (!failures) puts("PASS: view table exhaustion, unmap reuse, 4GB bounds, permissions and offset validation");
    return failures ? 1 : 0;
}
