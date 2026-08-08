# Bucket 1 — Stabilization: known bugs + verification regime

**Status:** Approved (Jun 12, 2026).
**Intent anchor:** [Authority/PROJECT_INTENT.md](../Authority/PROJECT_INTENT.md) — decision 3 (reliability first). Litmus tests 2 and 3 apply to every fix: the music keeps running, and fixes must not add distraction.
**Scope:** the four known bugs below, plus the start of the automated verification regime. No new features. No refactors beyond what a fix strictly requires (the fader/LED coupling observation is Bucket 2 scope).

## Reducing the at-the-hardware dependency

Agreed approach (Jun 12): the user's role shifts from operator-during-debugging to musician doing acceptance tests. Three levers apply in this bucket:

1. **Native tests per fix (not a closing step):** every bug fix must extract its logic into host-compilable code and leave behind a `pio test -e native` case that reproduces the bug. B1 (length finalization math) and B2 (BPM smoothing math) move entirely host-side this way.
2. **Capture and replay:** the instrumented build records incoming MIDI, clock pulses, gestures, and resulting state transitions during the session (serial or SD). Captures become fixture files replayable through the native harness — one session yields a permanent regression corpus and offline debugging.
3. **Host-side MIDI automation:** the DROID surface is plain MIDI, so a script on the Mac impersonates it — sends gesture/CC sequences to the connected Teensy over USB and asserts on the LED/fader-feedback MIDI coming back. Unattended hardware-in-the-loop; B3 is the primary target.

A fourth lever — display snapshot testing against a fake framebuffer — depends on the core/platform split and is deferred to Bucket 2+.

What stays with the user at the hardware: groove/latency feel, motorized fader physical behavior, the real DROID over USB host, and final per-bug sign-off.

## Bugs, owning code, and what is already known

### B1 — Recording length rounds wrong (adds a bar)

- **Symptom:** starting/stopping a recording adds a bar before or after instead of rounding off.
- **Expected (confirmed by user):** round to the *nearest* bar — stop slightly after a bar line shortens back to it; slightly before extends to it.
- **Owning code:** `Track::stopRecording` (`src/Track.cpp:393`) and `Track::stopRecordingToStopped` (`src/Track.cpp:463`) finalize loop length from `currentTick - startLoopTick`.
- **Step 1:** reproduce on hardware with exact timings (stop just before vs. just after a bar line), then read the finalization math against the reproduction before changing anything.

### B2 — External-clock BPM display takes bars to settle

- **Symptom:** external MIDI clock sync works, but the displayed BPM sometimes takes multiple bars to average out.
- **Confirmed mechanism:** `src/ClockManager.cpp:120-135` — BPM from a 24-interval sliding window is blended `0.02 * new + 0.98 * old` whenever the reading is within 3 BPM of the previous smoothed value. A 2% step per clock pulse converges slowly by construction.
- **Fix direction:** retune or replace the smoothing (the sliding window alone already averages one quarter note). Decide display behavior explicitly: track the source quickly vs. show a stable number. Verify on hardware against a real external clock.

### B3 — Motorized fader feedback stops updating

- **Symptom:** fader position updates back to the hardware work, then stop after a few times.
- **Owning code:** `MidiFaderManager` / `MidiFaderProcessor` / `MidiFaderActions` and the feedback-prevention logic described in [../Guides/FADER_STATE_SYSTEM.md](../Guides/FADER_STATE_SYSTEM.md).
- **Step 1:** reproduce with logging on the fader state machine to capture which state it is stuck in when updates stop. No cause is claimed until the log shows it.

### B4 — Note-move display lag

- **Symptom:** moving MIDI notes sometimes lags the screen update.
- **Owning code:** `DisplayManager` (throttled updates from `main.cpp`), note cache invalidation (`Track::invalidateCaches`, `NoteUtils::CachedNoteList`), `NoteEditManager` move path.
- **Step 1:** reproduce and measure — instrument the update path to find where the time goes (cache rebuild, draw, or throttle) before changing anything.

## Slices (one at a time, in this order)

1. **S1 — Capture session:** structured hardware session reproducing B1–B4, extending [../Guides/MANUAL_TEST_BAR_STEP_BUTTONS.md](../Guides/MANUAL_TEST_BAR_STEP_BUTTONS.md), on an **instrumented build that records fixtures** (MIDI in, clock pulses, gestures, state transitions) — not just written repro steps. Output: repro steps + capture files per bug + any new bugs found. Requires the user at the hardware once; agent prepares the test script and instrumented build. **Done (Jun 12, 2026):** `captures/session_20260612_164054.log` — partial coverage (one record cycle, external clock + NOTE_EDIT fader pass); supplement if B1 edge cases or B3 stall need stronger evidence.
2. **S2 — Host-side MIDI automation script:** Mac-side script that impersonates the DROID over USB MIDI against the connected Teensy (send gestures/CCs, assert on LED/fader-feedback MIDI). First target: drive fader updates in a loop until B3's feedback stall reproduces unattended.
3. **S3 — B2 fix** (mechanism already confirmed; smallest risk). Extract BPM computation host-side + native test with synthetic pulse timestamps, then verify against a real external clock.
4. **S4 — B1 fix** (core workflow: capture; highest musical impact). Extract length-finalization math host-side + native tests for round-to-nearest-bar, replay S1 captures as test input.
5. **S5 — B3 fix** (diagnose from S1/S2 evidence, then fix). Fader state-machine transitions extracted host-side where possible + native test.
6. **S6 — B4 fix** (measure from S1, then fix). Cache-rebuild logic native-testable; draw timing verified on device.
7. **S7 — Regime wrap-up:** fold the capture-replay harness and automation script into the documented workflow; storage round-trip and slot-switch native coverage as stretch.

Each slice: build (`pio run -e teensy41`), native tests (`pio test -e native`), then ask before flashing per the upload rule. Native tests are part of each fix's definition of done, not a separate phase.

## Checkpoint (end of bucket)

- All four bugs verified fixed on hardware by the user.
- Native suite extended and green; new tests cover the fixed logic.
- Decide Bucket 2 vs. Bucket 3 ordering (open decision in PROJECT_INTENT.md).
