# S4f.20 — job-owned Record geometry receipt

Status: native and firmware-build verified, 2026-09-28. Baseline `bed90c04` plus the
composition, identity, workspace-union, source-staging, closure-merge, preparation-role,
pass-transfer and publication slices. Closes residual 1 of the publication slice.
Authority: [shared finalization contract](SHARED-CAPTURE-FINALIZATION.md),
DEC-093 — shared capture finalization, DEC-043 — execution bounds,
[S4f.20 publication evidence](S4F20-RECORD-PUBLICATION-EXECUTION.md).

## G0 discovery / existing-owner decision

VERIFIED:

- `UndoLoopGeometry` is the three-scalar aggregate `{loopLengthTicks, startLoopTick,
  loopStartTick}`; `Track` already captures the record-start baseline into
  `recordCaptureBaselineGeometry_` / `hasRecordCaptureBaselineGeometry_`, and the inline
  `TrackUndo::pushRecordPassAdded` consumes it. `src/Track/TrackCaptureInput.cpp` sets it at
  record start.
- The deferred publication was reading that Track field (through the inline push), which
  belongs to whichever record start ran last. A successor's record start therefore rewrote the
  predecessor's undo baseline — exactly the defect the contract names.
- `GlobalUndoStack::entries` is the published undo store; a fixture can read the tip entry's
  geometry directly, so the defect is observable without a stand-in.

G0 PASS: extend the existing job receipt and the existing undo owner. No new owner, stack entry
kind, storage format or publication path.

## G1 contract / transition oracle fixed before firmware edits

- `CaptureStopJob` gains `recordBeforeGeometry`, `recordAfterGeometry`,
  `recordGeometryLoopId` and `hasRecordGeometry` (+32 B per job; the reviewed ring pin is
  re-based 28808 -> **28936** with its reason in the `static_assert`).
- `Track::captureRecordGeometryReceipt(job)` runs at the Record bind: it takes the record-start
  baseline (or the current loop geometry when none was recorded) as the job's **before**
  geometry, records the job's LoopId, marks the receipt present, and **consumes** the Track
  baseline so a successor cannot rewrite it and the inline route cannot double-consume it.
- `TrackUndo::pushRecordPassAdded(track, slot, passId, loopId, before, after)` is the job-owned
  overload: it uses the supplied geometry and LoopId and never reads the Track baseline. The
  existing single-argument entry point remains the inline route and now delegates to it.
- `stepDeferredRecordPublication` **fails closed** without a receipt whose LoopId matches the
  job, then stamps the job's after geometry from its own loop at publication and pushes the
  entry through the job-owned overload.

## G2 red-team review

PASS WITH TIGHTENING:

- COVERED: the defect is directly observable. A fixture seeds a successor record baseline after
  the bind; the published entry's before geometry must still match the job receipt, and a
  mutation that reads the live Track baseline publishes `4242` instead and fails.
- COVERED: a missing receipt denies at publication rather than falling back to live state
  (mutation M2: the bind not recording the receipt makes the publication deny).
- COVERED: a receipt for a different LoopId denies, so a replaced slot cannot consume it.
- COVERED: consuming the baseline at bind keeps the inline route's single-consume semantics for
  jobs that never publish through this route.
- TIGHTENING: the after geometry is stamped from the job's own loop at publication, not from the
  selected slot, so a successor's geometry cannot leak into this interval's undo entry.

## Fixtures and verification recipe

`test/test_shared_capture_finalization` (33 cases; the integrated Record case extended): the
controlled bind now calls the production capture helper; the fixture then seeds a successor
record baseline and drives the full chain to publication, asserting the tip `RecordPassAdded`
entry's before geometry equals the job receipt (and is not the seeded successor value) and that
its after geometry is the job loop's.

Required: focused native; full `pio test -e native`; `pio run -e teensy41-capture-serial` with
RAM1 padding >= 840 and the re-based ring pin; strict OpenSpec validation. No device run, upload
or checkoff.

## Completion record

- Focused: shared-finalization **33/33**.
- Full native: **2,585 passed / 7 skipped** (2,592), `pio test -e native`,
  00:03:30.541, no errored suite; log `/tmp/geometry-final-native.log`.
- Mutations failing assertions, with the source restored byte-for-byte before the final runs:
  the publication reading the live Track baseline (M1 — the seeded successor geometry appears in
  the entry) and the bind not recording the receipt (M2 — the publication denies).
- Capture-serial build PASS: RAM1 padding **840** (approved floor); the reviewed
  `sizeof(CaptureStopJobs)` pin is re-based 28808 -> **28936** B for the +32 B job receipt
  (+128 B per track across the four descriptors).
- Closeout: runtime state/current work, the shared contract and the S4f.20 task evidence are
  updated. Existing-owner implementation under DEC-093/DEC-043; no new architecture decision,
  exclusion or shipped deliverable. No device acceptance and no parent task checkoff; S4f.13 and
  S7 remain open.
- Remaining before S4f.20 can be checked off: the deferred Record revts validation (it resolves
  the active loop) and `invalidateCaches()` parity, plus the production Record bind site that
  will call `captureRecordGeometryReceipt` once S4f.17 record admission exists. Next: S4f.21
  caller migration. Record stop is still inline.
