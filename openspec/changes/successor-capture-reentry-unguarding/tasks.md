## 1. Preflight and capacity declaration

- [ ] 1.1 Inventory every refusal that a pending deferred stop currently causes on the reentry path (`Track::startOverdubbing`, `TrackManager::anyTrackRecordingOrOverdubbing`, `TrackManager::setActiveLoopIndex`, `MidiButtonActions::handleToggleRecord`, plus the `TrackCaptureInput.cpp` / `TrackPlaybackWindowBuild.cpp` predicates), and classify each as an ownership precondition to keep or a blanket refusal to replace. Verify: the inventory is written into `CONTINUOUS-CAPTURE-OWNERSHIP.md` with a keep/replace verdict per site.
- [ ] 1.2 Declare the successor capacity (one live interval plus one admitted deferred stop per track) in the ownership doc with its resource justification (two `CaptureAdmissionStaging` roles, one `captureStopOwner`), and confirm the existing explicit denial outcomes (`queue_full`, `storage_denied`) are the ones a capacity press will report. Verify: doc states the capacity and the denial outcomes; `openspec validate successor-capture-reentry-unguarding --strict` passes.

## 2. Track-level unguard (innermost)

- [ ] 2.1 Make `Track::startOverdubbing` admit a new interval while a predecessor's deferred stop is pending, keeping every storage/ownership precondition (active overdub-transfer admission, admitted canonical output, session-exit handoff slot) and removing only the predecessor-pending refusal. Verify: `test_track_reentry_does_not_depend_on_manager_guard` passes for the first time.
- [ ] 2.2 Prove the Track-level invariant the outer guards relied on: after A is admitted and B starts, B owns a distinct live interval (own staging role, own pending notes, own preview, own reservations) and A's job keeps its own. Verify: existing `test_stop_job_and_live_ingress_own_distinct_staging`, `test_stop_job_owns_pending_note_changes`, `test_stop_job_owns_capture_preview` still pass, plus a new assertion set that B's live state is untouched after A's cleanup completes.
- [ ] 2.3 Run the parent suite gate for this step. Verify: `pio test -e native -f test_continuous_capture` shows only the two outer reproducers still red; full `pio test -e native` shows no new failures; `pio run -e teensy41-capture-serial` succeeds and the RAM1 headroom is reported.

## 3. TrackManager unguard

- [ ] 3.1 Stop treating a pending deferred stop as "still capturing" for cross-track maintenance and slot-change decisions that belong to the live interval, so a same-pitch manager-driven reentry is admitted. Verify: `test_manager_reentry_with_a_held_same_pitch_note` passes.
- [ ] 3.2 Prove A's held same-pitch closure cannot consume B's new note: A's original audible NoteOff is retained exactly once and B's physical NoteOff closes B's note only. Verify: a new note-identity assertion in `test_continuous_capture` plus the existing closure-identity tests.
- [ ] 3.3 Re-run the step gate. Verify: `pio test -e native` shows only the Button A reproducer still red; `pio run -e teensy41-capture-serial` succeeds.

## 4. Button A unguard (outermost)

- [ ] 4.1 Remove `MidiButtonActions::handleToggleRecord`'s early return for a pending deferred stop, so Button A starts the successor interval on the same terms as the other routes. Verify: `test_button_a_three_intervals_accept_notes_with_service_paused` passes.
- [ ] 4.2 Confirm the three routes agree: Button A, manager and direct Track admission all admit B with the same outcome and the same denial when capacity is exhausted. Verify: a parameterized test drives all three routes for the admit and the deny case.
- [ ] 4.3 Re-run the step gate. Verify: `pio test -e native` is green apart from previously known unrelated failures (none expected); `pio run -e teensy41-capture-serial` succeeds.

## 5. Capacity, isolation and transport coverage

- [ ] 5.1 Cover explicit capacity exhaustion: a stop beyond the declared capacity is denied with its documented outcome, all previously accepted intervals keep their data/staging/receipts, and a later start is admitted once the predecessor retires. Verify: new native test in `test_continuous_capture`.
- [ ] 5.2 Cover old-job cleanup during live successor capture: A's deferred completion leaves B's pending notes, preview, reservations and track state untouched, and does not force the track's state on B's behalf. Verify: new native test; `test_track_reentry_does_not_depend_on_manager_guard` extended with a completion-during-capture phase.
- [ ] 5.3 Cover Transport Stop with both intervals live: the pending job retargets and completes, B keeps its accepted input, and no stale playback window is published. Verify: new native test asserting the bridge/selected source and B's captured rows.
- [ ] 5.4 Cover the superseded rule under reentry: when a publication advances past an interval's recorded causal predecessor, its capture is committed and no stale bridge is selected. Verify: `test_deferred_stop_records_and_pins_its_causal_predecessor_source` still passes and a reentry variant asserts `kNoBridge`.
- [ ] 5.5 Add the compact reentry telemetry (admission/denial with interval identity and queue depth; reuse versus restart counts) with no per-event capture logging. Verify: the new stage lines appear exactly once per transition in a capture-only run; `git diff --check`.

## 6. Verification and closeout

- [ ] 6.1 Run the full host gate. Verify: `pio test -e native` reports zero failures (the three reentry cases now pass), `openspec validate successor-capture-reentry-unguarding --strict` and `openspec validate resumable-midi-boundary-playback-maintenance --strict` pass, `git diff --check` is clean, and `pio run -e teensy41-capture-serial` succeeds with RAM1 headroom recorded.
- [ ] 6.2 Device acceptance for the reentry interaction: upload the built firmware, run the registered HITL entry point that records A, punches out and immediately reenters B, and confirm B's rows at their original ticks, A's completion, no stale window, and unchanged boot prewarm headroom (PSRAM/heap before and after). Verify: a managed HITL run with a captured result file and a recorded build identity.
- [ ] 6.3 Close out documentation: update `docs/Runtime/CURRENT_WORK.md`, `docs/Runtime/PROJECT_STATE.md`, `docs/DELIVERABLE_TRACKING.md`, the parent change's `tasks.md` (task 2.3 checkbox, now that all slices and the public-route/device evidence pass) and `CONTINUOUS-CAPTURE-OWNERSHIP.md`. Verify: docs describe the shipped reentry behavior, not the guarded one; no durable doc still says a pending finalization refuses a new interval.
