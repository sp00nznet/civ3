/*
 * The Linux host's dispatch table: the exe's lifted functions, then jgl.dll's
 * (0x10000000) and sound.dll's (rebased to 0x10080000). run_lift.py writes
 * each module's rows on their own (recomp_dispatch.inc); concatenated in this
 * order they are already sorted, as recomp_lookup's binary search needs.
 * The Windows build uses the exe's own recomp_dispatch.c: native32 runs the
 * real DLLs.
 */
#include "recomp_types.h"
#include "../recomp/gen/recomp_funcs.h"
#include "../recomp/gen_jgl/recomp_funcs.h"
#include "../recomp/gen_sound/recomp_funcs.h"

const recomp_dispatch_entry_t recomp_dispatch_table[] = {
#include "../recomp/gen/recomp_dispatch.inc"
#include "../recomp/gen_jgl/recomp_dispatch.inc"
#include "../recomp/gen_sound/recomp_dispatch.inc"
};
const uint32_t recomp_dispatch_count = sizeof recomp_dispatch_table / sizeof *recomp_dispatch_table;
