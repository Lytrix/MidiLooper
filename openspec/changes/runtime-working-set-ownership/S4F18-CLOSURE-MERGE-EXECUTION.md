# S4f.18 — Record closure merge and the staging phase

Status: native and firmware-build verified, 2026-09-28. Baseline `bed90c04` plus the
composition, identity, workspace-union and source-staging slices.
Authority: [shared finalization contract](SHARED-CAPTURE-FINALIZATION.md),
DEC-093 — shared capture finalization, DEC-043 — execution bounds.

## G0 discovery / existing-owner decision

VERIFIED: the frozen take is staged into the *same* private output the closure consumer
already writes, so no second store and no change to the closure consumer's contract are
needed. The composition orders the whole store canonically, so appending the take after the
closure Offs is equivalent to prepending it. Two details decided the wiring:

- `PendingStopAdmission::consumeClosureGrain()` sets the generic next phase to
  `CanonicalOrder` when the last closure grain is consumed. That default belongs to the
  overdub route; the Record step must set its own next phase, exactly as the overdub
  `ReservedClosure` branch already overrides it.
- The last closure append returns `Complete` without advancing the phase; the phase is
  advanced by a later call through the "no held notes left" branch. Both places must agree
  on the Record next phase.

G0 PASS: one new Record-only phase and one bounded step, reusing the existing closure output,
reservation and job receipts. No new store, owner, allocation or publication path.

## G1 contract / transition oracle fixed before firmware edits

- `PendingStopPhase::RecordSourceStaging` is appended to the enum so every existing phase
  range comparison is unchanged.
- `stepDeferredRecordClosure` keeps its existing contract and now advances the Record route
  to `RecordSourceStaging`: both when it consumes the last held note and when it is entered
  with none left (idempotent repeat). Its idempotence guard is extended to accept the new
  phase with no pending notes.
- `Track::stepDeferredRecordSourceStaging(job)` validates mode/phase/owner/LoopId/geometry,
  the runtime owner and frozen generation, closure completion (`pendingNotes` empty) and the
  raw range's readiness; tops the output credits back up to the runtime horizon before
  binding; binds the per-track `RecordSourceStaging::Cursor` under a receipt of
  (job descriptor, frozen generation, source generation); drives one bounded grain; and on
  `Complete`/`Empty` advances to `CanonicalOrder`.
- The cursor and its receipt live in the per-track, externally allocated `CaptureStopJobs`
  ring — one job per track stages at a time because the worker binds the FIFO head — so no
  `Track` DTCM field is added.
- The worker dispatches Record jobs before resolving overdub staging: a Record job owns no
  live staging, so the staging-dependent path below it must not be reached.

## G2 red-team review

PASS WITH TIGHTENING:

- COVERED: a runtime owner/generation that no longer matches the job denies without staging.
  The fixture originally incremented *both* sides, which cannot deny; it now breaks one side
  and the guard is genuinely exercised.
- COVERED: a stale or replaced source generation denies through the cursor's own receipt, so
  a mid-copy replacement cannot duplicate rows.
- COVERED: credits are topped up before binding; an unavailable top-up denies without
  mutation and the job retries on a later grain.
- COVERED: the closure Offs stay at the head and the frozen take follows with its original
  NoteIds; the composition orders both later, so the append order is not a semantic claim.
- TIGHTENING: the ring size pin is a reviewed decision. `sizeof(CaptureStopJobs)` is
  re-based 2984 -> 3072 B for the +88 B per-track cursor and receipt, recorded in the
  `static_assert` itself; the post-link RAM1 gate confirms the DTCM padding is unchanged.
- TIGHTENING: the new source file is added to the general Track harness so every suite that
  compiles the production Track TU links the step's dependencies. Placing it only in the
  narrower capture-stop harness left three firmware-boundary suites
  (`test_runtime_working_set_ownership`, `test_storage_manager_load`,
  `test_continuous_capture`) with undefined symbols; the full-suite gate caught that, the
  include moved, and the affected suites then passed **166/166** together.

## Fixtures and verification recipe

`test/test_shared_capture_finalization` (32 cases; one new): drive a controlled Record job's
closure to completion, assert the phase is `RecordSourceStaging` and the store holds only the
closure Offs, assert a mismatched runtime owner denies without staging, then drive staging to
completion and assert the store holds the closure Offs at the head followed by the frozen
take with its original identities and the phase advanced to `CanonicalOrder`. The existing
live-successor closure fixture now pins the same next phase.

Required: focused native suites; full `pio test -e native`; `pio run -e teensy41-capture-serial`
with RAM1 padding >= 840 and the re-based ring pin; strict OpenSpec validation. No device run,
upload or checkoff.

## Completion record

- Focused: shared-finalization **32/32**, source staging **7/7**.
- Full native: **2,584 passed / 7 skipped** (2,591), `pio test -e native`, 00:02:19.786,
  no errored suite; log `/tmp/closure-merge-final-native.log`.
- Two counterexamples fail assertions, not compilation, with the source restored
  byte-for-byte before the final runs: the Record next phase left at the generic
  `CanonicalOrder` (two fixtures fail), and the staging owner/generation guard removed (the
  denial fixture fails).
- Capture-serial build PASS: RAM1 padding **840** (approved floor); `sizeof(CaptureStopJobs)`
  is now **3072 B** (+88 B per track, externally allocated ring, 8 tracks = 704 B), re-based
  deliberately in the pin with its reason. No linked ITCM growth claim beyond the existing
  flash-resident step.
- Closeout: the admission packet's sequence is updated (step 2 landed); runtime state/current
  work, the shared contract and the S4f.18 task evidence are updated. Existing-owner
  implementation under DEC-093/DEC-043; no new architecture decision, exclusion or shipped
  deliverable. No device acceptance and no parent task checkoff; S4f.13 and S7 remain open.
- Next: sequence step 3 — the per-track preparation role and
  `stepDeferredRecordNormalization` over the merged store. The role's workspace is already
  the union scratch (24,576 B) and its placement is pinned in the
  [role-admission packet](S4F18-ROLE-ADMISSION-PACKET.md). Record stop is still inline.
