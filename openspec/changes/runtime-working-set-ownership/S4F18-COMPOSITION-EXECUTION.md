# S4f.18 — bounded Record preparation composition

Status: native and firmware-build verified, 2026-09-28. Baseline `bed90c04` on the existing
feature branch plus the preserved unrelated dirty HITL/doc edits.
Authority: [shared finalization contract](SHARED-CAPTURE-FINALIZATION.md),
DEC-093 — shared capture finalization, DEC-043 — execution bounds.

> Superseding note (identity slice): the deny-only identity contract described below is
> extended by [S4F18-IDENTITY-EXECUTION.md](S4F18-IDENTITY-EXECUTION.md). The cursor now
> completes missing NoteOn identities in canonical order when the caller supplies a
> `NoteIdSource`, and still denies `IdentityIncomplete` without one. The composition suite
> is now 17 cases; the counts in this file describe the earlier composition slice.
>
> Superseding note (role-admission round): the composition `Workspace` is now a union
> scratch role of **24,576 B** instead of 40,960 B, because wrap repair and minimum-length
> filtering run in disjoint phases; the next production slice is pinned in
> [S4F18-ROLE-ADMISSION-PACKET.md](S4F18-ROLE-ADMISSION-PACKET.md).

## G0 discovery / existing-owner decision

VERIFIED from the current production code:

- `Loop::sealCapture` normalizes inline in this order: `ensureCaptureEventsSorted`
  (legacy `noteEventLess` = tick, then NoteOff < NoteOn < other), assign missing NoteOn
  identities, `finalizeCaptureWrapWindowAtStop`, then
  `removePairsShorterThanNoteMinLength`, then `verifyCaptureHotStop`.
- The bounded primitives already exist: `LoopEventStore::beginPrivateTickOrder` /
  `stepPrivateTickOrder` (stable tick order), `beginPrivateFilter` / `stepPrivateFilter` /
  `readPrivateFilterRow` / `markPrivateFilterPair` / `finishPrivateFilter` (private
  marking and compaction), `LoopStopFinalize::TailRepairCursor` and
  `CaptureIncrementalSanity::MinimumLengthCursor` (ordered-input policy decisions).
- The private order cursor sorts by tick only; it deliberately does not choose an
  equal-tick class tie order. `MinimumLengthCursor` and `TailRepairCursor` require the
  legacy (tick, class) stream, so the composition needs a canonical-key order.
- Record ingress assigns NoteOn identities at acceptance (`TrackCaptureInput`), while
  `sealCapture` keeps a defensive assign step. A job-owned prepared store therefore
  arrives identity-complete for Record; synthetic repair Offs carry no identity in both
  paths.
- The legacy store helper `finalizeWrapWindowOnStore` appends synthetic Offs without
  re-sorting; only `removePairsShorterThanNoteMinLength` re-sorts, and only when it
  removes at least one pair (it reloads a canonically sorted flat copy). The composition
  canonicalizes after repair unconditionally, which is the order a published Record pass
  and its playback view require.

G0 PASS: compose the existing primitives under a new bounded Record preparation role.
No new owner, storage format, undo kind, capacity, queue or production dispatch.
OPEN after this slice: bounded missing-identity assignment, resource admission and the
shared worker/publication handoff; this slice alone does not enable deferred Record stop.

## G1 contract / transition oracle fixed before firmware edits

`RecordPreparation::RecordPreparationCursor` binds one private, prewarmed, dense,
unpublished `LoopEventStore` and a caller-owned `Workspace`. It runs five bounded stages
over that store:

1. canonical (tick, NoteOff < NoteOn < other) private order, stable for equal keys;
2. wrap repair over the ordered stream, appending the synthetic Offs through the caller's
   `ChunkReservation` and holding an ephemeral seal so those rows never schedule
   persistence;
3. canonical re-order after the appends;
4. minimum-length decisions over the re-ordered stream, applied through private pair
   marking;
5. private compaction and tail retirement.

Every stage reports at most `quantum` fixed units. The cursor never reads live producer
geometry, the selected slot, transport or clock; `Geometry` carries the job's receipt.
`current()` re-checks the caller's generation reference every step, so a replaced
preparation role denies instead of preparing a successor's content. A denied or
discarded cursor leaves ownership with the caller, which discards the private output;
the cursor never restores partially mutated private rows.

Identity is a caller contract: a NoteOn with no identity denies with
`IdentityIncomplete` rather than allocating one, so a missing-identity gap cannot be
silently papered over. An emitted repair row with no reservation denies with
`ReservationMissing`. Empty input reports `Empty` once and then `Complete`.

## G2 red-team review

PASS WITH TIGHTENING:

- COVERED: borrowed private storage only; the store cursor ownership checks cover
  generation, pool lifetime, count and directory shape on every touched chunk.
- COVERED: no whole-take mirror, no take-sized ordering array, no per-event bitmap; the
  only scratch is the caller's fixed MIDI-key workspaces.
- COVERED: synthetic rows use reserved physical credits and an ephemeral seal; a
  non-prewarmed store or an exhausted reservation denies instead of allocating.
- COVERED: equal-tick class order and stable equal-key identity are asserted directly.
- COVERED: a stale generation, an identity-incomplete On and a missing reservation each
  deny before terminal publication; retry after restoring the resource publishes once.
- TIGHTENING resolved: `expectRows` originally compared a sorted inventory, which cannot
  detect ordering. The suite now has an order-sensitive `expectRows` and a separate
  `expectInventory` for the legacy differential.
- TIGHTENING resolved: the canonical key must not become a new equal-tick policy. The
  tick-only cursor keeps its existing contract; canonical order is opt-in and the visible
  legacy `noteEventLess` class order is reused unchanged.
- TIGHTENING resolved: the composition is not dispatched by firmware yet, so no ITCM or
  RAM1 growth is claimed for the new role. The only firmware change in the linked image
  is the canonical-order support inside the existing flash-resident order worker.

## Fixtures and verification recipe

New suite `test/test_record_preparation`: real `LoopEventStore`, real tail-repair and
minimum-length cursors, no policy or storage stubs. Cases: passthrough identity; staged
On-before-Off canonicalization; synthetic terminal Off with reorder; both `openTailCloseTick`
branches; wrapped head/tail pair suppression; minimum-length removal and disable; repair
and minimum-length composed on the appended stream (removed and retained variants);
reorder past a later existing row; 2,056-row reversed long take above the overdub staging
cap; empty and zero-length terminals; identity-incomplete and missing-reservation denial;
stale preparation generation; bounded units at Q=1/7/64 with unchanged directory capacity
and pool occupancy; eight randomized differential rounds against the legacy
`finalizeWrapWindowOnStore` + `removePairsShorterThanNoteMinLength` inventory.

Required: focused native; the existing private order/filter/record-tail/minimum suites;
full `pio test -e native`; `pio run -e teensy41-capture-serial` with RAM1 padding >= 840;
strict OpenSpec validation. No device run, upload or task checkoff.

## Completion record

- Focused new suite: **12/12**. Existing primitives re-run: private tick order, private
  filter, record tail repair, record minimum length — **30/30**.
- Full native: **2,571 passed / 7 skipped**, 2,578 total, `pio test -e native`,
  00:03:59.070. Log tail: `/tmp/recordprep-full-native.log`.
- Four counterexamples fail assertions, not compilation, and every source was restored
  byte-for-byte before the final runs: canonical order reduced to tick-only (caught by
  the canonical-order case and the differential); the post-repair reorder skipped (caught
  by the reorder-past-a-later-row case); minimum-length policy skipped (caught by the
  removal case and the differential); the identity guard removed (caught by the denial
  case).
- Capture-serial build PASS: RAM1 variables **96,160**, ITCM code **392,376**, padding
  **840** (approved floor), locals **34,912**. `arm-none-eabi-size` on the same ELF:
  text **1,071,424**, data **82,624**, bss **183,200**. The new composition TU is not yet
  referenced by production, so it contributes no linked ITCM; the canonical-order support
  lives in the existing flash-resident order worker.
- ARM layout measured from the real firmware flags: `Workspace` **40,960 B**
  (tail repair 24,576 + minimum length 16,384); `RecordPreparationCursor` **1,104 B**;
  private order cursor **864 -> 872 B** (+8 for the canonical key); filter cursor
  **56 B** unchanged. These are borrowed/caller-owned, not per-slot resident fields.
- Closeout: runtime state/current work, the shared contract status and the S4f.18 task
  evidence are updated. Existing-owner implementation under DEC-093/DEC-043; no new
  architecture decision, exclusion or shipped deliverable. No device acceptance and no
  parent task checkoff; S4f.13 and S7 remain open.
- Next: bounded missing-identity completion, then resource admission and the shared
  worker/publication handoff. Record stop is still inline.
