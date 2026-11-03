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

This accepted Phase 0 boundary names the logical indexes only. The physical directory that implements
their structural sharing remains a later measurement decision.

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

`Loop::stepPendingSnapshotPublish` is the only publication owner. Session-exit pass lists are queued at their
stop owners; an overdub wrap queues **nothing** but an O(1) revision stamp, because the overdub session record
(`overdubSessionPassIds_[i]` with `overdubSessionCompanionIds_[i]`) already is its ordered publication obligation
([DEC-248](../../DECISION_LOG.md)). Idle then admits a bounded number of units
(`MidiServiceDrain::kSnapshotIdleConstructionUnitsPerTurn` = 2).

Publication admission charges the slab to its allocator tier: an external-only slab is not internal
heap headroom. The internal requirement is the 32 KiB safety floor plus 8 KiB working margin, plus
the 32 KiB slab only for internal-slab targets. Pool-refusal and transient allocation park/recovery
remain unchanged; no allocation probe is added ([DEC-157](../../DECISION_LOG.md)). When the arena tail looks low,
the reserve is resolved by the read-only, allocation-free free-list walk, at most once per 250 ms on the idle owner and
never on the MIDI or Clock path ([DEC-249](../../DECISION_LOG.md)): the tail never recovers, so a tail-only verdict
refused every publication turn after an overdub.

Publication is **atomic** and conditional:

- The candidate is frozen (`LoopSnapshotConstruction::freeze`) and stored with
  `storePublishedSnapshot`; the previous root remains selected until then.
- A candidate is stored only after every queued wrap fragment, session-exit pass,
  and companion change is complete. A failure never publishes a prefix.
- Wrap journals carry a `targetRevision` so a candidate is never marked current
  for a revision it does not contain; unstamped journals stay unselected.
- A wrap's publication (DEC-248) is produced from the session record in causal order -- record i's pass through the
  pass producer, then record i's companions in bounded fragments (at most 4 rows translated, 16 examined), then
  record i+1 -- and publishes once every live record is incorporated, at the wrap Clock's post-seal stamp. A lost
  stamp still publishes the content, with coverage unset. A current root owes nothing.

**Two request shapes, one lane (DEC-150).** A request queues work; it never publishes. The *incremental* form
(`requestCommittedPassesRepublish`) diffs every queued committed pass and companion against the **published root**, which
is what a pass-state change needs. The *rebuild* form (`requestCommittedPassesRebuild`) resolves an **empty** parent, so
the candidate is the active pass inventory plus the active overlays — used by the undo/redo edge when the entry changed
**edit passes**: the committed-pass diff would be empty there, and a companion change can only add or update, so a
withdrawn overlay can only be retracted by omitting it from a rebuild. Both stamp `coveredPassRevision` from the inventory
they enumerated (DEC-073), both keep the previous root selected and audible until the replacement lands, and both are
serviced by the same idle owner at the same bounded units.

A second request **replaces** an older one instead of being dropped: its enumeration is newer, and the older
candidate would otherwise publish behind the live revision. A rebuild box that a stop's session-exit queue
adopts re-enumerates the identities it does not track yet before stamping coverage (an empty-parent candidate
must contain every pass its watermark claims); the shared enumeration itself stays literal, so a duplicate pass
id remains an inventory row.

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
  in [HITL_CONTENT_VERIFICATION.md](../../Guides/HITL_CONTENT_VERIFICATION.md).

---

## Related

- [DerivedViews.md](DerivedViews.md) — consumer representations built from the snapshot
- [Playback.md](Playback.md) — playback consumer contract
- [Storage.md](Storage.md) — persisted passes and snapshot body format
- [RuntimeArchitecture.md](RuntimeArchitecture.md) — layer boundaries
- [ExecutionBounds.md](ExecutionBounds.md) — construction units and failure policy
- [IntervalProjection.md](IntervalProjection.md) — readiness deadline: an updated version must be ready before the playhead returns
- [NAMING.md](../NAMING.md) — `LoopSnapshot`, `pass`, `materialize`
