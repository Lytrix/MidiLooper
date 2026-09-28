# S4f.18 — closure-plan Step 1: preparation/publication boundaries

Status: native and firmware-build verified, 2026-09-28. Baseline `bd7b3bba` plus the
preparation/publication chain. Repairs the six source-verified boundary defects in
[S4F-CLOSURE-PLAN.md](S4F-CLOSURE-PLAN.md) §1 ("Repair preparation and publication
boundaries before dispatch").
Authority: [shared finalization contract](SHARED-CAPTURE-FINALIZATION.md),
DEC-093 — shared capture finalization, DEC-043 — execution bounds.
Tasks: S4f.18 — bounded Record preparation, S4f.20 — atomic publication (both stay
open; production admission/bind is S4f.17/S4f.21).

## G0 discovery / existing-owner decision

VERIFIED against the tree at `bd7b3bba`:

- `RecordSourceStaging::Cursor::begin` and `RecordPreparationCursor::begin` entered the
  **global static** ephemeral seal and kept it for the cursor's whole lifetime, so any
  other track sealing a chunk between two pending grains skipped
  `PersistenceQueue::admitSealedChunk` (`LoopEventStore::sealChunk`).
- `Track::stepDeferredRecordSourceStaging` discarded the cursor and cleared its receipt on
  any `Denied`, so a credit-exhaustion retry re-appended the already-staged prefix. The
  copy was also capped by the caller's initial 8-chunk credit top-up.
- `Track::stepDeferredRecordNormalization` discarded the role on `Denied` while the private
  output could already be ordered, repaired or filter-marked; a re-bind would re-read
  temporary filter markers as original events.
- Both preparation steps rejected an empty private output, so an all-dropped or
  all-filtered Record take could never reach a terminal state.
- `Track::stepDeferredRecordPassTransfer` rejected `output.store.empty()` before consulting
  the owned transfer receipt, although `stepCommittedChunkTransfer` empties the store and
  can still return Pending while reversing the destination directory. The prewarmed
  ownership row was also fixed at `ACTIVE_OVERDUB_TRANSFER_CHUNKS = 8` chunks.
- `Track::stepDeferredRecordPublication` committed the pass and cleared `ownsPendingPass`
  before checking the job-owned geometry receipt, so a missing receipt published content
  with no undo/persistence and no retryable receipt.

G0 PASS: extend the existing cursors, the existing job receipt and the existing prewarmed
admission. No second finalizer, no new storage format, no lowered accepted-input capacity.

## G1 contract / transition oracle fixed before firmware edits

- **Resumable pressure.** `RecordSourceStaging::Step::Pressure` and
  `RecordPreparation::Step::Pressure` mean "the private append could not take another chunk;
  no row was consumed and no mark was invalidated". `Denied` is sticky: the caller must not
  discard and re-bind over a private output that may already hold staged or marked rows.
- **Turn-local seal.** Each private append enters and leaves the global ephemeral seal
  inside the append itself (`appendPrivately`), so the scope is absent whenever a `step`
  returns.
- **Empty terminal.** New `PendingStopPhase::RecordEmpty` and
  `Track::stepDeferredRecordEmptyResult`: no pass, no `RecordPassAdded` undo, no persistence,
  the inline `CommitResult::Skipped` geometry reset only when the slot has no active pass,
  then terminal retirement. The live capture store is deliberately not discarded.
- **Transfer continuation.** The empty-store guard applies only when the pass is not yet
  owned. `Loop::prewarmPreparedPassTransferChunks(chunks)` sizes the next prewarmed ownership
  row from the prepared take at the first transfer grain (cold, idempotent).
- **Publication preflight.** The geometry receipt and
  `canTransferCapturePassWithoutAllocation` are validated before the irreversible commit.

## G2 red-team review

PASS WITH TIGHTENING:

- COVERED: the seal defect is directly observable without a device — a second track's
  256-row capture between private grains must queue its sealed chunk.
- COVERED: pressure/resume is proven by exact row counts, and the sticky-deny path is proven
  by an unchanged marked store across repeated denials.
- COVERED: the empty terminal is proven for both the projection-dropped and the
  minimum-length-filtered shape, including a live successor on another slot and terminal
  retirement.
- COVERED: the transfer continuation is proven with a 24-chunk prepared take that empties the
  store mid-reversal, and the exact destination directory order is pinned.
- COVERED: the publication preflight is proven by a missing receipt that denies with the
  job-owned pass intact, no published pass, no undo and no persistence, then publishes
  exactly once when restored.
- TIGHTENING: the transfer-row capacity admission uses the flash-resident `push_back` path
  because a runtime `std::vector::reserve` instantiation cost 320 B of ITCM and took RAM1
  padding below its reviewed floor (measured 840 -> 712 and restored to 840).

## Fixtures and verification recipe

- `test/test_record_source_staging` (7): `Pressure` resume from a 1-credit reservation over a
  300-row take (pauses at exactly 256 copied rows, resumes to 300 exact rows) and
  seal-absent-after-step assertions.
- `test/test_record_preparation` (18): pressure resume for the synthetic wrap Offs exactly
  once each, and a sticky denial after a pair was already marked that leaves the store
  byte-identical across repeated denials.
- `test/test_shared_capture_finalization` (38): publication preflight, transfer continuation
  and exact directory order, both empty-result shapes with a live successor, and the
  interleaved second-track seal/persistence isolation.

Required: focused native; full `pio test -e native`; `pio run -e teensy41-capture-serial` with
RAM1 padding >= 840; strict OpenSpec validation. No device run, upload or checkoff.

## Completion record

- Focused: shared-finalization **38/38**, preparation **18/18**, staging **7/7** (**63/63**).
- Full native: **2,591 passed / 7 skipped** (2,598), `pio test -e native`,
  00:03:58, no errored suite.
- Capture-serial build PASS: RAM1 padding **840** (approved floor), byte-identical ITCM
  (`code 392,376`), no new ITCM symbol after the transfer-row admission was moved to the
  flash-resident `push_back` path.
- Mutations failing the owning fixture, sources restored byte-for-byte before the final runs:
  M1 holding the seal across turns, M2 publication preflight after the commit, M3 rejecting
  the empty result, M4 denying the owned transfer on the emptied store, M5 a sticky staging
  credit denial, M6 a sticky normalization pressure denial.
- Closeout: runtime state/current work and the S4f.18/S4f.20 task evidence are updated.
  Existing-owner implementation under DEC-093/DEC-043; no new architecture decision,
  exclusion or shipped deliverable. No device acceptance and no parent task checkoff.
- Still open (unchanged, S4f.17/S4f.21): production Record admission and the production bind
  site, so the geometry receipt and the transfer-row capacity are established at that bind
  rather than at the first transfer grain; the deferred revts validation and
  `invalidateCaches()` parity (S4f.20); S4f.19 playback handoff. Record stop is still inline.
