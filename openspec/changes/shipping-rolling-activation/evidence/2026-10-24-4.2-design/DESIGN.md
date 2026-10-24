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
