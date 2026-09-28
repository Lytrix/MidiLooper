# S4f.18 — preparation-role admission and workspace ownership (execution packet)

Status: **revised 2026-09-28 after a G0 correction.** The original packet assumed the
closure output store already held the normalized take; it does not. See
"G0 correction and slice sequence" below. Baseline `bed90c04` plus the composition,
identity and source-staging slices ([composition](S4F18-COMPOSITION-EXECUTION.md),
[identity](S4F18-IDENTITY-EXECUTION.md),
[source staging](S4F18-SOURCE-STAGING-EXECUTION.md)).
Authority: [shared finalization contract](SHARED-CAPTURE-FINALIZATION.md), DEC-093 — shared
capture finalization, DEC-043 — execution bounds.

## G0 discovery / existing-owner decision

VERIFIED from the current production code:

- The Record route owns a job-keyed **closure** output, not the take: the runtime's
  `canonicalCaptureOutput` store is prewarmed, `canonicalCaptureChunks` is an eight-credit
  `ChunkReservation`, and `Track::stepDeferredRecordClosure` appends one terminal held-note
  Off per grain through `appendReserved`. It **requires that store to be empty** and never
  copies the frozen raw rows, so the closure store alone is not a preparable take.
- The frozen take rows live in the job's `CaptureRange` (`job.rawRange`) and are readable
  one row at a time through the generation-qualified `CaptureRange::read`, with the stop
  projection already exposed as `LoopEventStore::projectRecordStopTick`.
- `canonicalCaptureOutputAdmitted()` requires the store empty, `reservedAppendReady()` and
  the full eight credits, so closure and preparation cannot overlap a successor's borrow.
- The composition cursor and its workspace have **no production owner**: `Workspace` is
  caller-owned and neither `TailRepairWorkspace` nor `MinimumLengthWorkspace` is
  instantiated anywhere in `src/` outside the primitives themselves.
- `CaptureStopJob` is the Track-owned, externally allocated per-interval receipt. The worker
  `Track::processDeferredOverdubStopGrain` binds the FIFO **head**, so at most one job per
  track advances a preparation grain at a time; different tracks interleave.
- `LoopPlaybackRuntime` is per slot and lazily allocated. Adding this role there would be
  per-cold-slot growth, which the contract and the B7 finding already reject.

G0 PASS for the role, with a **corrected sequence**: the prepared take must first be built
from the frozen raw rows and the closure Offs. The missing pieces are (a) bounded source
staging, (b) merging the closure Offs into the staged store, then (c) one bounded
preparation role per **track** holding the composition. No new owner, queue, schema or
publication path is required beyond that.

### G0 correction and slice sequence (2026-09-28)

The original packet treated `canonicalCaptureOutput` as the normalized take. Reconnaissance
falsified that: `beginPrivateFilter`/`beginPrivateTickOrder` need a dense store, and the
closure consumer refuses a non-empty one and writes only terminal Offs. The corrected
sequence, each independently verifiable:

1. **Source staging (landed).** `RecordSourceStaging::Cursor` copies the frozen
   `CaptureRange` into a prewarmed private output, one row per bounded unit, applying
   `projectRecordStopTick` (remap an overflowing NoteOn, drop every other overflowing row)
   under an ephemeral seal. See [source-staging evidence](S4F18-SOURCE-STAGING-EXECUTION.md).
2. **Closure merge (landed).** Reconnaissance refined this: because the composition orders
   the store canonically, the closure Offs can stay first and the frozen take can be staged
   *after* them, so the closure consumer's own contract is unchanged. A Record-only
   `RecordSourceStaging` phase and `stepDeferredRecordSourceStaging` drive the staged copy
   into the same private output and advance to `CanonicalOrder`. See
   [closure-merge evidence](S4F18-CLOSURE-MERGE-EXECUTION.md).
3. **Role + normalization (landed).** `RecordPreparation::Role` and
   `stepDeferredRecordNormalization` now bind by job identity over the merged store and drive
   one bounded grain to the terminal `RecordPrepared`. See
   [preparation-role evidence](S4F18-PREPARATION-ROLE-EXECUTION.md).
4. **Publication** is S4f.20 and stays out of scope.

## G1 contract / transition oracle fixed before firmware edits

Add one externally allocated `RecordPreparationRole` per track (8 total), owned beside the
existing Track job ring and referenced only through the job's identity:

- cursor: `RecordPreparation::RecordPreparationCursor` (**1,120 B** measured ARM);
- workspace: `RecordPreparation::Workspace`, now a union scratch role of **24,576 B**
  (wrap repair and minimum-length filtering are disjoint phases; the tail cursor is
  discarded before the filter begins, and `FilterReset` re-discards it);
- reservation: reuse `runtime->canonicalCaptureChunks` — the same eight credits the closure
  consumer used, because closure and preparation are sequential phases of one job;
- identity source: bound to the job's `Loop` so completion order matches `sealCapture`.

Per-track cost is 25,696 B; eight tracks are **205,568 B**, allocated once from the
external-first allocator at cold init. No RAM1/DTCM field, no per-slot growth, no
allocation at the MIDI edge.

New bounded Track step `stepDeferredRecordNormalization(CaptureStopJob&)`:

- requires `stop.phase == PendingStopPhase::CanonicalOrder`, `job.mode == Record`, a
  complete closure (`job.pendingNotes.empty()`), the runtime owner/generation match, and a
  non-empty output store — otherwise `Denied` with no mutation;
- binds the composition once with the job's geometry (`loopLengthTicks`, the job's
  `phaseTick` as `openTailCloseTick`, one-bar wrap window, the Loop's minimum-length
  policy) and stores the binding in the per-track role under the job's identity;
- drives exactly one bounded grain per call and returns `Pending`/`Complete`/`Denied`;
- on `Complete` advances to the next phase (publication, S4f.20) and never publishes here.
  A denial after binding preserves the receipt and permits exactly-once retry.

The role is bound by **job identity** (slot, LoopId, interval, frozen generation), never by
the active or selected slot, so a successor on another slot cannot consume the obligation.

## G2 red-team review

- COVERED: one role per track is sufficient because the worker binds the FIFO head; two
  jobs on one track cannot be in preparation at the same time. Cross-track independence is
  preserved by eight independent roles.
- COVERED: the union is safe only because the phases are disjoint; `FilterReset`
  re-discards the tail cursor and a `static_assert` pins `sizeof(Workspace)` to the larger
  lane array, so a future field addition cannot silently double it.
- COVERED: closure and preparation share the eight credits sequentially; the credit count
  already bounds synthetic Offs (at most 2,048 rows = 8 chunks), and exhaustion denies.
- COVERED: no allocation at the MIDI edge — the role is allocated once at cold init, and a
  missing role denies the grain instead of allocating.
- TIGHTENING: the external allocation must be attempted at cold init and its failure must
  be a named, non-fatal state (the record route is not admitted yet), not a boot abort.
  Measure the exact PSRAM delta and re-report RAM1 padding.
- UNKNOWN/open: whether publication can be reached in the same grain budget; that is S4f.20
  and is deliberately out of this packet.

## Fixed verification oracle (next slice)

- Fixture: the existing `test_shared_capture_finalization` production closure. Freeze a
  Record job, run closure to completion, then drive normalization at Q=1/7/32 to `Complete`
  and assert exact canonical rows, identities, wrap-repair Offs and minimum-length
  removals against the legacy helpers on the same input.
- Denial matrix: wrong mode, closure incomplete, stale owner/generation, empty output,
  missing role, exhausted credits, and a replaced job identity each deny with no mutation,
  and the obligation survives for exactly-once retry.
- Bounds: every grain reports at most `quantum` units; no directory growth; unchanged
  `usedChunkCount` outside the reservation.
- Required: focused native, full `pio test -e native`, `pio run -e teensy41-capture-serial`
  with RAM1 padding >= 840 and the measured PSRAM delta, strict OpenSpec validation.

## Completion record

Packet only; the union workspace and its `static_assert` are the only landed code.

- Focused composition **17/17** with the unioned workspace (the combined repair +
  minimum-length cases exercise both phases through the shared lanes).
- Full native **2,576 passed / 7 skipped**, 2,583 total (`pio test -e native`,
  00:01:33.889), re-run against the unioned source. Log: `/tmp/union-final-native.log`.
- Capture-serial build PASS: RAM1 padding **840** (approved floor), no linked ITCM growth
  (the composition TU is still not dispatched). ARM `Workspace` **24,576 B** (was 40,960),
  cursor **1,120 B**; `static_assert` pins the union to the larger lane array.
- No mutation is claimed for the union: it is safe by phase order, and the `static_assert`
  is the guard that a future field addition cannot double the role.
- Record stop is still inline; resource admission, publication and caller migration remain
  open. No device run, upload, new decision or task checkoff.
- Next slice: the closure merge (sequence step 2), then the per-track preparation role and
  `stepDeferredRecordNormalization` (sequence step 3). The role's geometry, credits,
  identity and identity-binding contract below is unchanged and already pinned.
