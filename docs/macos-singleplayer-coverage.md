# Apple Silicon single-player compatibility matrix

Local validation on November 4, 2026 with the native arm64 Civilization III
host from `main` (game executable SHA-256
`9ea0f9049be5ed72197e61f4805615ccd8ca04d6201e4e213dad2b7b49f5c429`).
All cases used **private APFS game copies** with separate temporary saves.
The original installation, primary saves and preferences were not modified.

This work deliberately prioritizes long single-player campaigns, bundled
Conquests content and rules modifications. It is not a complete certification
of every scenario, turn, custom mod or victory condition.

## Campaign continuation

| Case | Verified result | Scope |
|---|---|---|
| Existing Persian 330 BC save | **Pass: turn 111 -> turn 121, 130 BC** | 200-second run, five new autosaves, final Samaria city view visually checked; original source hash unchanged |

This is late-game continuation, **not** 121 consecutive turns played on this
port from a fresh start. It does not test all victory conditions or later eras.

## Bundled Conquests and custom scenario results

| Scenario | Observed result | Evidence and caveat |
|---|---|---|
| **Mesopotamia** | **Pass: initial turn -> turn 1 (3960 BC)** | Loaded `1 Mesopotamia.biq`, two fresh autosaves, Phoenician map/units screenshot examined |
| **Mesoamerica** | **Pass: turn 4 (300 AD)** | Loaded the custom `5 Mesoamerica.biq`, generated five autosaves, Mayan city/map screenshot examined |
| **Fall of Rome** | **Pass: turn 1 (324 AD)** | Custom cities and rules loaded, two fresh autosaves, screenshot captured |
| **Middle Ages** | **Pass: turn 1 (843 AD)** | Custom art and rules loaded, two fresh autosaves, screenshot captured |
| **Sengoku — Sword of the Shogun** | **Pass: turn 7 (January 1453 AD)** | Six fresh autosaves; Japanese units/map screenshot examined |
| **Age of Discovery** | **Loaded and rendered; progression not verified** | Scenario BIQ, English/European historic map in 1490 AD; one initial autosave, still showing 'Please wait' during bounded run |
| **Napoleonic Europe** | **Loaded and rendered; progression not verified** | 1800 AD England city screen, scenario file loaded; one initial autosave but no completed next turn |
| **Rise of Rome** | **Loaded and rendered; progression not verified** | `2 Rise of Rome.biq`, historical 350 BC map screenshot with Carthaginian units, but two bounded runs did not confirm a completed next turn |
| **WWII in the Pacific** | **Loaded and rendered; progression not verified** | `9 WWII in the Pacific.biq`, Dec. 1941 map and naval units, one initial autosave; first run missed a safe screenshot, longer retry captured a scene showing 'Please wait' but still turn 0 |
| **Ancient Treasures introductory scenario** | **Inconclusive** | Scenario BIQ loaded; no fresh autosave and no safe framebuffer captured during bounded run |
| **The Three Sisters introductory scenario** | **Pass: turn 2 (300 AD)** | Intro2 BIQ loaded and three autosaves produced with safe final screenshot |
| **New Alliances introductory scenario** | **Loaded and rendered; progression not verified** | Intro3 BIQ opened, initial 700 AD autosave, but still at turn 0 at the end of the run |

A game reaching its initial position does **not** prove its AI turn will
complete. The Rise of Rome, WWII, Age of Discovery and Napoleonic results should not be
reported as full scenario compatibility passes. The shared runtime's bounded watchdog may also
return exit code 5 when the guest cannot safely yield for a screenshot;
that outcome alone is not evidence of a gameplay crash.

## Installed single-player rules/content variants

| Variant | Observed result | Scope |
|---|---|---|
| **No Civ Traits** (`.biq`) | **Pass: turn 11, 3450 BC** | File actually opened, fresh autosaves, map visually reviewed |
| **With Plague** (`.biq`) | **Pass: turn 18, 3100 BC** | File actually opened, fresh autosaves, Greek map visually reviewed; no in-game outbreak was specifically tested |
| **Cheaper Upgrades** (`.biq`) | **Pass: turn 7, 3650 BC** | Selected scenario loaded, six fresh autosaves, bounded native screenshot |
| **No Unique Units** (`.biq`) | **Pass: turn 12, 3400 BC** | Selected scenario loaded, six fresh autosaves, bounded native screenshot |
| **Quick Civ** (`.biq`) | **Pass: turn 6, 2875 BC** | Selected scenario loaded, six fresh autosaves, bounded native screenshot |
| **Age of Bronze** (`age of bronze.bix`) | **Partial progress; no screenshot verdict** | Older Play the World scenario actually opened and generated six autosaves from 4000 through 3750 BC; the snapshot watchdog timed out while the guest was busy (exit 5) |

These results test ruleset loading, graphics and basic turn progression, not
every special rule, event, custom asset path or full campaign.

## Testing, isolation and reproducibility

Three new helpers in `tools/` reproduce these checks without retaining huge
copies of the game:

```sh
python3 tools/macos_campaign_soak.py \
  --seed-save "game/Conquests/Saves/Xerxes of the Persians, 330 BC.SAV" \
  --output work/persian-campaign-check --play 200 --interval 900

python3 tools/macos_scenario_soak.py mesoamerica \
  --output work/mesoamerica-check --play 130

python3 tools/macos_mod_soak.py with-plague \
  --output work/plague-check --play 145
```

Provide a **new** output directory for each run. Each script rejects
preexisting output paths, clones the input game, records the loaded scenario
file, game milestones, logs and final screenshot when available, and removes
the disposable clone and its test saves. The original source remains intact.
Because all test tools use scripted input at 1024x768, a scenario with
different setup dialogs may need revised click coordinates; failing to advance
is not automatically a game-engine defect.

For diagnostic screenshots and logs, inspect the local ignored `work/`
directories named for each case. None of the game files, saves, fonts, lifted
generated source or screenshots belong in Git. The regular Desktop app and
native game build are unaffected by these checks.

## What remains unproven

- A complete campaign from 4000 BC to victory, including later technological
  eras, diplomacy, combat, victory conditions and uninterrupted save/reload.
- AI advancement in particularly large scenarios (notably WWII and Rise of
  Rome) within short test windows. These scenarios exercise more game-state
  work than the smaller passing scenarios.
- Scenario-specific events, completed scenarios, later turns in larger maps,
  and arbitrary external user mods beyond the installed rules sets tested here.
- Equivalent Windows behavior of custom scripted events and scenario rules.

A fully optimized `-O1` build was investigated to evaluate large-scenario
AI performance, but the very large generated source compile was stopped
before completion. No performance or correctness result is claimed for that
experimental build, and the current playable `-O0` game was left intact.
