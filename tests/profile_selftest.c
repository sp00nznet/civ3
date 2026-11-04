/* Native 64-bit profile shim regression; only disposable synthetic INIs. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "win32hle.h"
#include "host.h"
const recomp_dispatch_entry_t recomp_dispatch_table[] = {{0, NULL}};
const uint32_t recomp_dispatch_count = 0;
static int failures;
static uint32_t name, buffer, fallback;
static void check(const char *label, int ok) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", label); ++failures; }
}
static uint32_t guest(const char *s) { return s ? hle_guest_strdup(s) : 0; }
static uint32_t call(const char *function, int n, const uint32_t *args) {
    uint32_t va = hle_resolve(function);
    if (!va) { fprintf(stderr, "Missing %s\n", function); exit(2); }
    return hle_call_guest(va, n, args);
}
static uint32_t write_value(const char *section, const char *key, const char *value) {
    uint32_t args[] = {guest(section), guest(key), guest(value), name};
    return call("WritePrivateProfileStringA", 4, args);
}
static int read_equals(const char *section, const char *key, const char *value) {
    uint32_t args[] = {guest(section), guest(key), fallback, buffer, 128, name};
    call("GetPrivateProfileStringA", 6, args);
    return !strcmp(hle_ptr(buffer), value);
}
int main(void) {
    char directory[] = "/tmp/civ3-profile.XXXXXX";
    if (!mkdtemp(directory)) return 2;
    char path[512]; snprintf(path, sizeof path, "%s/test.ini", directory);
    FILE *f = fopen(path, "w");
    if (!f) return 2;
    fputs("; keep comment\r\n[Video]\r\nWidth=1024\r\nOther=keep\r\n[Audio]\r\nVolume=50\r\n", f);
    fclose(f);
    recomp_host_init(); mach_enter();
    name = guest(path); buffer = hle_va(hle_alloc(128)); fallback = guest("missing");
    check("update existing key", write_value("video", "Width", "640") == 1);
    check("new value visible immediately", read_equals("VIDEO", "WIDTH", "640"));
    check("insert existing-section key", write_value("Video", "Height", "768") == 1);
    check("inserted key visible", read_equals("video", "Height", "768"));
    check("unrelated key preserved", read_equals("Video", "Other", "keep"));
    check("unrelated section preserved", read_equals("Audio", "Volume", "50"));
    for (int i = 0; i < 25; ++i) {
        char text[16]; snprintf(text, sizeof text, "%d", i);
        check("repeated update succeeds", write_value("Video", "Width", text) == 1);
        check("repeated update reads latest", read_equals("Video", "Width", text));
    }
    check("new section", write_value("Conquests", "Faction0", "12") == 1);
    uint32_t ints[] = {guest("Conquests"), guest("Faction0"), 999, name};
    check("integer read sees saved faction", call("GetPrivateProfileIntA", 4, ints) == 12);
    check("delete key", write_value("Video", "Width", NULL) == 1);
    check("deleted key falls back", read_equals("Video", "Width", "missing"));
    check("delete section", write_value("Video", NULL, NULL) == 1);
    check("deleted section gone", read_equals("Video", "Height", "missing"));
    check("other section survives deletion", read_equals("Audio", "Volume", "50"));
    f = fopen(path, "a");
    if (!f) return 2;
    fputs("[Conquests]\nFaction0=99\n", f); fclose(f);
    check("update duplicate legacy section", write_value("Conquests", "Faction0", "7") == 1);
    check("duplicate key removed", read_equals("Conquests", "Faction0", "7"));
    uint32_t zero_size[] = {guest("Audio"), guest("Volume"), fallback, 0xFFFFFFF0u, 0, name};
    check("zero-sized destination not touched", call("GetPrivateProfileStringA", 6, zero_size) == 0);
    check("flush is a no-op", write_value(NULL, NULL, NULL) == 0);
    check("flush preserves content", read_equals("Audio", "Volume", "50"));
    /* A directory cannot be atomically replaced by an INI. */
    name = guest(directory);
    check("write failures are reported", write_value("x", "y", "z") == 0);
    mach_leave();
    unlink(path); rmdir(directory);
    if (!failures) puts("PASS: profile insert/update/delete, case-insensitive sections, duplicate repair, failure and bounds checks");
    return failures ? 1 : 0;
}
