# Culture-linked startup repair and extended Mac testing

Verified locally on November 3, 2026, on the Apple Silicon Mac in the
[validation ledger](macos-validation.md). This is an experimental compatibility
repair, not a claim of complete campaign, scenario, multiplayer or map-layout
conformance with the original Windows executable.

## What changed

The earlier native port required Culturally Linked Start Locations to be off.
With that option on, startup could index the civilization rules with `-1` and
read before the rules allocation. Whether that neighboring memory was mapped
made the failure intermittent.

The startup routine clears the leader identities before choosing civilizations,
then initializes the actual leaders **after** selection. Two culture-comparison
sites were reading the not-yet-initialized leader identity. The current choices
were already available in the human choice argument, opponent-choice vector,
or scenario-player entries. The repair reads those pending selections instead.

`src/linux/culture_start_fix.h` preserves any already-valid identity, validates
player and race bounds, and resolves only an unassigned identity. A still-random,
invalid or unavailable selection is skipped rather than indexed into the rules.
The helper does not write game memory, choose civilizations, consume random
numbers, alter a save, or initialize leaders early.

## Reproducible build-only transformation

`tools/macos_culture_overlay.py` applies the helper at the two instruction sites
in a **build-directory copy** of generated C. The original generated inputs and
commercial executable remain unchanged. The transformation requires the exact
supported executable SHA-1 and the expected four/five occurrences across aliased
entrypoints in the full pinned lift. A changed instruction shape, partial lift,
unsupported executable, or already-modified input causes a build failure rather
than an incomplete silent repair.

The CMake switch `CIV3_CULTURE_START_FIX` defaults to `ON` for the Mac path.
No toolkit checkout is edited. Linux and Windows continue to use their previous
source paths. Cached overlays check input metadata, the transformation's hash,
the executable hash, and output hashes; unchanged configure/build runs preserve
both source-overlay and executable timestamps. A measured no-change build took
0.299 seconds on this Mac.

**Conservative preferences are unchanged:** first setup still defaults the
culture-linked option to off, and existing preferences are not rewritten.
Enabling it now uses the experimental repair in a normally configured Mac build.
The preflight warns about its limited validation and about older/unpatched builds.
Exact start-location equivalence, unusual scenario configurations, and multiplayer
have not been established.

## Completed tests on the repaired executable

All runs below used binary SHA-256:
`f8b032bf168b101c57452b4414b8bb7399c9dcc2f5527a1aae6cd0d9ab9953f8`.

| Test | Observed result |
|---|---|
| Random civilization, tiny map, culture-linked rule on | Founded Rome and reached turn 4 / 3800 BC |
| Selected Persia, small map, culture-linked rule on | Founded Persepolis and reached turn 4 / 3800 BC |
| Random civilization, standard map, culture-linked rule on | Founded Rome and reached turn 2 / 3900 BC |
| Reusable new-game test tool, another tiny map | Passed through turn 3; original preferences unchanged |
| Actual native Cocoa window, Persia, small map, culture-linked rule on | Founded Persepolis and reached turn 4 / 3800 BC with audio enabled; OS window registration confirmed and framebuffer visually inspected |
| Save -> process exit -> reload | Restored the manual save at turn 3 and played to turn 7, creating four new autosaves; source save unchanged |

The three matrix framebuffers were visually inspected. Logs and preferences
confirmed that the culture-linked bit remained enabled, the map sizes were the
requested sizes, and the selected-Persia case actually used Persia. These were
not menu-only or process-exists checks. The bounded runs ended with the host's
expected watchdog exit code 4.

Local evidence: `work/macos-culture-matrix/`, `work/macos-newgame-tool-verified/`,
`work/macos-culture-roundtrip/`, `work/macos-culture-window/`, and
`work/macos-culture-investigation/`.
No screenshots, generated game C, saves, fonts or game assets are published.

## Additional testing earlier in this pass

Before adding the culture repair, the previous verified binary was frozen for
two isolated checks:

- A five-minute gameplay run advanced from turn 3 to turn 19 / 3050 BC, without
  detected runtime failures. This is a short soak, **not** a long-campaign test.
- A recorded UI run opened the Domestic Advisor, Science Advisor, Civilopedia
  and Preferences, returned to the map, and advanced from turn 3 to turn 11.
  The recorded screens were visually inspected.

Those runs used the pre-repair binary, SHA-256:
`edbcb20f29f52fa95c51231783bed7bd8ba5d0dc3708e3a84d47bae4b38e80f0`.
Their evidence is retained in `work/macos-extended-qa/`, separately from the
final-build results above.

## Regression coverage and commands

**54 Python tests and five native tests passed.** All five native tests also
built and passed from a source-only staging directory without commercial assets
or generated game code.

The native culture test covers all 961 player/human-slot combinations, already
valid identities, scenario entries, missing/random selections, invalid player
counts, and null/overflowing choice-vector addresses. The overlay tests cover
aliased sites, modified input, partial lifts, executable mismatch, unchanged
inputs, output tampering, missing cache products and repeat-build timestamps.

The smoke validator now rejects unexpected game error dialogs even when a
main-menu milestone and colorful framebuffer exist. Only the exact known
upstream Steam initialization notice is tolerated.

```sh
python3 -m unittest discover -s tests -p 'test_macos*.py' -v
ctest --test-dir build-macos --output-on-failure
python3 tools/macos_newgame_test.py
# Or one focused culture-enabled case:
python3 tools/macos_newgame_test.py persian-small
```

The new-game tool makes fresh APFS copy-on-write installations, retains inherited
saves separately, records the binary hash and actual settings, rejects errors or
lack of completed turns, and does not reuse earlier successful screenshots.
It refuses an existing output directory or an output inside the source game.
