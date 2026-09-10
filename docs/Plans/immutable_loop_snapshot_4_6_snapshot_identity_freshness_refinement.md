# Immutable loop snapshot — OpenSpec 4.6 snapshot-identity freshness

**Status:** Now implementing — Stage 0 inventory complete; Stage 1 VisualCache identity stamp next.  
**OpenSpec:** `immutable-loop-snapshot-passes` task `4.6`  
**Parent:** 4.5 SHIPPED ([`immutable_loop_snapshot_4_5_cross_gate_verify_refinement.md`](immutable_loop_snapshot_4_5_cross_gate_verify_refinement.md)).  
**Does not start:** §5 undo/load format, §6 compatibility retirement, hydrate 4b–5, raise `N`, new scheduler / `RuntimeWorkBudget::admit`, Phase 2/3 of the PassMaterialize deferral roadmap.

## Task text (authority)

> Update every migrated derived representation to use **snapshot identity** for source freshness while retaining its existing owner, invalidation, bounded build, and stale/fallback policy. … Compatibility replay/materialization remains only an oracle or rollback path and is not optimized.

## Invariant

```text
Derived representation freshness key = published snapshot identity
(when built from a current root), not playbackRevision alone.

Same playbackRevision with a replaced root must not reuse a stale derived
representation. Different identity ⇒ rebuild per existing owner policy.
```

Playback already does this (`PlaybackMergedMidiEvents.builtFromSnapshotIdentity`, task 4.1). 4.6 extends the same pattern to remaining consumers.

## Architecture checkpoint (task start)

| Question | Answer |
|---|---|
| Ownership change? | **NO** per slice — each consumer keeps its owner; only the freshness key changes |
| State-transition change? | **NO** for Stage 1 — wrap/publish/idle unchanged; identity stamp only |
| Unbounded MIDI-interval owner? | **NO** |
| Formal trigger? | Only if a slice moves undo/load format (§5) or retires materialize as normal (§6) — stop and reassess |

Reuse: **YES** — extend `publishedSnapshotIdentity()` + stamp pattern from `ensurePlaybackMergedMidiEventsBuilt`.

## Inventory (Stage 0)

| Consumer | Owner | Freshness today | 4.6 gap? | Notes |
|---|---|---|---|---|
| `PlaybackMergedMidiEvents` | `Track` / `LoopPlaybackRuntime` | revision **+** `builtFromSnapshotIdentity` | **No** | 4.1 done |
| `VisualCache` | `Loop` | `builtFromRevision` only | **Yes — Stage 1** | Source migrated 4.4b/e |
| `DrawSourceGatherKey` | `DisplayManager` | revision + `publishedSnapshotRevision` + current flag | **Yes** | After VisualCache |
| Overdub span cache reuse | `Loop` | playback + published **revision** numbers | **Yes** | Fill already snapshot |
| NOTE_EDIT projection caches | `EditManager` | `playbackRevision` + `visualCache.revision` | **Yes** | Source Snapshot\|VisualCache (RC-A) |
| `materializedLoopEventsForNoteEditFocus` | `EditManager` | `playbackRevision` | **Yes** | Later |
| `legacyMidiEventsFromCommitted` | `Track` | `playbackRevision`; always pass materialize | **Yes — large** | Named in 4.4 closeout |
| `passesMaterializedStore_` / `midiEvents()` | `Loop` | dirty flag | **Yes → §6 overlap** | Don’t over-invest |
| Undo / clear / SD load | `TrackUndo` / `StorageManager` | — | **Out — §5** | |
| LCR prepared window | `LoopContentResolution` | `preparedPlaybackRevision` | Defer / §6 | |

**Class 3:** pass rematerialize stays **fallback/oracle** when no current root (RC-A already removed it from NOTE_EDIT request selection).

## Stage plan

### Stage 1 — VisualCache snapshot-identity stamp (first firmware)

Mirror playback:

1. Add `VisualCache` / loop field for `builtFromSnapshotIdentity` (or equivalent).
2. Stamp `publishedSnapshotIdentity()` on idle/full rebuild when source is current root; `nullptr` on fallback rebuild.
3. Extend `visualCacheIsCurrentSourceRevision` (or sibling) to require identity match when identity was stamped.
4. Retain dirty-bar / stale / fallback policy; no wrap/publish transition edits.
5. Native fixture: same `playbackRevision`, different published identity ⇒ cache not treated current.
6. Device: existing `base` or occupy smoke + RAM1 ≥ 4000; full native.

**Falsify Stage 1 if:** identity ignored and cache reused across root replace at same revision; or PassMaterialize / full flatten reintroduced on request paths; or new scheduler appears.

### Stage 2+ (ordered, separate commits)

1. `DrawSourceGatherKey` identity field  
2. Overdub span-cache reuse identity  
3. NOTE_EDIT committed-base cache keys  
4. Then evaluate `legacyMidiEventsFromCommitted` (may need design if source + freshness)

## Explicit non-goals

- Implementing §5 undo attachment in 4.6 slices  
- Deleting `materializeToEventVector` globally  
- Turning RC-A idle debt into a new scheduler  
- Raising snapshot idle `N`

## Pre-implementation review (Stage 1)

### Ready

- Pattern exists on `PlaybackMergedMidiEvents`.
- VisualCache source already snapshot on current root.

### Resolved

| Topic | Decision |
|---|---|
| First slice | VisualCache identity stamp |
| §5 / §6 | Out of Stage 1 |

### Open before Stage 1 coding

1. Exact field placement (`VisualCache` struct vs `Loop` sidecar) — pin to match playback naming (`builtFromSnapshotIdentity`).
