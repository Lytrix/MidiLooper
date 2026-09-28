# S4f.18 — per-track Record preparation role and the normalization step

Status: native and firmware-build verified, 2026-09-28. Baseline `bed90c04` plus the
composition, identity, workspace-union, source-staging and closure-merge slices.
Authority: [shared finalization contract](SHARED-CAPTURE-FINALIZATION.md),
DEC-093 — shared capture finalization, DEC-043 — execution bounds,
[role-admission packet](S4F18-ROLE-ADMISSION-PACKET.md).

## G0 discovery / existing-owner decision

VERIFIED: after the closure merge the private output holds the terminal closure Offs followed
by the projected frozen take, and the phase is `CanonicalOrder`. That is exactly the store the
composition was built for, so the admission slice is binding plus a worker phase — not a new
store, owner, schema or publication path. Two facts decided the placement:

- `CaptureStopJobs` is already externally allocated once per track by
  `prewarmCaptureStopJobs`, and the worker binds the FIFO head so only one job per track
  prepares at a time. The preparation role therefore belongs in that ring: no new allocation
  path, no `Track` DTCM field, no per-slot growth.
- The min-length policy is not per `Loop`: `noteMinLengthTicks` and
  `noteMinLengthRemoveEnabled` are process globals, so the geometry is a job-time snapshot of
  them, not a live Loop read.

G0 PASS: one role per track, one new terminal phase, one bounded step.

## G1 contract / transition oracle fixed before firmware edits

- `RecordPreparation::Role` owns the composition cursor and the **union** workspace (24,576 B)
  plus a binding `Key` (slot, LoopId, interval, frozen generation) and a stable generation
  token, so a rebind can never let a stale cursor publish into a successor's job. It lives in
  the per-track ring; `sizeof(Role)` is 25,736 B and the reviewed ring pin is re-based
  3072 -> **28,808** B with its reason recorded in the `static_assert`.
- `PendingStopPhase::RecordPrepared` is appended to the enum so existing ranges are unchanged.
- `Track::stepDeferredRecordNormalization(job)` validates mode, phase (`CanonicalOrder`),
  owner/LoopId/geometry, the runtime owner and frozen generation, closure completion and a
  non-empty prewarmed output; tops the credits back up before binding; binds once by job key
  with the job's geometry (`openTailCloseTick = UINT32_MAX`, because the inline Record stop
  closes open tails at loop end, one-bar wrap window, the min-length globals) and an identity
  source bound to the job's `Loop`; drives exactly one bounded grain; and on `Complete`
  advances to `RecordPrepared` without publishing.
- The Record worker branch now dispatches `ReservedClosure` -> closure, `RecordSourceStaging`
  -> staging, `CanonicalOrder` -> normalization, and `RecordPrepared` -> no progress (the
  S4f.20 publication slice owns the next transition).

## G2 red-team review

PASS WITH TIGHTENING:

- COVERED: a runtime owner/generation that no longer matches the job denies without touching
  the store; a contract-pinned unit test asserts the store is unchanged after the denial.
- COVERED: the terminal phase refuses to re-run preparation, so a completed job cannot
  double-apply wrap repair or policy.
- COVERED: binding is by job identity, never by active or selected slot.
- COVERED: credits are topped up before binding; a denial performs no mutation and the job
  retries on a later grain.
- COVERED: the merged store proves the composition actually ran through the Track step — the
  pre-normalization order (Off, Off, On, On) becomes canonical (On, On, Off, Off).
- TIGHTENING: adding the composition's TUs to the Track harness must not duplicate what the
  suite already links. `CaptureIncrementalSanity.cpp` is already provided by
  `LoopCaptureTestDeps.cpp`, so only `LoopStopFinalize.cpp` and `RecordPreparation.cpp` were
  added; the first attempt redefined every symbol and the compile caught it immediately.
- TIGHTENING: `RecordPrepared` is a terminal wait, not a silent stall. The worker branch
  names it and returns no progress; publication is deliberately a separate slice.

## Fixtures and verification recipe

`test/test_shared_capture_finalization` (33 cases; one new integrated): drive a controlled
Record job's closure, then staging, then normalization at the worker quantum; assert the
denial of a mismatched runtime binding leaves the store untouched, the terminal phase is
`RecordPrepared`, and the store is canonically ordered with both NoteOn identities preserved.
A further normalization call in the terminal phase denies. Policy depth (repair, removal,
projection, long takes) stays in `test/test_record_preparation` and
`test/test_record_source_staging`, which the step composes unchanged.

Required: focused native suites; full `pio test -e native`; `pio run -e teensy41-capture-serial`
with RAM1 padding >= 840 and the re-based ring pin; strict OpenSpec validation. No device run,
upload or checkoff.

## Completion record

- Focused: shared-finalization **33/33**; with the preparation and source-staging suites
  **57/57**.
- Full native: **2,585 passed / 7 skipped** (2,592), `pio test -e native`,
  00:05:00.046, no errored suite; log `/tmp/normalization-full-native.log`.
- Two counterexamples fail assertions, not compilation, with the source restored byte-for-byte
  before the final runs: the terminal `RecordPrepared` phase not set (the integrated fixture
  fails), and the normalization owner/generation guard removed (the denial fixture fails).
- Capture-serial build PASS: RAM1 padding **840** (approved floor). The role is externally
  allocated, so the 8 x 25,736 B = 205,888 B is PSRAM, not DTCM; the reviewed
  `sizeof(CaptureStopJobs)` pin is re-based 3072 -> **28,808** B with the reason in the assert.
- Closeout: the admission packet's sequence is complete through step 3; runtime state/current
  work, the shared contract and the S4f.18 task evidence are updated. Existing-owner
  implementation under DEC-093/DEC-043; no new architecture decision, exclusion or shipped
  deliverable. No device acceptance and no parent task checkoff; S4f.13 and S7 remain open.
- Next: sequence step 4 — mode-correct atomic publication and retirement (S4f.20), consuming
  the `RecordPrepared` terminal. Record stop is still inline.
