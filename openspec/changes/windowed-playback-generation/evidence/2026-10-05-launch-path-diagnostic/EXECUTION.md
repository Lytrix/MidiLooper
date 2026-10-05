# Execution packet — the switch leg's launch-path diagnostic (G2)

Task: `windowed-playback-generation` §6.4/6.5's blocked hardware evidence. Follows
[`../2026-10-05-launch-path-trace/EXECUTION.md`](../2026-10-05-launch-path-trace/EXECUTION.md), whose trace this slice
corrects and whose "next discriminating step 2" it is: *one narrow row at
`preparePlaybackLaunchForSlot`'s exits … its own execution packet, RAM1 check and stimulus*.

## Identity and scope

- **Scope**: instrument the launch path's silent exits with a **qualification-only** row emitted **on state change**,
  then run **one** authorized switch episode with `NOTE_EDIT` confirmed closed and report the first failed transition.
  **Non-goals**: no fix of the behaviour the row exposes, no architecture change, no change to the fallback path, no
  re-enabling or disabling of the rolling lane, no broad logging refactor, no content-parity claim.
- **Owner and path**: `TrackManager::servicePendingSlotProvision` (the off-clock `SlotProvision` turn) →
  `Track::preparePlaybackLaunchForSlot` → its guards → the publication owner (unchanged). The reporter
  (`Track::reportPlaybackLaunchGateRow`) reads that same state and emits; it admits and refuses nothing.
- **Primary files**: `include/Utils/DebugSessionCapture.h` (declaration + `SC_LAUNCH_GATE` + `SC_LAUNCH_GATE_ENABLED`),
  `src/Utils/DebugSessionCapture.cpp` (emitter + Tier-A entry), `include/Track.h` +
  `src/Track/TrackPlaybackWindowBuild.cpp` (the per-target cadence state and the exit rows),
  `src/TrackManager/TrackManagerSlotMatrix.cpp` (the caller's three exits), fixtures in
  `test/test_runtime_working_set_ownership/` and `test/test_capture_ring/`.
- **Preconditions**: build `teensy41-capture-serial-window-qualification`; the registered `windowed_content_parity`
  leg with its `--switch-target-slot`; authorized scratch range track 8 slots 4/5/6; the target filled by an earlier
  session's 24-bar take. **External blocker of the parent task**: the launch's source publication, reported below.
- **Explicit non-goals for the record**: the row is *qualification-only by build* — every other image compiles neither
  the row nor its cadence state, so the default capture env and the plain firmware keep their approved RAM1 lines and
  their behaviour exactly.

## Oracle fixed before behaviour edits

| Case | Initial state | Action | Expected observation |
|---|---|---|---|
| G1 (the run-06 shape) | demoted 24-bar target, committed passes, **no** current root, no publish in flight, switch pending | one `servicePendingSlotProvision` turn | one row, `x=launch_requested`, `req=1`, `root=0`, `avoid=1`, `cmt=1`, `len=18432`, `t/s` = the target |
| G2 (cadence) | the state G1 leaves | two further identical turns | **no** new row; the state moved only once (preparation `Requested`, demand live) |
| G3 (source arrival) | the recorded request | publication stepped to current | a row whose flags move (`root` 1, `pend` 0) — the transition the trace could not see |
| G4/G5/G6 (direct) | active slot / empty slot / a 3-bar filled slot | `preparePlaybackLaunchForSlot` called directly | `launch_slot_not_eligible` / `launch_no_runtime` / `launch_not_demoted_content`, each with its target |
| G7 (caller) | a switch to a short filled target | the production turn | `launch_not_reached_legacy` **or** `launch_not_reached_occupied` (the discriminator is the target's occupancy, and `len` is on the row) |
| G8 (tier) | — | `isTierATextLine` on the row | true for `DIAG,launch_gate,`, false for `DIAG,launch_gatex,` (the exact boundary) |
| **C1 (counterexample)** | a demoted rootless target | two turns, asserting the row count stays 2 | a row per turn (the cadence removed) must fail the fixture — that mutation is the control |

## Verification recipe

- `pio test -e native -f test_runtime_working_set_ownership` → **143/143**;
  `pio test -e native -f test_capture_ring` → the tier suite's **159/159** in the same run;
  `pio test -e native` → **2,797 cases / 7 skipped / 2,790 succeeded / 0 failed**.
- `pio run -e teensy41-capture-serial` and `-e teensy41-capture-serial-window-qualification`: both **SUCCESS** with
  their RAM1 lines byte-identical (`variables 96,192`; the qualification `code 392,232 + padding 984`).
- `scripts/check_marker_contract.py --audit`: 84 Tier-A prefixes, 0 over the 31-byte head-read limit.
- **Device**: the qualification env uploaded, then the registered leg
  `host_midi_hitl.py run --preset windowed_content_parity --track 8 --loop-slot 6 --midi-channel 8 --record-bars 24
  --switch-target-slot 5 --serial-port /dev/cu.usbmodem154944801 --out-dir captures/s64-switch-09`. Oracles: the
  target is named on the row (`t=7,s=4,len=18432`), `NOTE_EDIT` is confirmed closed in the same capture
  (`Long press - exited edit mode`), and the switch's own rows (`Queued playback switch`, the hold bound) bound the
  episode. **Episode 03 is the authoritative one**; episodes 01 (aborted at MIDI enumeration after the flash; no
  gesture) and 02 (complete episode, zero drained `#CAP` rows — the delivery defect the two delivery rules answer) are
  recorded beside it.
- **Not claimed**: the first failed transition is reported **as measured**, and the mechanism that remains is a
  code-level candidate with the row it still needs named; nothing about 6.4/6.5 completion, F-7/F-8/AC-8, or content
  parity.
