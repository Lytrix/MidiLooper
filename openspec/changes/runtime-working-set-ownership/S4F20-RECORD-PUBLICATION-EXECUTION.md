# S4f.20 — mode-correct Record atomic publication and retirement

Status: native and firmware-build verified, 2026-09-28. Baseline `bed90c04` plus the
composition, identity, workspace-union, source-staging, closure-merge, preparation-role and
pass-transfer slices. This completes the **publication transaction** for the deferred Record
route; one named item remains before S4f.20 can be checked off.
Authority: [shared finalization contract](SHARED-CAPTURE-FINALIZATION.md),
DEC-093 — shared capture finalization, DEC-043 — execution bounds.

## G0 discovery / existing-owner decision

VERIFIED from the current production code:

- The inline Record stop's side effects live in
  `Track::finalizeCommitSideEffects(Committed, RecordStop|RecordStopToStopped, …)`. It was
  evaluated and **rejected** for the deferred route: it drags the whole inline side-effect surface
  (note-edit `EditManager`, `TrackManager` pass reclaim, the cache walk) into every suite that
  compiles the Track worker, and two of its Record effects are not job-correct —
  `queueDeferredRecordRevts()` reads `getActiveLoop()`, and `invalidateCaches()` is an inline-only
  extra. The publication therefore calls the job-slot-keyed effects directly:
  `TrackUndo::pushRecordPassAdded(track, job.slot, passId)`, `Loop::markDisplayCachesStale()` and
  the persistence helper. `commitPreparedCapturePassAtomic` already invalidates derived views
  through `Loop::notifyCommittedContentChanged`.
- `Loop::commitPreparedCapturePassAtomic(PreparedCapturePassTransfer&, PendingCapturePass&)`
  fills a caller-owned receipt (`transferCapturePass`) and completes the bounded canonical
  handoff. Because the pass belongs to the **job**, not to the Loop producer slot,
  `transferred.retiresLiveCapture` is false and a successor's live capture is not retired.
- Job-keyed, once-only persistence admission already exists:
  `requestLoopSlotPersistAndSaveState(track, slot, heap)`, the same helper the deferred overdub
  handoff uses.
- Retirement of a published job is bounded: `Track::retireJobCaptureSourceGrain` releases one
  frozen-extent grain per call and reserves the descriptor for a future admission.

G0 PASS: no new owner, queue, format or undo kind. The deferred route composes existing owners.

## G1 contract / transition oracle fixed before firmware edits

- `PendingStopPhase::RecordPublished` is appended to the enum. The Record chain is now
  `ReservedClosure -> RecordSourceStaging -> CanonicalOrder -> RecordPrepared -> RecordPassReady
   -> RecordPublished -> (out of the ring)`.
- `stepDeferredRecordPublication(job)` (from `RecordPassReady`): validates mode, phase,
  owner/LoopId/geometry, the runtime owner and frozen generation, and the job-owned pass; performs
  the atomic canonical handoff with a local receipt; then reads the just-committed pass id, pushes
  the job-slot `RecordPassAdded` undo entry, marks the job loop's display caches stale and admits
  persistence once for the job's slot; then advances to `RecordPublished`.
- `stepDeferredRecordRetirement(job)` (from `RecordPublished`): releases the frozen extent one
  bounded grain per call, clears the runtime stop owner under the job identity, clears the job's
  owner/LoopId/mode, returns the ring entry (head advance, count decrement) and resets the
  admission.
- The worker dispatches both for Record jobs; a stale identity denies at every step.

## G2 red-team review

PASS WITH TIGHTENING:

- COVERED: a stale runtime owner or generation denies before the handoff, and a wrong phase or a
  job without a staged pass denies at publication.
- COVERED: the job-owned pass is committed through a job-owned receipt; the live capture is not
  retired, so a successor interval keeps its own input.
- COVERED: persistence is admitted once, keyed to the job's slot, only after the handoff and the
  side effects succeed; a denied handoff admits nothing.
- COVERED: retirement is bounded and returns the ring entry exactly once (mutation M3), and the
  Record undo entry is pushed exactly once (mutation M1).
- NAMED RESIDUALS (S4f.20 stays unchecked):
  1. **Job-owned Record geometry.** `TrackUndo::pushRecordPassAdded` reads the Track's
     record-capture baseline geometry and the live loop geometry. Correct for the inline route and
     for this controlled fixture, but the contract requires the job to own before/after geometry so
     a live successor cannot overwrite it; the protected transaction also stages slot-history
     pruning and redo retirement. The receipt has no production capture site yet because
     **production Record admission does not exist**; it must be captured at that bind site and
     consumed fail-closed here.
  2. **Deferred Record revts validation.** `queueDeferredRecordRevts()` is an inline-path deferred
     MIDI validation and resolves `getActiveLoop()`, so calling it from the job-keyed worker would
     read the wrong slot. It is deliberately not called on the deferred route and moves with the
     job-identity work above.
  3. **`invalidateCaches()` parity.** Not run on the deferred route; the atomic handoff already
     calls `notifyCommittedContentChanged`, and the extra inline cache walk is what drags the
     note-edit surface in.
- TIGHTENING: the harness consequence was measured, not assumed. Referencing
  `pushRecordPassAdded` from the worker keeps the undo-branch helpers alive in suites that compile
  the Track worker, which surfaces three note-edit preview-revision calls and the disabled-pass
  reclaim owner. Three candidate fixes were tried in order: linking the inline side-effect TU
  (pulled the whole note-edit surface in — rejected); putting inert fail-closed stubs in
  `load_harness_stubs.h` (collided with the offline/drain suites, which include that header **and**
  link the real owners — rejected); and defining the four inert stubs in the load suite's own
  translation unit, which is the only suite that links neither owner. The third fix is what landed
  and the whole suite set is green.

## Fixtures and verification recipe

`test/test_shared_capture_finalization` (33 cases; the integrated Record case extended): the full
chain closure -> source staging -> normalization -> prewarmed pass transfer -> publication ->
retirement. Assertions: `RecordPublished`, `loop.passes.hasRecordPass()`, exactly one
`TrackUndo` entry, and a returned ring entry (queue depth back to zero). Earlier denials
(mismatched runtime binding, terminal re-entry, second transfer) still hold.

Required: focused native; full `pio test -e native`; `pio run -e teensy41-capture-serial` with
RAM1 padding >= 840 and the unchanged ring pin; strict OpenSpec validation. No device run,
upload or checkoff.

## Completion record

- Focused: shared-finalization **33/33**.
- Full native: **2,585 passed / 7 skipped** (2,592), `pio test -e native`, 00:01:53.003, no
  errored suite; final log `/tmp/publication-final2-native.log` (earlier runs with link errors in
  firmware-boundary suites are superseded and are not counted as passing).
- Focused with the load suite: **68/68** (load 35 + shared-finalization 33).
- Mutations failing assertions, with the source restored byte-for-byte before the final runs: the
  `RecordPassAdded` undo entry not pushed (M1), the atomic canonical handoff skipped (M2 — the
  publication denies), and the ring entry not returned (M3 — the queue-depth assertion fails).
- Capture-serial build PASS: RAM1 padding **840** (approved floor); the reviewed
  `sizeof(CaptureStopJobs)` pin is unchanged at **28,808 B** — publication uses a local transfer
  receipt and the job's existing `pendingPass`.
- Closeout: runtime state/current work, the shared contract and the S4f.18/S4f.20 task evidence
  are updated. Existing-owner implementation under DEC-093/DEC-043; no new architecture decision,
  exclusion or shipped deliverable. No device acceptance and no parent task checkoff; S4f.13 and
  S7 remain open.
- Next: the job-owned Record geometry receipt captured at the production Record bind site
  (S4f.17 remainder) and consumed fail-closed by this publication, with the protected
  undo/pruning transaction; then S4f.21 caller migration. Record stop is still inline.
