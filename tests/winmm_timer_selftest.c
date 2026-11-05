/* Exercise the real timeSetEvent worker through synthetic guest callbacks.
 * A callback deliberately takes several periods: the next call must wait,
 * rather than replaying missed audio updates immediately. No game assets.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "win32hle.h"
#include "host.h"

static atomic_uint calls, finished, failures;
static uint64_t previous_end;
static uint32_t expected_user;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static void pause_ms(unsigned ms) {
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000};
    while (nanosleep(&ts, &ts)) {}
}
static void check(const char *label, int ok) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", label); atomic_fetch_add(&failures, 1); }
}
static uint32_t invoke(const char *name, int n, uint32_t *args) {
    uint32_t shim = hle_resolve(name);
    if (!shim) hle_fatal("missing timer shim %s", name);
    return hle_call_guest(shim, n, args);
}
static void callback(void) {
    uint64_t start = now_ns();
    unsigned call = atomic_load(&calls);
    uint32_t id = A32(0);
    check("timer callback arguments", id && A32(1) == 0 &&
          A32(2) == expected_user && A32(3) == 0 && A32(4) == 0);
    if (call) {
        /* Allow clock quantization and host scheduling variance. The old
         * worker repeats immediately here after our 120 ms callback. */
        check("late callback does not trigger immediate catch-up",
              start - previous_end >= 15000000ull);
    }
    pause_ms(120);
    if (call == 2) {
        check("callback can cancel its own timer",
              invoke("timeKillEvent", 1, &id) == 0);
    }
    previous_end = now_ns();
    atomic_store(&calls, call + 1);
    RET(0, 5);
}
static void oneshot(void) {
    atomic_fetch_add(&finished, 1);
    RET(0, 5);
}
const recomp_dispatch_entry_t recomp_dispatch_table[] = {
    {0x00401000u, callback}, {0x00401100u, oneshot}
};
const uint32_t recomp_dispatch_count = 2;

int main(void) {
    recomp_host_init();
    mach_enter();
    expected_user = 0xA15D00Du;
    uint32_t args[5] = {30, 1, 0x00401000u, expected_user, 1};
    uint32_t timer = invoke("timeSetEvent", 5, args);
    check("periodic timer created", timer != 0);
    mach_leave();
    uint64_t deadline = now_ns() + 3000000000ull;
    while (atomic_load(&calls) < 3 && now_ns() < deadline) pause_ms(1);
    /* Taking the machine ensures the last callback returned. */
    mach_enter(); mach_leave();
    check("periodic timer made progress", atomic_load(&calls) == 3);
    pause_ms(100);
    check("self-cancel stopped future callbacks", atomic_load(&calls) == 3);

    mach_enter();
    args[2] = 0x00401100u; args[4] = 0;
    check("one-shot timer created", invoke("timeSetEvent", 5, args) != 0);
    mach_leave();
    deadline = now_ns() + 1000000000ull;
    while (!atomic_load(&finished) && now_ns() < deadline) pause_ms(1);
    pause_ms(100);
    check("one-shot fires exactly once", atomic_load(&finished) == 1);
    printf("%s: delayed periodic guest callbacks, self-cancellation and one-shot timer\n",
           atomic_load(&failures) ? "FAIL" : "PASS");
    return atomic_load(&failures) ? 1 : 0;
}
