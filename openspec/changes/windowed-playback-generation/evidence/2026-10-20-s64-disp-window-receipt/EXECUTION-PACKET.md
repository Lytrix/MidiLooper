# 6.4 — display-window transition receipt, and a timestamp-ordered read of it (G2 packet)

Authority: task **6.4** (`tasks.md`), the 2026-10-20 ten-run record
([`s64-multi-run`](../2026-10-20-s64-multi-run/EXECUTION.md)) that left 6.4 **measured, not qualified**, and the
2026-10-19 device-leg packet ([`s64-windowed-profile-device-leg`](../2026-10-19-s64-windowed-profile-device-leg/EXECUTION-PACKET.md))
whose oracle terms are reused verbatim. This packet is a G2 execution packet for **this slice**, not evidence that 6.4
passed.

## Identity and scope

- **Packet/task IDs:** the 6.4 qualification blocker only. Lane terms (F-7 boundary, F-8 servable-while-demoted, AC-8
  receipts) are **not touched**: no lane check, no `oracle_complete` term, no deferral changes.
- **Approved contract:** preset `windowed_content_parity` / `STIM-WINDOW-CONTENT-01@v1` (track 8, slot 4, 24 bars),
  unchanged and never edited to chase a condition. DEC-149 records the two design decisions this slice adds.
- **Status:** device verified (three consecutive greens) — 6.4 checkoff itself remains a separate judgement.
- **Owner and path:** `DisplayManager` owns the detailed window; the receipt is raised by
  `DisplayManager::observeDetailedWindowTransition` (window owner, display frame cadence) and formatted/emitted by
  `DebugSessionCapture::displayWindowTransition` (capture owner). `DisplayManager::update` reaches it through the
  existing flash-resident per-update capture hook `checkDmaStagingDiagnostics` — see "RAM1" below.
- **Primary files:** `include/Utils/DebugSessionCapture.h` (`SC_DISP_WINDOW_TRANSITION` + declaration),
  `src/Utils/DebugSessionCapture.cpp` (PROGMEM format, direct-or-ring delivery), `include/DisplayManager.h`,
  `src/DisplayManager.cpp` (hook), `src/DisplayManager/DisplayNoteWindowGather.cpp` (sampler + dedupe key),
  `linker/imxrt1062_t41_lcr.ld` (`.rodata` carve-out), `test/test_capture_ring` (Tier-A pin),
  `scripts/hitl/verify/display_window.py` and `scripts/test_long_loop_display_serial_verify.py` (host read + tests).
- **Preconditions present:** the 6.4 recipe, the registered preset and stimulus, the leg scenario with the 2026-10-20
  host-tooling fixes, a running device with no shutdown latch.
- **Non-goals (explicit):** no `DISP` retuning and **no bulk `DISP` streaming**; no capture flush-policy change; no lane
  tier change; no oracle term added, removed or weakened; no firmware behaviour change to what the display paints or
  when it follows.

## Oracle fixed before behavior edits

The oracle is the existing 6.4 leg oracle. Its terms, unchanged:

1. A bounded-window row exists with the long-loop length and `state=PLAYING`.
2. **Freeze:** while the NOTE_EDIT overlay is open and no gesture is in flight, the held window start does not change.
3. **Snap:** the play/stop long press leaves the frozen window position.
4. **Hold:** the play/stop hold moves the window (≥3 rows, ≥2 distinct starts), else the indirect release-duration
   witness.
5. `window_bars` within 1..16 and `loop_len ≈ 24 bars` on every row read into a bracket.

**What this slice adds is a data source, not a relaxation.** The receipt reports the window the display holds
(`detailedWindowStartTick_`/`detailedWindowBars_`, i.e. the values the next paint resolves from) plus what the display
is showing (track state, edit-session mode, note-session open, loop length). Counterexamples that must still fail, and
do (host-pinned):

- a receipt row with a second window start inside the freeze → `window_not_frozen_during_note_edit`;
- a snap that never leaves the frozen position → `long_press_snap_did_not_move_window`;
- a hold with no window movement and no release witness → `insufficient_disp_during_play_stop_hold`;
- a run with no playing row at all → `missing_playing_bounded_window_disp`.

**Bracket precision (declared, measured, not tuned to a pass):** the freeze bracket ends where the snap **gesture**
begins (`Button Processor: Ch16 Note40 ON`), not where the long press is detected. While the play/stop button is held
the window follows by design (`shouldAutoFollowDetailedWindow` includes `isPlayStopButtonHeld()`), which the receipt
made visible for the first time (window 4546 held through NOTE_EDIT, 6144 once the button went down, and the long press
re-centres on that same 6144 in the same take). The snap term therefore compares against the **frozen** position. Both
directions are pinned by tests, including the "window never moved" failure.

## Controls and coverage

- **Cost control:** transition-only. The dedupe key is
  `(track, slot, state, noteSession, editSession, loopLength, windowBand, windowBars)`; `windowBand` is the held start in
  whole bars because the follow window recentres continuously (~24 ticks per 63 ms sample). Sampled at 250 Hz only while
  the track **plays**, plus one closing row when it leaves that state. Measured: **15 rows** for a full 24-bar run
  (first draft: 2 757 rows and `tier_a_evicted=1`; run 01 below is that measurement).
- **Delivery:** two rules copied from `readinessReceiptImmediate` — write directly when USB has room, else the ring — so
  the row is not left waiting behind a retained transport buffer.
- **Tier-A:** classified by the header's existing `DIAG,disp_` family (no new literal); pinned through the production
  predicate in `test/test_capture_ring`.
- **RAM1:** a direct call from `DisplayManager::update` measured +16 B of ITCM, exactly the lane image's remaining
  padding above the 840 B floor, so the sampler rides the existing flash-resident per-update capture hook instead. The
  sampler's 25 B of `.data` plus `DisplayNoteWindowGather.cpp`'s read-only strings crossed the DTCM align edge
  (`variables 96,192 → 100,288`); a `.rodata` carve-out for that TU restores `96,192`.
- **Production paths exercised:** the receipt is emitted by the same firmware the runs execute; the verifier reads the
  same lines the capture delivers. No mock, no synthetic serial.

## Verification recipe

- `pio run -e teensy41-capture-serial` and `pio run -e teensy41-capture-serial-window-qualification` — RAM1 budget line
  must stay at or above the approved floor.
- `pio test -e native` (full) and `python3 scripts/check_marker_contract.py 'DIAG,disp_window,'`.
- `python3 -m unittest scripts.test_long_loop_display_serial_verify` and `scripts.test_windowed_content_parity_verify`.
- Device: `.venv/bin/python scripts/host_midi_hitl.py run --preset windowed_content_parity --track 8 --loop-slot 4
  --midi-channel 8 --build-env teensy41-capture-serial-window-qualification --record-bars 24 [--build-upload]
  [--no-boot-reset] --out-dir captures/i5-s64-receipt-24bar-NN`; qualification needs **three consecutive** combined
  greens on one boot, with the first post-flash run excluded as warm-up.
- Evidence path: this directory. Baseline revision `a3c5d051` + this slice's dirty tree.
