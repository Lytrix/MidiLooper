# Loop Snapshot Pitch Bar Span Index Refinement

**Status:** Proposed — OpenSpec Phase 0 documentation only
**OpenSpec:** [`immutable-loop-snapshot-passes`](../../openspec/changes/immutable-loop-snapshot-passes/)
**Parent:** [`loop_pass_snapshot_effective_state_refinement.md`](loop_pass_snapshot_effective_state_refinement.md)
**Decision to amend:** [DEC-044](../DECISION_LOG.md#dec-044-immutable-loop-snapshot-model-replaces-maintained-lcr-direction)

## Intent

Close OpenSpec task 1.6 by selecting the persistent reference structure:

```text
same pitch
    → bars the incoming span intersects
    → NoteRef list in those bars
    → existing overlap resolve
    → publish only dirty bars
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
are reused.

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
    candidates = {A, C}          // not the rest of the loop
    resolve → C' and B
    new bars: 4 [A, C', B], 5 [A, B]
    reuse:    bars 2–3 unchanged
```

Wrap-crossing notes keep the existing two linear segments. Each segment
inserts `NoteRef`s only into the bars it intersects.

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
3. Select bars the incoming span intersects.
4. Collect NoteRefs from those bars; dedup by NoteId.
5. existingNoteOverlapsIncomingHold / resolveConstrainedGeometry.
6. Allocate new immutable objects only for added or changed notes.
7. Remove, replace, or insert NoteRefs in the dirty bars only.
8. Reuse unchanged pitch/bar lists.
9. Freeze and publish the new LoopSnapshot directory.
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
- Bound construction operations 1–5 to “pitch + intersecting bars,” not
  path depth.

### [`specs/immutable-loop-snapshots/spec.md`](../../openspec/changes/immutable-loop-snapshot-passes/specs/immutable-loop-snapshots/spec.md)

Add/replace requirements:

- Persistent reference root **is** pitch → bar → `NoteRef` (notes) and bar →
  `EventRef` (non-notes).
- Successor construction copies only dirty pitch/bar lists.
- Multi-bar membership for intersecting spans; wrap uses two linear segments.
- Span index is not the event index and not open-at-S.
- Incoming mutation selects same pitch, then intersecting bars, before resolve.
- Consumers select a bar range from the snapshot before `IntervalProjection`.

### [`proposal.md`](../../openspec/changes/immutable-loop-snapshot-passes/proposal.md),
[`ARCHITECTURE-REVIEW.md`](../../openspec/changes/immutable-loop-snapshot-passes/ARCHITECTURE-REVIEW.md),
[`tasks.md`](../../openspec/changes/immutable-loop-snapshot-passes/tasks.md)

- Phase 0 north star names this structure.
- Mark 1.6 done: structure selected; remaining work is formal reassessment
  (1.1) and later measurement of one dirty-bar successor unit.
- Phase 2 stays blocked on 1.1 approval and an `ExecutionBounds.md` row for
  dirty-bar construction. It is no longer blocked on “pick a structure.”

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
  reference structure.
- Overlap is specified as span membership, not On/Off-per-bar pairing.
- Unchanged bars are reused; dirty bars are the successor copy unit.
- Phase 2 is still not authorized.
- Formal architecture reassessment (task 1.1) remains open.

## Apply order

1. Patch `design.md` selection + rejected alternatives.
2. Patch `immutable-loop-snapshots` requirements and scenarios.
3. Align proposal, architecture review, and tasks 1.6 / Phase 2 gate.
4. Amend DEC-044, parent refinement, NAMING, Storage, CURRENT_WORK.
5. Stop. Do not start Phase 1 firmware.
