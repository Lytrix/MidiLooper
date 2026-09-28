# S4f.18 — bounded missing-identity completion

Status: native and firmware-build verified, 2026-09-28. Baseline `bed90c04` on the existing
feature branch plus the preserved unrelated dirty HITL/doc edits. Supersedes the
"identity is a deny-only caller contract" note in the preparation-composition slice.
Authority: [shared finalization contract](SHARED-CAPTURE-FINALIZATION.md),
DEC-093 — shared capture finalization, DEC-043 — execution bounds.

## G0 discovery / existing-owner decision

VERIFIED: `Loop::sealCapture` completes missing NoteOn identities after its canonical sort
and before wrap repair (`assignMissingNoteIdsInStore` walks the sorted store and calls
`Loop::allocateNoteId()`), so the composition must match that position and order. Record
ingress normally assigns identities at admission, and terminal closure Offs carry their
receipt, but the prepared route must not depend on that being true for every row. The
private filter cursor already gives exclusive ordinal access during `InputReady`
(`readPrivateFilterRow`, `markPrivateFilterPair`), so a sibling write is the smallest
extension; no new cursor, queue, allocation or resident field is needed.

G0 PASS: add one fail-closed private-row identity write and one bounded composition phase.
OPEN after this slice: resource admission and the shared worker/publication handoff; this
slice alone still does not enable deferred Record stop.

## G1 contract / transition oracle fixed before firmware edits

`LoopEventStore::assignPrivateNoteId` writes `id` only when the filter cursor owns the
store in `InputReady`, the row exists, is not filter-marked, is a NoteOn, and currently has
no identity. An invalid `id` is refused. An existing receipt is therefore never overwritten
and a non-note row is never given a note identity.

`RecordPreparationCursor` gains an `AssignIds` phase between the canonical order and wrap
repair. It preflights the same private filter cursor, walks the canonical ordinals in
order, and for each NoteOn with no identity asks a caller-supplied `NoteIdSource` for one.
One row is one bounded unit. A default-constructed (invalid) source denies
`IdentityIncomplete`, as does a source that returns an invalid id or a refused write, so a
missing identity can never be silently published. The phase reuses the existing filter
cursor contract and is discarded before repair, so wrap repair and minimum-length filtering
see the same store they did before with identities now present.

## G2 red-team review

PASS WITH TIGHTENING:

- COVERED: assignment happens in canonical order, matching the legacy normalization order;
  assigning before the sort or in append order changes the identity mapping.
- COVERED: no overwrite. The composition guards before the call and the store guards again;
  a direct store-level fixture exercises the store guard so it is not dead code.
- COVERED: a NoteOff, a ControlChange, a missing ordinal, a marked row and an invalid id
  are all refused, and the refused row is left byte-identical.
- COVERED: the phase is bounded one row per unit and adds no allocation; the cursor still
  holds the private ephemeral seal and the caller's reservation is untouched.
- COVERED: `NoteIdSource` is a two-word borrowed callback; no `std::function`, no
  allocation, no per-slot resident state.
- TIGHTENING resolved: the previous `RepairRead` identity guard was removed rather than
  duplicated — `AssignIds` is now the single identity gate, so the deny is named at the one
  place that can also satisfy it.

## Fixtures and verification recipe

`test/test_record_preparation` (now 17 cases): the new cases cover canonical-order
completion (ids 1..N follow the canonical stream, not the append order), an idle source
when every identity is present (zero calls, zero assignments), an invalid source id denying
with `IdentityIncomplete`, a store-level fail-closed matrix (overwrite, NoteOff,
ControlChange, missing ordinal, invalid id, row unchanged), and an eight-round-style
differential that also completes identities against the legacy helpers on the same rows.

Required: focused native; the private filter/order, record tail-repair and record
minimum-length suites; full `pio test -e native`; `pio run -e teensy41-capture-serial` with
RAM1 padding >= 840; strict OpenSpec validation. No device run, upload or task checkoff.

## Completion record

- Focused composition suite **17/17**; with the private filter/order, record tail-repair and
  record minimum-length suites, **47/47**.
- Full native: **2,576 passed / 7 skipped**, 2,583 total (`pio test -e native`,
  00:01:20.595), re-run against the final source after the unused-member cleanup. Final log:
  `/tmp/identity-final-native.log`; the superseded pre-cleanup run is
  `/tmp/identity-full-native.log` (same counts).
- Three counterexamples fail assertions, not compilation, with sources restored
  byte-for-byte before the final runs: the store overwrite guard removed (caught by the
  store-level fail-closed fixture); the `AssignIds` phase skipped (four fixtures fail); and
  assignment moved before the canonical order (caught by the canonical-order fixture).
- Capture-serial build PASS: RAM1 padding **840** (approved floor), no linked ITCM growth
  (the composition role is still not dispatched). ARM layout re-measured with the real
  firmware flags: `Workspace` **40,960 B** unchanged; `RecordPreparationCursor`
  **1,104 -> 1,120 B** (+16 for the borrowed `NoteIdSource` and the assigned-id counter);
  private order cursor **872 B** and filter cursor **56 B** unchanged.
  *Superseding note (role-admission round):* the composition `Workspace` is now a union
  scratch role of **24,576 B** (wrap repair and minimum-length filtering are disjoint
  phases), pinned by a `static_assert`; the cursor is unchanged at 1,120 B. See
  [role-admission packet](S4F18-ROLE-ADMISSION-PACKET.md).
- Closeout: runtime state/current work, the shared contract status and the S4f.18 task
  evidence are updated. Existing-owner implementation under DEC-093/DEC-043; no new
  architecture decision, exclusion or shipped deliverable. No device acceptance and no
  parent task checkoff; S4f.13 and S7 remain open.
- Next: resource admission and the shared worker/publication handoff. Record stop is still
  inline.
