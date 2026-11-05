/* Pure, source-only regression for first-turn runaway effects suppression. */
#include <stdint.h>
#include <stdio.h>
#include "sfx_guard.h"

static int errors;
static void check(const char *what, int good) {
    if (!good) { fprintf(stderr, "FAIL: %s\n", what); errors++; }
}
static void step(civ3_sfx_guard *s, float rms, float clip, unsigned frames) {
    (void)civ3_sfx_guard_step(s, rms, clip, frames, 44100);
}

int main(void) {
    civ3_sfx_guard state = {.gain = 1.0f};
    /* Normal quiet PCM and a short loud effect should never trigger. */
    for (int i = 0; i < 180; ++i) step(&state, 0.05f, 0.0f, 1024);
    check("quiet audio passes unchanged", !state.muted && state.gain == 1.0f);
    for (int i = 0; i < 20; ++i) step(&state, 0.72f, 0.12f, 1024);
    check("short effect passes unchanged", !state.muted && state.gain == 1.0f);

    /* A long, clean loud soundtrack-like signal is not distortion.
     * Do not mistake amplitude alone for clipped runaway audio. */
    civ3_sfx_guard unclipped = {.gain = 1.0f};
    for (int i = 0; i < 220; ++i) step(&unclipped, 0.78f, 0.0f, 1024);
    check("sustained loud but unclipped sound is not muted",
          !unclipped.muted && unclipped.gain == 1.0f);

    /* Several separated legitimate impacts must not accumulate the
     * 750 ms threshold across quiet periods. */
    civ3_sfx_guard bursts = {.gain = 1.0f};
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 14; ++j) step(&bursts, 0.82f, 0.12f, 1024);
        for (int j = 0; j < 16; ++j) step(&bursts, 0.02f, 0.0f, 1024);
    }
    check("separated clipped impacts retain sound effects",
          !bursts.muted && bursts.gain == 1.0f);

    /* A sustained almost full-scale repeated waveform must be suppressed.
     * This is what the first-turn Civ3 software effects mixer produced. */
    int triggered = 0;
    for (int i = 0; i < 100; ++i) {
        if (civ3_sfx_guard_step(&state, 0.82f, 0.26f, 1024, 44100) > 0) triggered++;
    }
    check("sustained clipping triggers exactly once", triggered == 1);
    check("sustained clipping remains quiet but not fully muted", state.muted && state.gain >= 0.14f && state.gain <= 0.16f);

    /* Intermittent moderately loud noise should not repeatedly toggle state. */
    for (int i = 0; i < 90; ++i) step(&state, 0.36f, 0.01f, 1024);
    check("does not unmute in noisy medium-level stream", state.muted);

    int recovered = 0;
    for (int i = 0; i < 150; ++i)
        if (civ3_sfx_guard_step(&state, 0.01f, 0.0f, 1024, 44100) < 0) recovered++;
    check("calm signal re-enables effects once", recovered == 1);
    check("gain fades to unity after recovery", !state.muted && state.gain == 1.0f);

    /* No state changes from malformed callback arguments. */
    civ3_sfx_guard copy = state;
    check("zero rate ignored", civ3_sfx_guard_step(&state, 0.8f, 0.8f, 1024, 0) == 0);
    check("zero frames ignored", civ3_sfx_guard_step(&state, 0.8f, 0.8f, 0, 44100) == 0);
    check("invalid updates leave state intact", state.gain == copy.gain && state.muted == copy.muted);

    if (!errors) puts("PASS: sustained effects distortion attenuated without total silence; quiet samples and automatic recovery retained");
    return errors ? 1 : 0;
}
