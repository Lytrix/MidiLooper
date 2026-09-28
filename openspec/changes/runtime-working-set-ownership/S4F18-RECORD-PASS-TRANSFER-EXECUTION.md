# S4f.18/S4f.20 — bounded Record pass transfer

Status: native and firmware-build verified, 2026-09-28. Baseline `bed90c04` plus the
composition, identity, workspace-union, source-staging, closure-merge and preparation-role
slices. Leads into S4f.20 — mode-correct atomic publication.
Authority: [shared finalization contract](SHARED-CAPTURE-FINALIZATION.md),
DEC-093 — shared capture finalization, DEC-043 — execution bounds,
[role-admission packet](S4F18-ROLE-ADMISSION-PACKET.md).

## G0 discovery / existing-owner decision

VERIFIED: the overdub `PrepareTransfer` phase already owns exactly the mechanism Record needs,
and it is mode-neutral. `Loop::beginPreparedCapturePassFromActiveAdmission(Capture& source,
PendingCapturePass& pass, LoopEventStoreCommitCursor& cursor, const uint64_t& generation,
uint32_t sealedAtTick)` stages a pass from any job-owned `Capture&` source into the job's own
receipt using one admitted transfer row, and `Loop::stepPreparedCapturePass` moves its rows in
bounded grains. The overdub worker calls both with `runtime->canonicalCaptureOutput` as the
source — the same object the Record route has just normalized.

G0 PASS: no new owner, receipt, capacity or copy. Record reuses the admitted transfer row and
the job-owned `pendingPass`/`passTransfer`, with its own source and its own terminal phase.

## G1 contract / transition oracle fixed before firmware edits

- `PendingStopPhase::RecordPassReady` is appended to the enum, so every existing phase range
  comparison is unchanged. The chain is now
  `ReservedClosure -> RecordSourceStaging -> CanonicalOrder -> RecordPrepared -> RecordPassReady`.
- `Track::stepDeferredRecordPassTransfer(job)` validates mode, phase (`RecordPrepared`),
  owner/LoopId/geometry, the runtime owner and frozen generation, and a non-empty private
  output; begins the job-owned transfer receipt once and returns after that scalar grain; then
  steps the staged transfer at the worker quantum and advances to `RecordPassReady` on
  `Complete`.
- Nothing is published. The atomic publication transaction — pass, `RecordPassAdded`
  undo/geometry, audible-source handoff and once-only persistence — is S4f.20 completion and
  consumes this terminal. Until it exists, `RecordPassReady` is a named wait, not a stall.
- Capacity comes from the existing prewarmed active-transfer horizon; a missing horizon denies
  before any ownership moves.

## G2 red-team review

PASS WITH TIGHTENING:

- COVERED: the transfer begins exactly once. Re-entering the begin branch is refused by the
  receipt's own source state (mutation M3), so a second begin cannot consume a second row.
- COVERED: a wrong mode, phase, owner, geometry or generation denies without moving ownership.
- COVERED: the terminal phase refuses a second transfer, so a staged pass cannot be replaced.
- COVERED: after the transfer the private output store is empty because its chunks moved into
  the job-owned pass; the pass phase is `Record`, so a Record pass is staged, not an overdub one.
- TIGHTENING resolved honestly: the phase guard and the empty-store guard are *individually*
  redundant for the terminal refusal — removing the phase guard alone is not observable, because
  the empty store denies the same call. The load-bearing behaviour is proven by the ready-phase
  mutation (M1) and the re-begin mutation (M3); no mutation is claimed for the redundant guard.
- TIGHTENING: `RecordPassReady` must not be treated as published. The worker returns no
  progress for it and the phase is named for the next slice.

## Fixtures and verification recipe

`test/test_shared_capture_finalization` (33 cases; the integrated Record case extended):
closure -> source staging -> normalization -> prewarmed transfer horizon -> bounded pass
transfer. Assertions: the terminal `RecordPassReady` phase, `job.ownsPendingPass`, a non-empty
`job.pendingPass.committedChunkIds` with `CapturePassPhase::Record`, an emptied private output
store, and a denied second transfer. The earlier denials (mismatched runtime binding, terminal
normalization re-entry) still hold.

Required: focused native; full `pio test -e native`; `pio run -e teensy41-capture-serial` with
RAM1 padding >= 840 and the unchanged ring pin; strict OpenSpec validation. No device run,
upload or checkoff.

## Completion record

- Focused: shared-finalization **33/33**.
- Full native: **2,585 passed / 7 skipped** (2,592), `pio test -e native`,
  00:02:55.275, no errored suite; log `/tmp/passtransfer-full-native.log`.
- Mutations failing assertions, sources restored byte-for-byte before the final runs: the
  `RecordPassReady` phase not set (M1, the integrated fixture fails) and the transfer receipt
  re-begun every grain (M3, the transfer denies on an empty source and never completes).
  Removing the phase guard alone (M2) is **not** observable and is recorded as such.
- Capture-serial build PASS: RAM1 padding **840** (approved floor); the reviewed
  `sizeof(CaptureStopJobs)` pin is unchanged at **28,808 B**, because this slice reuses the
  job's existing `pendingPass`/`passTransfer`/`ownsPendingPass` receipt.
- Closeout: runtime state/current work, the shared contract and the S4f.18/S4f.20 task
  evidence are updated. Existing-owner implementation under DEC-093/DEC-043; no new
  architecture decision, exclusion or shipped deliverable. No device acceptance and no parent
  task checkoff; S4f.13 and S7 remain open.
- Next: S4f.20 completion — the atomic publication transaction over `RecordPassReady`
  (canonical pass + `RecordPassAdded` undo with job-owned before/after geometry + audible-source
  handoff + once-only persistence + retirement). Record stop is still inline.
