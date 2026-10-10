# S7.5b.4 — deferred record-return source retention (G2 execution packet)

## Identity and scope

- **Task:** `runtime-working-set-ownership` S7.5b.4 — retain the record-return source until the committed source handoff.
- **Owner/path:** `Track::stepDeferredRecordRetirement`, the existing `CaptureStopJob`/`PendingStopAdmission`, and `LoopPlaybackRuntime::serviceFrozenRecordWindow` / existing Track playback entry. No new owner, source representation, publisher, queue, or scheduler.
- **Evidence:** `captures/s64-37c-01/host_midi_hitl_windowed_post_stop_drain_20261006_224412.log`: `RECS` stop at ~94.923 s; pass/job retirement and descriptor rearm around 100.46–100.49 s; `VCACHE,idle_src` reports `root_rev=0,playback_rev=1` after retirement; `snapshot_unit,publish` occurs at ~114.585 s; the two-bar current-source playback build starts at phase 6574 / completes around 114.622 s. User reports the first bar is audible and the following ~7 bars are not. `MO` rows are lossy under capture shedding; the state/publication rows plus the user's observation are the basis, not absence of `MO` rows alone.
- **Relevant authority:** runtime spec “Prepared first bar survives deferred record publication”; DEC-081 — audible playback never gates persistence; DEC-108 — readiness is completed publication; DEC-114 — retained-source interval path. DEC-055 — no second whole-loop NOTE_EDIT representation is unchanged and unrelated.
- **Non-goals:** changing first-bar content/geometry, pausing persistence, changing save format or StopAdmissionContract, adding a scheduler/manager/lease/source model, blanket silence, or changing NOTE_EDIT/queued-slot behavior.

## Oracle fixed before behavior edits

Production-path fixture: `test_shared_capture_finalization`, using its real Record stop/finalization/publication/retirement path, controlled `Loop::stepPendingSnapshotPublish`, frozen `CaptureRange`, active Track MIDI entry and `MidiSendCapture` boundary.

1. **Stale-root state:** Record pass is committed; `PendingStopPhase::RecordPublished`; first-bar return view is bound; `publishedSnapshotIsCurrent() == false`; no frozen onward build is active. One retirement grain must return `Pending` and preserve job identity, `captureStopOwner`, raw-range ownership, and the first-bar view. The pre-fix behavior is expected to retire here.
2. **Beyond bar one:** while snapshot publication is deliberately held, drive production MIDI ticks through successive bars. The bar-1 event comes from the prepared return view; events in later intervals come from the job's frozen source. Compare ordered MIDI to an independent expected sequence: every expected pair once, no early/duplicate retrigger, no silence at interval handoff. After every interval assert the same job/source identity remains owned.
3. **Publication/source handoff:** advance the real snapshot publisher to the committed playback revision. Retirement remains pending until `publishedSnapshotIsCurrent()`. Then the current committed source takes over through the existing Track playback entry/fallback; exercise the next due interval while derived windows are rebuilding, and assert exact MIDI parity. Only after that source handoff may the same job retire once and release the raw range.
4. **After retirement:** continue through the next interval and a wrap; compare exact MIDI and assert no gap, duplicate first-bar replay, or stale-source read. Persistence work remains independently eligible throughout.
5. **Controls:** if the current committed snapshot is already ready at `RecordPublished`, retirement completes without an artificial delay. A stale/invalid replacement never authorizes early source release. Existing bounded stale-build cleanup remains bounded and cannot release the last audible source while the committed successor is unavailable.
6. **Red-proof:** restore the old “retire when no frozen build is currently active” predicate; the stale-root test must fail because the raw source/job is released before onward coverage is produced.

## Bounds and controls

- Reuse existing `TrackCaptureStopTestAccess`, `MidiSendCapture`, snapshot publication stepping, and deferred Record fixture controls. Add only the smallest read-only production observation needed to assert source ownership/readiness.
- Preserve fixed/windowed coverage limits and the existing frozen-source grain quantum. Do not assert a new microsecond or bar-count budget; use `Loop::publishedSnapshotIsCurrent()` and the existing declared playback deadline contract.
- Preserve DEC-081: snapshot/window work and persistence continue concurrently; the test must not stop transport or save to obtain the playback result.
- Scope is one Track owner and one deferred Record interval; no other track, overdub, NOTE_EDIT, or queued-start behavior changes.

## Verification recipe

- Baseline/focused: `pio test -e native -f test_shared_capture_finalization` (case IDs added by this slice; require the new delayed-publication case present and passing).
- Broad: `pio test -e native`; `pio run -e teensy41-capture-serial`; `pio run -e teensy41-capture-serial-window-qualification`; inspect RAM1 guard.
- Docs: strict OpenSpec validation for `runtime-working-set-ownership`; `git diff --check`.
- Device: not part of this implementation gate. The prior authorized run `captures/s75b-device-20261007` did not reach a record leg (clear was refused by `waiting_commit_gate`); do not treat it as evidence for this fix. No upload without authorization.

## Completion record

- **Before implementation:** specified; baseline shared-finalization suite **64/64** passed before adding the new case.
- **Implementation:** `Track::stepDeferredRecordRetirement` now waits for the current committed snapshot when a servable frozen record view is still required; the existing `Snapshot` owner receives its bounded turn while that RecordPublished job is pending. The frozen range and return view remain owned until source currency.
- **Regression fixture correction:** restored `TrackCaptureStopTestAccess::transportStop()` in `test_the_successor_take_commits_with_content_after_a_transport_stop`; its omission caused the diagnostic `capture=2, active=1, length=0, job=0` because no stop was requested. This was fixture-only, not a production defect.
- **Verified:** `pio test -e native -f test_shared_capture_finalization` **65/65**; `pio test -e native -f test_runtime_working_set_ownership` **177/177**; `pio test -e native` **2,859 cases / 2,852 succeeded / 7 skipped / 0 failed**; strict OpenSpec validation valid; `git diff --check` clean. Both capture builds passed after the implementation (recorded in session verification).
- **Device / review:** device acceptance NOT RUN; the earlier authorized setup was blocked before the record leg at `waiting_commit_gate`. No new upload, flash, or device claim. Independent lifecycle review was completed earlier in this implementation slice with no concrete defect; its test-oracle strengthening was incorporated.
