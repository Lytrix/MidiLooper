# S4f.18 — bounded Record source staging

Status: native and firmware-build verified, 2026-09-28. Baseline `bed90c04` on the existing
feature branch plus the preserved unrelated dirty HITL/doc edits.
Authority: [shared finalization contract](SHARED-CAPTURE-FINALIZATION.md),
DEC-093 — shared capture finalization, DEC-043 — execution bounds.

## G0 discovery / existing-owner decision

VERIFIED: the frozen take rows live in the job's `CaptureRange` (`job.rawRange`) and are
readable one row at a time through the generation-qualified `CaptureRange::read`
(O(1), chunk/event indexed, `atEnd` per chunk). The legacy stop projection is already a
static per-row policy, `LoopEventStore::projectRecordStopTick`: a NoteOn at or beyond the
final length is remapped by `tick %= loopLength`, and every other row at or beyond it is
dropped; a zero length copies unchanged. The full-store form
(`mapNoteOnsAtOrBeyondTickIntoLoop`) is content-sized and therefore unusable on the stop
path.

Also VERIFIED, and it corrected the admission packet: `Track::stepDeferredRecordClosure`
requires `canonicalCaptureOutput.store` to be **empty** and appends only terminal held-note
Offs. The prepared take must therefore be built from the frozen raw rows first, with the
closure Offs merged afterwards — see the corrected sequence in the
[role-admission packet](S4F18-ROLE-ADMISSION-PACKET.md#g0-correction-and-slice-sequence-2026-09-28).

G0 PASS: add one bounded staging cursor. No new owner, allocation, ordering array or
publication path. Ordering, closure merging and identity remain later, separate grains.

## G1 contract / transition oracle fixed before firmware edits

`RecordSourceStaging::Cursor` binds one immutable sealed `CaptureRange` and one private
prewarmed output plus the caller's `ChunkReservation`, then copies row by row:

- one bounded unit is one source row or one chunk transition; `lastStepUnits()` never
  exceeds the quantum;
- a row the projection refuses is counted and dropped without touching the output;
- an accepted row is appended only through `appendReserved`, so the cursor never allocates
  and never grows the directory;
- `begin` holds an ephemeral seal for the copy so no staged chunk is admitted to
  persistence, and releases it on `Complete`, `Empty`, `Denied` and `discard`;
- `begin` fails closed (`SourceUnavailable` / `OutputUnavailable`) before any mutation, and
  a stale source generation denies mid-copy with `SourceUnavailable`.

The source is never mutated or released by the cursor; the caller keeps ownership.

## G2 red-team review

PASS WITH TIGHTENING:

- COVERED: the projection is the existing per-row policy, not a reimplementation; a
  zero-length interval copies unchanged, matching the legacy early return.
- COVERED: privacy is asserted on a copy large enough to seal real chunks, so the ephemeral
  seal is genuinely covered rather than vacuously true.
- COVERED: credits exhaustion denies mid-copy with `OutputUnavailable`; no fallback
  allocation and no partial-publication claim (the output stays private).
- COVERED: boundary rows are pinned at tick == length (remapped to 0) and at tick > length.
- COVERED: a fully-dropped source reports `Complete` with an empty output, distinct from a
  source that never had rows (`Empty`).
- TIGHTENING resolved: the first attempt asserted privacy on a four-row copy whose chunk
  never sealed, so removing the seal was not observable. The assertion moved to the
  600-row case where chunks really seal; the mutation is now caught.

## Fixtures and verification recipe

`test/test_record_source_staging` (7 cases): in-loop passthrough with identity preserved;
overflow remap plus non-NoteOn drop; zero-length passthrough; empty source versus
all-dropped; stale source generation; unprepared output and exhausted credits; bounded
units at Q=1/7/64 with unchanged directory capacity, unchanged pool occupancy and every
sealed chunk `NotScheduled`.

Required: focused native; full `pio test -e native`; `pio run -e teensy41-capture-serial`
with RAM1 padding >= 840; strict OpenSpec validation. No device run, upload or checkoff.

## Completion record

- Focused suite **7/7**.
- Full native **2,583 passed / 7 skipped** (2,590), `pio test -e native`,
  00:05:11.879; log `/tmp/source-staging-final-native.log`.
- Two counterexamples fail assertions, not compilation, with the source restored
  byte-for-byte before the final runs: the projection removed (row counts and inventories
  fail in two fixtures); the ephemeral seal removed (the sealed 600-row copy is admitted,
  and the privacy assertion fails with `Expected 0 Was 1`).
- Capture-serial build PASS: RAM1 padding **840** (approved floor), no linked ITCM growth
  (the cursor is not dispatched yet). The new TU compiles into the image as before.
- Closeout: the admission packet was corrected, and runtime state/current work plus the
  S4f.18 task evidence are updated. Existing-owner implementation under DEC-093/DEC-043; no
  new architecture decision, exclusion or shipped deliverable. No device acceptance and no
  parent task checkoff; S4f.13 and S7 remain open.
- Next: merge the closure Offs into the staged store, then the per-track preparation role
  and `stepDeferredRecordNormalization`. Record stop is still inline.
