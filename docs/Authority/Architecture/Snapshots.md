# Runtime architecture — `LoopSnapshot` (effective state)

**Parent:** [RuntimeArchitecture.md](RuntimeArchitecture.md)

**Naming:** [NAMING.md](../NAMING.md) — **Snapshot** is the act; **`LoopSnapshot`**
is the object. Do not call it a `LoopPass`, a flat MIDI vector, or a tile/lane.

**Authority:** [`immutable-loop-snapshot-passes`](../../../openspec/changes/immutable-loop-snapshot-passes/),
[DEC-044](../../DECISION_LOG.md), [DEC-047](../../DECISION_LOG.md) (+ 2026-09-16
amendment), [DEC-048](../../DECISION_LOG.md), [DEC-057](../../DECISION_LOG.md), and
[ExecutionBounds.md](ExecutionBounds.md) rows 21–22.

---

## Question

> What is the authoritative effective state of a loop, and how is it built,
> published, and consumed without a full-loop rebuild on a hot path?

`LoopSnapshot` is the complete immutable effective-state object. `LoopPass`
records remain the historical/compatibility operation rows; they are not the
effective authority. Each consumer keeps its own derived representation and
build policy ([DerivedViews.md](DerivedViews.md)); those representations read a
selected snapshot and never reconstruct missing content from a parent.

---

## Owner and lifetime

| Concern | Owner |
|---|---|
| Publication and identity | `Loop` (`storePublishedSnapshot`, `publishedSnapshot`, `publishedSnapshotIsCurrent`, `publishedSnapshotIdentity`) |
| Compatibility pass rows | `LoopPasses` (`recordPass`, `overdubPasses`, `editPasses`) |
| In-flight candidate | `Loop::pendingSnapshotPublishBox_` → a frozen, unpublished `LoopSnapshot` |
| Object storage | snapshot slab allocator, 32 KB slabs ([DEC-048](../../DECISION_LOG.md)) |

- **Published root** — the selected snapshot. It is the source for normal
  consumers at its `playbackRevision`. `publishedSnapshotIsCurrent()` is the
  freshness test; `publishedSnapshotIdentity()` is the identity consumers stamp.
- **Unpublished candidate** — private and unselected. The previous published root
  stays selected until the candidate is complete and frozen.
- `loopLengthTicks` on the snapshot root is the sole loop-boundary authority; note
  objects carry identity and start/end geometry only.

---

## Representation

| Index | Shape | Contents |
|---|---|---|
| **Span index** | pitch → canonical 768-tick bar → membership list of `NoteRef`s | Complete immutable notes whose geometry intersects that bar; a note may appear in several bars |
| **Event index** | bar → membership list of `EventRef`s | An indexed view of existing event-stream order — not a second ordering authority |

Membership uses **fixed-capacity pages** of `kSnapshotMembershipPageCapacity` (16)
references. The number of pages per bar is derived from an explicit design
envelope, `kSnapshotMembershipMaxRefsPerBar` (4096 refs), applied equally to the
event index and to each note pitch/bar list. Pages are allocated lazily from the
slab allocator, so the envelope costs nothing until content uses it.

Successor construction reuses unchanged immutable references and unchanged
membership; only dirty pitch/bar membership and the minimum directory path to it
are newly constructed. A snapshot never stores a parent pointer used to recover
content.

---

## Construction

`LoopSnapshotConstruction` applies a bounded set of semantic mutations to a
parent snapshot. It is classified in [ExecutionBounds.md](ExecutionBounds.md)
row 21 (one persistent-cursor bar, at most eight 16-reference membership pages per
step) and has no content-sized unit.

- **Journal caps** bound a fragment: `kSnapshotWrapJournalMaxChanges` (32),
  `kSnapshotWrapJournalMaxAffectedBars` (32),
  `kSnapshotWrapJournalMaxMembershipEntries` (64),
  `kSnapshotWrapJournalMaxBytes` (4096). Oversized work becomes ordered bounded
  fragments that retain the exact parent identity.
- **Normalization** during recording/overdub is owned by
  `RuntimeCanonicalPassInputs` (per-pass normalized output, no allocation) plus
  `RuntimeCompanionPasses` (merge with companion edits). Successful stop cleanup
  adopts that result via `adoptCommittedNormalization` after matching
  source/worker generations.
- **Transaction (DEC-047 amendment 2026-09-16).** A pass-change fragment is built
  on a working copy; authoritative `PendingPassChangeProgress` advances only after
  the fragment is successfully incorporated. A failed fragment discards its
  partial candidate and is retried from unchanged progress, so a mutation is never
  partially applied twice.
- **Failure policy.** `LoopSnapshotConstruction::failureReason()` distinguishes a
  membership-capacity (representability) failure from an allocation failure.
  Transient/allocation failures retry the exact fragment; a persistent
  representability failure keeps the previous root selected, blocks publication,
  and emits a diagnostic rather than rebuilding the same impossible fragment
  (capture-gated `DIAG,snapshot_fragment`).

---

## Publication

`Loop::stepPendingSnapshotPublish` is the only publication owner. Wrap journals
and session-exit pass lists are queued at their commit/stop owners; idle then
admits a bounded number of units
(`MidiServiceDrain::kSnapshotIdleConstructionUnitsPerTurn` = 2).

Publication is **atomic** and conditional:

- The candidate is frozen (`LoopSnapshotConstruction::freeze`) and stored with
  `storePublishedSnapshot`; the previous root remains selected until then.
- A candidate is stored only after every queued wrap fragment, session-exit pass,
  and companion change is complete. A failure never publishes a prefix.
- Wrap journals carry a `targetRevision` so a candidate is never marked current
  for a revision it does not contain; unstamped journals stay unselected.

---

## Consumers

| Consumer | Derived representation | Source |
|---|---|---|
| Playback | `LoopPlaybackRuntime` `mergedEvents` / `playbackOrder`; `RuntimePlaybackViewSet` window + `RuntimePlaybackBridge` transition overlay | published snapshot event membership (`tryGatherPublishedSnapshotEventsInWindow`) with a stale-root overlay |
| Display | `visualCache.notes` / `liveDisplayNotes` | snapshot note-span membership (`resolveDisplayNotes`); pass replay only as no-current-root fallback |
| Analyze / NOTE_EDIT | prepared snapshot/tick indexes around a tick | selected snapshot membership |
| Effective queries | `Loop::copyEffectiveCommittedEvents*` (backed by `CommittedMidiScratch`) | published snapshot; `LoopPasses::materializeToEventVector` remains an oracle/compat path |
| Persistence | `PersistedLoopSnapshot` body format `LSV1` ([DEC-057](../../DECISION_LOG.md)) | published snapshot at save |

---

## Verification

- **Native:** `test_loop_take_survival` carries the dense device regression
  (`dense_capture_fixture.h`, 734 rows), a 257-reference bar boundary, a
  partial-apply retry/exactly-once fixture, and a representability-parking
  fixture.
- **Hardware:** the `hitl-content-verification` skill dumps
  `record` / `overdub` / `effective` and compares playback inventory; semantics
  in [SNAPSHOT_CONTENT_VERIFICATION.md](../../Guides/SNAPSHOT_CONTENT_VERIFICATION.md).

---

## Related

- [DerivedViews.md](DerivedViews.md) — consumer representations built from the snapshot
- [Playback.md](Playback.md) — playback consumer contract
- [Storage.md](Storage.md) — persisted passes and snapshot body format
- [RuntimeArchitecture.md](RuntimeArchitecture.md) — layer boundaries
- [ExecutionBounds.md](ExecutionBounds.md) — construction units and failure policy
- [NAMING.md](../NAMING.md) — `LoopSnapshot`, `pass`, `materialize`
