/* No game data: strict CLI parsing and cross-process installation exclusion. */
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "game_lock.h"
#include "host_options.h"
#include "script_options.h"
static int failures;
#define CHECK(name, condition) do { if (!(condition)) { fprintf(stderr, "FAIL: %s\n", name); ++failures; } } while (0)
int main(void) {
    uint32_t value = 99;
    const char *bad[] = {"", "0", "-1", "+1", "1x", " 5", "5 ", "NaN", "1e2", "241", "99999999999999999999999999999"};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; ++i) CHECK(bad[i], !civ3_unsigned_option(bad[i], 1, 240, &value));
    CHECK("valid fps", civ3_unsigned_option("240", 1, 240, &value) && value == 240);
    const char *events[][2] = {{"--move","1,2@nan"},{"--move","1,2@inf"},{"--move","1,2@-1"},
        {"--move","1,2@3junk"},{"--move","999999999999999999,2@1"},{"--click","32768,2@1"},
        {"--key","z+0x42@1"},{"--key","cc+0x42@1"},{"--key","0x100@1"},{"--key","+42@1"},
        {"--wait","0xFFFF@1"},{"--wait","0x10001@1"},{"--wait","0x100000000@1"},
        {"--autopilot","0@1"},{"--autopilot","-3@2"},{"--autopilot","9999999999@2"},
        {"--click","1,2@86401"},{"--click",NULL}};
    ev_t e;
    for (unsigned i = 0; i < sizeof events / sizeof *events; ++i)
        CHECK(events[i][0], civ3_parse_event(events[i][0], events[i][1], &e) == -1);
    CHECK("normal click", civ3_parse_event("--click", "621,557@10.5", &e) == 1 && e.x == 621 && e.y == 557 && e.t == 10.5);
    CHECK("modifiers", civ3_parse_event("--key", "cs+0x4D@26", &e) == 1 && e.y == 3 && e.x == 0x4D);
    CHECK("wait", civ3_parse_event("--wait", "0xA74EA4@11", &e) == 1 && e.x == 0xA74EA4);
    CHECK("not an event", civ3_parse_event("--help", NULL, &e) == 0);
    char dir[] = "/tmp/civ3-safety.XXXXXX";
    if (!mkdtemp(dir)) return 2;
    int lock = civ3_game_lock(dir); CHECK("acquire game lock", lock >= 0);
    pid_t child = fork();
    if (child == 0) {
        close(lock);
        int second = civ3_game_lock(dir);
        _exit(second < 0 && (errno == EWOULDBLOCK || errno == EAGAIN) ? 0 : 1);
    }
    if (child < 0) return 2;
    int status; waitpid(child, &status, 0);
    CHECK("another process excluded", WIFEXITED(status) && !WEXITSTATUS(status));
    close(lock);
    lock = civ3_game_lock(dir); CHECK("stale PID does not block relaunch", lock >= 0); close(lock);
    char path[512], target[512];
    snprintf(path, sizeof path, "%s/.civ3-native.lock", dir);
    snprintf(target, sizeof target, "%s/do-not-touch", dir);
    FILE *f = fopen(target,"w"); if (!f) return 2; fputs("KEEP",f); fclose(f);
    unlink(path); if (symlink(target, path)) return 2;
    CHECK("refuse symlink lock", civ3_game_lock(dir) < 0);
    char bytes[5] = {0}; f = fopen(target,"r"); if (!f) return 2; fread(bytes,1,4,f); fclose(f);
    CHECK("symlink target unchanged", !strcmp(bytes,"KEEP"));
    unlink(path); unlink(target); rmdir(dir);
    if (!failures) puts("PASS: strict numeric/event parsing, process exclusion, stale locks and symlink safety");
    return failures != 0;
}
