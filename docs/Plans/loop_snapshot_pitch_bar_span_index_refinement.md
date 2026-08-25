# Loop Snapshot Pitch Bar Span Index Refinement

**Status:** Applied — OpenSpec Phase 0 documentation only
**OpenSpec:** [`immutable-loop-snapshot-passes`](../../openspec/changes/immutable-loop-snapshot-passes/)
**Parent:** [`loop_pass_snapshot_effective_state_refinement.md`](loop_pass_snapshot_effective_state_refinement.md)
**Decision to amend:** [DEC-044](../DECISION_LOG.md#dec-044-immutable-loop-snapshot-model-replaces-maintained-lcr-direction)

## Intent

Close OpenSpec task 1.6 by selecting the **logical** reference structure:

```text
same pitch
    → bars the incoming span intersects
    → NoteRef list in those bars
    → existing overlap resolve
    → publish dirty membership + minimum directory path
```

Do not implement firmware. Do not add a new OpenSpec change. Patch the existing
change artifacts so the snapshot model uses this index instead of leaving
“structurally shared” abstract.

## Why this structure

Overlap already matches **pitch + tick**, not channel
([DEC-032](../DECISION_LOG.md) / `accumulatePendingNoteChangesFromSourceNotes`).
Consumers already work in **bars** (`dirtyBars`, 2-bar playback, 16-bar display).
`NoteRef` is already the loop-scoped handle ([NAMING.md](../Authority/NAMING.md)).

A flat `NoteRef` vector copies every reference on successor construction. A
path-copy tree shares history but does not make same-pitch overlap an array
index. Pitch-then-bar membership does both: lookup is local, unchanged bars
remain shared. The physical directory that points at those lists is Phase 1 /
measurement; dirty bars do not automatically share a 1000-entry directory.

Do **not** name this a tile, lane, bucket, or grid. The time unit is **bar**.
The handle is **NoteRef**. The object is **LoopSnapshot**.

## Selected model

A `LoopSnapshot` is a complete directory of **pitch → bar → NoteRef list**
plus a separate **bar → EventRef list** for non-note events.

A note whose span intersects bars 2–5 is referenced in bars 2, 3, 4, and 5.
Query those bars, dedup by `NoteId`, then run the existing overlap owner.

```text
pitch 60
    bar 2: [A]
    bar 3: [A]
    bar 4: [A, C]
    bar 5: [A]

incoming B, pitch 60, bars 4–5
    candidates = {A, C}          // A occupies bar 4; not the rest of the loop
    changed objects: C → C'
    added: B
    unchanged: A                 // inspected, not replaced
    new bars: 4 [A, C', B], 5 [A, B]
    reuse:    bars 2–3 unchanged
```

Wrap-crossing notes keep the existing two linear segments
(`[start, loopEnd)` and `[0, end)`). Each segment inserts `NoteRef`s into
the bars it intersects; both membership sets share one `NoteId`. A query
near the loop boundary unions end-side and zero-side bars, then
deduplicates by `NoteId`.

### Three indexes stay separate

| Index | Bar contains | Use |
|-------|----------------|-----|
| **Span** | `NoteRef`s whose span intersects the bar | Overlap, display membership |
| **Event** | NoteOn / NoteOff / CC whose **tick** is in the bar | Playback emit |
| **Open-at-S** | Sounding identities at tick S | Occupy / ledger |

Do not resolve overlap from On/Off events in one bar. That misses any note
whose On is in an earlier bar. Do not store unmatched open Ons in the span
index. The span list holds complete `NoteRef`s.

A long note costs one `NoteRef` per intersecting bar on that pitch. A bar
grows with same-pitch spans that **intersect that bar**, not with earlier
unclosed Ons. Cap later firmware at candidates per pitch per bar, not at
“open Ons.”

Non-note events are points: one `EventRef` in the bar of their tick. They
do not use multi-bar membership.

## Incoming-note path (spec contract)

```text
1. Existing semantic owner supplies settled mutation (or incoming hold span).
2. Select same pitch.
3. Select bars the incoming span intersects (initial candidate region).
4. Collect NoteRefs from those bars; dedup by NoteId.
5. existingNoteOverlapsIncomingHold / resolveConstrainedGeometry.
6. Dirty bars = incoming bars ∪ bars of every note whose geometry or
   membership the resolver changed.
7. Allocate new immutable objects only for added or changed notes.
8. Remove, replace, or insert NoteRefs in the dirty membership lists only.
9. Newly allocate only those lists and the minimum directory path to them.
10. Freeze and publish the new LoopSnapshot root.
```

Each step must keep a declared `ExecutionBounds.md` class before runtime
publication. Publication remains atomic. No parent-snapshot walk for content.

## OpenSpec edits (this plan’s work)

All edits stay in
[`openspec/changes/immutable-loop-snapshot-passes/`](../../openspec/changes/immutable-loop-snapshot-passes/)
plus the parent refinement and DEC-044 amendment. No firmware.

### [`design.md`](../../openspec/changes/immutable-loop-snapshot-passes/design.md)

- Replace the open “compare three structures” decision with this selection.
- Reject explicitly: flat complete `NoteRef` vector; path-copy tree as the
  overlap index; On/Off-per-bar as the overlap index; carry-in `presentAt`
  as the snapshot root.
- Keep lifetime, provisional vs global undo, redo-branch, and latest-saved
  reboot text already recorded.
- Identify/apply/construct have semantic locality bounded by affected pitch
  + dirty bars. That is not an execution bound. Path-copy remains allowed
  only as a directory implementation.

### [`specs/immutable-loop-snapshots/spec.md`](../../openspec/changes/immutable-loop-snapshot-passes/specs/immutable-loop-snapshots/spec.md)

Add/replace requirements:

- Persistent reference root **is** pitch → bar → `NoteRef` (notes) and bar →
  `EventRef` (non-notes).
- Successor construction newly allocates only dirty membership and the
  minimum directory path; unchanged membership stays shared.
- Multi-bar membership for intersecting spans; wrap uses two linear segments.
- Span index is not the event index and not open-at-S.
- Incoming mutation selects same pitch, then intersecting bars, before resolve.
- Consumers select a bar range from the snapshot before `IntervalProjection`.

### [`proposal.md`](../../openspec/changes/immutable-loop-snapshot-passes/proposal.md),
[`ARCHITECTURE-REVIEW.md`](../../openspec/changes/immutable-loop-snapshot-passes/ARCHITECTURE-REVIEW.md),
[`tasks.md`](../../openspec/changes/immutable-loop-snapshot-passes/tasks.md)

- Phase 0 north star names this structure.
- Mark 1.6 done as **logical** structure selected. Physical directory
  remains Phase 1 / measurement.
- Phase 2 stays blocked on 1.1 approval and an `ExecutionBounds.md` row for
  dirty-bar construction. Long notes can still touch many bars.

### Authority / parent plan

- Amend DEC-044 with the selected structure.
- Update
  [`loop_pass_snapshot_effective_state_refinement.md`](loop_pass_snapshot_effective_state_refinement.md)
  open decision “persistent reference-collection representation.”
- [`NAMING.md`](../Authority/NAMING.md): `LoopSnapshot` reference root is
  pitch/bar `NoteRef` lists. Do not add Tile or Lane.
- [`Storage.md`](../Authority/Architecture/Storage.md): persist logical
  `NoteRef` / `EventRef` membership, not raw pointers.
- [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md): Phase 0 still
  documentation-only; structure is selected.

## Exclusions

- No firmware, no new native test architecture.
- No new overlap owner. `resolveConstrainedGeometry` / overdub pending
  changes stay the semantic owners.
- No change to horizon, projection math, cursor, ledger, or occupy.
- No `presentAt` / LCR maintained effective-state owner.
- No new domain noun.
- Eight-candidate LCR units remain a measurement hypothesis only.

## Exit criteria

- OpenSpec design and specs name pitch → bar → `NoteRef` as the selected
  **logical** reference structure.
- Overlap is specified as span membership, not On/Off-per-bar pairing.
- Unchanged pitch/bar membership remains shared with the parent snapshot;
  only dirty membership lists and the minimum immutable directory structure
  required to reach them are newly constructed.
- Phase 2 is still not authorized.
- Formal architecture reassessment (task 1.1) remains open.
- Physical directory representation is not claimed as solved.

## Apply order

1. Patch `design.md` selection + rejected alternatives.
2. Patch `immutable-loop-snapshots` requirements and scenarios.
3. Align proposal, architecture review, and tasks 1.6 / Phase 2 gate.
4. Amend DEC-044, parent refinement, NAMING, Storage, CURRENT_WORK.
5. Stop. Do not start Phase 1 firmware.
