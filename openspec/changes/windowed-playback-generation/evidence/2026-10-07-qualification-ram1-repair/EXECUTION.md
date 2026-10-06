# Qualification image RAM1 repair — execution packet

## Identity and scope

- **Task:** Repair the existing `teensy41-capture-serial-window-qualification` RAM1 overflow so it links within the approved 12-bank budget; no OpenSpec behavior task is checked off by this memory-placement slice.
- **Owner/files:** Keep ownership with `Track` and the existing qualification-only rolling playback path. Primary code: `include/Track.h`, `src/Track/TrackPlaybackWindowBuild.cpp`, and `src/Track/TrackPlaybackHotPath.cpp`; existing RAM1 linker script and guard are evidence, not budget authorities to weaken. No new field/state; default code path remains unchanged.
- **Baseline:** HEAD `0d1144d9`; worktree includes prior 3.7a documentation closeout plus this scope note. Baseline qualification output: `variables 96,192`, `code 393,240 + padding 32,744`, locals `2,112`; default capture baseline: `variables 96,192`, `code 392,248 + padding 968`, locals `34,880`.
- **Contract:** Preserve all playback decisions, identities, output ordering, refusal/recovery outcomes, DTCM globals, and the default capture RAM1 line. Do not rebase `scripts/ram1_budget.json`. Move/outline only code required to recover the qualification image's 13th ITCM bank. No device timing claim; any changed execution placement is not timing-equivalent evidence.
- **Acceptance:** Both `teensy41-capture-serial` and `teensy41-capture-serial-window-qualification` link with the approved RAM1 variables (`96,192`), the qualification image has at most 12 ITCM banks and passes the 840 B padding floor, and the default image remains byte-identical in its RAM1 line. Qualification playback semantics remain native-green.
- **Explicit non-goals:** No upload/flash, no lane enablement change, no timing/service-envelope claim, no linker budget rebase, no new state/scheduler/owner, no change to 3.7b/3.14/4.5.
- **Commit boundary:** One independently reviewable linker-placement/code-size repair plus evidence; preserve unrelated prior documentation changes.

## Oracle / transition

Initial state: existing source and current default/qualification build artifacts at the stated baseline. The production state machine and fixtures do not change. Build the two environments and compare their linked RAM1 figures; inspect the qualification ELF's `.text.itcm`, `.data`, `.bss`, and symbol deltas against the default ELF. If the guard still fails, use the saved ELF/map and symbol attribution to choose another cold branch; do not trim assertions, disable behavior, or change budgets.

Expected: default code behavior and RAM1 line unchanged; qualification rolling behavior compiles with the same outcome logic, uses at most 12 ITCM banks, and reports nonnegative locals meeting the guard. A compile-only pass without the RAM1 line is not acceptance.

## Controls and verification

- Existing behavior coverage: `test_runtime_working_set_ownership` rolling/paged path fixtures, including admission, adoption, fallback, refusal, cancellation, and exact MIDI output.
- Focused and broad native tests run after the code-placement edit; no new fixture is needed unless semantics move or a test fails.
- Builds: `pio run -e teensy41-capture-serial`; `pio run -e teensy41-capture-serial-window-qualification`.
- Record exact ELF section/symbol before/after and guard output. Confirm `git diff --stat -- src include test` and no DTCM variable delta beyond the unchanged approved line.
- `openspec validate windowed-playback-generation --strict`; `git diff --check`.
- Device: NOT RUN. No flash or hardware claim.

## Completion record

- Baseline qualification build: reproduced failure, 13 banks, locals 2,112 B; DTCM `variables` unchanged.
- **Code/ELF attribution:** The qualification-only source selector and rolling-window resolver are explicitly `FLASHMEM`. Qualification `.text.itcm` is 391,456 B versus 392,384 B at baseline (−928 B); DTCM `.data`/`.bss` and reported `variables 96,192` are unchanged.
- **RAM1 builds:** `teensy41-capture-serial-window-qualification` **SUCCESS**, 12 banks, `code 392,312 + padding 904`, locals 34,880, 64 B above the unchanged 840 B floor. Default `teensy41-capture-serial` **SUCCESS**, unchanged RAM1 line `variables 96,192; code 392,248 + padding 968`.
- **Native/validation:** focused rolling suite 176/176; `pio test -e native` **2,855 cases / 2,848 succeeded / 7 skipped / 0 failed**; `openspec validate windowed-playback-generation --strict` valid; `git diff --check` clean. Build warnings for unused `rollingGateFlags` and `snapshotGateVerdicts` remain non-fatal.
- **Timing/device:** No upload or device run. FLASH placement is on the clock path and changes physical execution timing; neither device timing equivalence nor a service-envelope claim is made.
