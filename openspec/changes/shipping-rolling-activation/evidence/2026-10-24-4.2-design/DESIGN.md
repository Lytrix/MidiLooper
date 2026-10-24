# 4.2 — save checkpoint parity: content oracle and repair design (2026-10-24, rev 2)

**Task:** F-6 (checkpoint parity, save), transferred from `windowed-playback-generation` task 4.2.
**Status:** design only; no firmware line changed. **Repair: B — bounded pre-publication reconciliation** (no LSV1
provenance field). This revision resolves the coverage source, removes the circular comparison, and makes failure
transactional.

## 0. What the code actually persists (the coverage question)

`writeLoopPersisted` writes an LSV1 body from two explicit sources:

- `persistedOracle = loop.sharePassesSnapshot()` — a snapshot derived **solely from the pass state**; written as the
  body's pass state.
- `loopSnapshot = loop.publishedSnapshot()` — the published root; written as the body's root.
- `writeLoopSnapshotBodyFormatV1(io, *persistedOracle, *persistedOracle, *loopSnapshot)`.

The body carries **no coverage figure**. `applyLoadedLoopSlotBodyCommit` applies the persisted pass snapshot first and
then installs the persisted root with `storePublishedSnapshot(…, kSnapshotCoverageCurrent)`; its own comment states
the claim rests on the **writer's** admission and that the body's structure does not prove the root's correspondence to
the passes.

**Coverage is therefore implicit, not persisted.** Both sources are whole-loop snapshot representations, so the
comparison domain is the **whole loop** (more precisely, the intersection of the two snapshots' declared domains; the
fixture must assert that both declare the same loop length). The design MUST NOT require a persisted coverage revision;
revision equality is only a **secondary** consistency check when both persisted identities are from the same domain.
If a future body can carry a partial root, the domain must be reconstructed from an authoritative persisted field; none
exists today, so a partial-root body is conservatively treated as not comparable and is never installed as current.

## 1. Definition (central)

> A checkpoint is coherent iff the persisted root and the persisted pass state **independently project to the same
> canonical committed content over the checkpoint's authoritative coverage**. Any persisted generation/revision
> identities that are specified to survive serialization must also agree.

The claim is **not** correspondence to a historical in-memory root (that identity is not persisted); it is that the
persisted root and persisted pass state encode the same committed checkpoint.

## 2. Pre-publication, source-independent verification

The compare MUST run **before** the new root or the new passes become authoritative:

1. The load job stages the two explicit sources: **persisted-root enumerator** and **persisted-passes-derived
   enumerator** (the body's own pass snapshot; no live-loop install).
2. It compares them over the domain (§3) with a bounded streaming walk.
3. Only on equality does it commit the new passes **and** the new root atomically (§5), then
   `storePublishedSnapshot(…, Current)`.
4. It MUST NOT validate the persisted root by first installing it and then reading `Track::playbackWindowEventAtForSlot`
   / `copyEffectiveCommittedEventsInRange` / any API that resolves through the root/current being validated. Those
   reads are circular for this oracle.

A divergent root is never `Current`, even transiently.

## 3. Canonical equivalence tuple (not raw structs, not "same events")

Compare, in canonical order (the snapshots' committed event order), the tuple:

- **tick**
- **event type** (NoteOn / NoteOff / control)
- **channel**
- **pitch** (note events) or **controller + data** (control events)
- **velocity** (note) / **value** (control)
- **pair/closure relationship** — one NoteOn matched to its NoteOff at the same note identity and the persistent
  closure membership the body records

**Note IDs** are compared only if the persisted representation guarantees they survive reconstruction with the same
identity; otherwise the tuple is the musical/structural identity (pitch + tick + type), so a rebuilt-but-equivalent
root is not falsely rejected. Raw `MidiEvent` structs are never compared (padding/order are not content).

**RED mutation:** change a structurally harmless field first — NoteOn **velocity**, or both members of a note pair's
**pitch**. Do not start with ticks (that would test sorting/index/closure validation rather than checkpoint parity).

## 4. Passes are a reconstruction authority (prerequisite control)

B depends on this, so prove it before building the compare:

- Take an ordinary coherent writer state (passes committed, root published).
- Reconstruct the root **solely from the persisted pass representation** (`sharePassesSnapshot()` on the writer / the
  body's pass snapshot on load), without borrowing the published root.
- Prove the reconstruction equals the writer root over the domain, tuple-for-tuple.

If this control fails, the passes are deltas whose interpretation depends on the root; B would be circular and needs a
different design. The control is a registered native fixture, not an assumption.

## 5. Transactional failure at the slot level

- The **old slot remains wholly authoritative** until the new body has passed parity/reconciliation.
- The load job stages the new pass snapshot and root; on equality it commits **both atomically**.
- On divergence: do not install the new passes or root. Name `OwesReconciliation`; the old slot (passes + root +
  served state) remains intact. If there is no previous slot state, expose **no current checkpoint**.
- Do not "retain the prior valid root" while the new passes are already installed — that is a mixed-generation state.
- A persistent reconstruction failure retains the complete old slot state; it is reported once, never silently
  repaired to the diverged root.

## 6. Repair options

### A. Persisted provenance (format change)

Add the root's coverage/content identity to LSV1. **Cost:** LSV1 v1 is pinned; field/version/compat/rollback. **Limit:**
provenance proves what the writer claimed, not that the root matches the passes; a wrong body with a matching stamp
would pass. Not a correctness mechanism; at most a future fast path.

### B. Bounded pre-publication reconciliation (recommended)

Compare the two staged snapshots (§2) over the domain (§3) with a bounded streaming walk using the existing
continuation machinery; on equality commit atomically; on divergence name stale and keep the old slot. **No format
change; no knowledge of the historical root required.**

### C. A + B

Only if the comparison measures too expensive on device; A is then a fast path on top of B, never its replacement.

## 7. Performance and acceptance

- The two sources are already materialized in the body; the comparison is a **bounded streaming walk** over their
  canonical streams, not a pass rebuild and not necessarily one pass enumeration. The acceptance gate is per load turn:
  **no individual load turn may exceed the existing load-job bound**; if the body is large, the walk uses the existing
  page/cursor continuation. Do not change the bound pre-emptively.
- Native acceptance: §4 control GREEN; the divergent-body RED (velocity / pitch pair) becomes `OwesReconciliation` or a
  rebuilt `Current`, never the diverged root as current; coherent round trip `Current`; unset-coverage/no-root controls;
  two mutations (unconditional-equal, install-anyway) restored byte-identically.
- Device acceptance: load a diverged body from SD on the qualification image; assert the named outcome and the served
  content (passes-derived or no current), never the diverged root; the body remains readable; per-turn bound holds.
- Undo: restore the entry's exact root (4.1); the load reconciliation is at the save/restore edge only.
- Persistence/RAM: no new field; the staged snapshots/scratch are the load path's existing working set; no synchronous
  timing-edge work; rollback is the prior HEX (the body format is unchanged).

## 8. Implementation order (the review's order)

1. **Prove pass-only reconstruction** (§4 control) — if it fails, stop and redesign.
2. **Land the structurally-valid divergent-body RED** (velocity or pitch pair) before any repair.
3. **Move parity before publication** (staged compare, no live install).
4. **Implement bounded compare/reconcile** (§6 B) with the canonical tuple (§3).
5. **Prove transactional failure** (§5: old slot wholly authoritative; atomic commit; no previous → no current).
6. **Device/perf leg** (§7), then record the outcome and the first production consumer of `checkpointParity`.

## 9. Not in this design

- A new persistence format or a provenance field as the correctness mechanism.
- A second content authority or a second reader.
- Revision equality as a correctness primitive (it is a secondary consistency check only).
- The `nextNoteId` counter as a parity signal.

## Step 1 result (2026-10-24) — passes-only reconstruction control: GREEN

Registered `test_4_2_control_persisted_passes_alone_reconstruct_the_writer_root` (`test_storage_loop_io`):
a settled writer loop is committed and published; `sharePassesSnapshot()` is adopted into a **scratch loop with no
published root**, and `resolveDisplayNotes(passesOnly.passes, …)` is compared to `resolveDisplayNotes(writerRoot, …)`.
The note content agrees element-for-element (pitch, start, end, noteId), and `copyEffectiveCommittedEventsInRange`
on the scratch loop reads the **passes**, not the root. So the persisted pass representation is a reconstruction
authority for the committed note projection; the event-tuple/closure domain remains the next control extension.

This gates B: the comparison can be built from the passes-derived projection without borrowing the root being
validated. No firmware line changed; `test_storage_loop_io` 56/56.

## Step 2 result (2026-10-24) — divergent-root RED and the note-level guard: GREEN

- **RED:** `test_4_2_a_divergent_root_is_not_published_as_current` writes a coherent production body, then mutates **only
  the parsed staged root** (both pair members' pitch plus the root note's pitch; passes/IDs/metadata untouched). The
  positive bite check proves the staged root carries 63 while the passes-derived projection still carries 62 before the
  loader is asked to act. Against the old loader the divergent root was published `Current` (the RED).
- **Guard:** `applyLoadedLoopSlotBodyCommit` now compares the two staged sources **before** `applySnapshotToLoop`:
  `resolveDisplayNotes(persistedOracle.passes, …)` vs `resolveDisplayNotes(*loadedLoopSnapshot, …)` (note pitch/start/
  end). Unequal → return without applying either source; a fresh slot exposes no current checkpoint.
- **Tests:** the storage suite **57/57** (control, coherent save parity, divergent refusal); full native
  **3,437 / 7 skipped / 3,430**; both firmware builds at the exact pins (code 391,752 + 1,464 / 391,992 + 1,224).
- **Fixture correction:** `test_lsv1_load_commit_rebuilds_undo_and_preserves_root` previously built a synthetic
  passes/root pair that was not a writer state; the guard correctly refused it, so the fixture now writes a coherent
  production body. This is the invariant the guard introduces.

**Deliberately narrow:** note-level parity only. Velocity/channel/event-tuple/closure comparison, the explicit
existing-slot transactional proof (the guard returns before apply; the test is still owed), the bounded streaming
walk, and the device leg remain. **4.2 is not complete.**

## Step 2 transactional result (2026-10-24) — existing-slot generation intact: GREEN

`test_4_2_a_failed_parity_leaves_the_existing_slot_generation_intact`: a coherent body is loaded into a slot; a second,
independently parsed body whose staged root is pitch-mutated is then refused. The existing slot keeps the **same root
object**, the **same pass id** and the **same parity** (`Current`), and the diverged pitch never appears in its notes.
This proves "apply neither" on an existing slot: the guard returns before `applySnapshotToLoop`, so no mixed
root/passes generation is possible. Storage suite **58/58**; full native **3,438 / 7 / 3,431**.

Still owed in 4.2: the canonical event-tuple/closure comparison (the next slice; it requires a passes-derived
effective-event projection usable on staged passes, because `copyEffectiveCommittedEventsInRange` is a `Loop` method),
the bounded streaming walk, and the device leg.

## Step 3 result (2026-10-24) — event-tuple guard (order-preserving first slice): GREEN

- **Passes-derived projection:** `copyEffectiveCommittedEventsFromPasses(const LoopPasses&, loopLengthTicks, …)` is
  extracted from `Loop::copyEffectiveCommittedEventsInRange` (the method now delegates), so the loader compares the
  **staged** passes without installing them.
- **Guard:** after the note compare, the loader compares the staged root's event stream (`eventMembership` per bar)
  with the passes-derived effective stream on the canonical tuple — tick, type (On/Off/other), channel,
  pitch-or-controller, velocity-or-value — **in their own committed order**. NoteIds and raw structs are not compared.
- **Velocity RED → GREEN:** `test_4_2_a_velocity_divergence_is_not_published_as_current` changes the staged root's
  NoteOn velocity (and note velocity) 100 → 90; the note-level projections still agree (pitch/start/end/noteId), so the
  note guard is blind, and the old loader published it. The event guard refuses. Red-proof: disabling only the event
  comparison makes **exactly** the velocity case fail; the pitch case (note guard) and the transactional case stay
  green. Restored byte-identically.
- **Pins:** both builds SUCCESS — default `391,944 + 1,272`, qualification `392,184 + 1,032` (+192 code from the
  guard; both above the 840 floor). Storage **59/59**; full native **3,439 / 7 / 3,432**.

**Still owed:** same-tick canonical ordering and the bounded streaming walk (the order-preserving compare is the first
slice, not the final canonical sort), explicit closure-edge assertions, the device leg. **4.2 is not complete.**
