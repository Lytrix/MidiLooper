# Immutable loop snapshot — OpenSpec 4.6 snapshot-identity freshness

**Status:** Stage 1 **firmware + native SHIPPED** (2026-09-10). Device HITL `base`/`occupy` not yet green this session — see closeout. Next: Stage 2 `DrawSourceGatherKey` after device gate or user accept.  
**OpenSpec:** `immutable-loop-snapshot-passes` task `4.6` (still open — Stage 1 only)  
**Parent:** 4.5 SHIPPED ([`immutable_loop_snapshot_4_5_cross_gate_verify_refinement.md`](immutable_loop_snapshot_4_5_cross_gate_verify_refinement.md)).  
**Does not start:** §5 undo/load format, §6 compatibility retirement, hydrate 4b–5, raise `N`, new scheduler / `RuntimeWorkBudget::admit`, Phase 2/3 of the PassMaterialize deferral roadmap.

## Codebase / end-state review (2026-09-10)

### Still accurate

| Claim | Code check |
|---|---|
| Playback already identity-stamps | `PlaybackMergedMidiEvents.builtFromSnapshotIdentity` + match in `ensurePlaybackMergedMidiEventsBuilt` |
| VisualCache freshness revision-only | `VisualCache.builtFromRevision` only; `visualCacheIsCurrentSourceRevision` compares to `playbackRevision` |
| DrawSource uses revision + current flag | `DrawSourceGatherKey.publishedSnapshotRevision` + `snapshotCurrent` — no pointer |
| Overdub span reuse | `overdubSourceSpanCachePublishedRevision_ == publishedSnapshotRevision()` when current — revision, not identity |
| NOTE_EDIT projection keys | `playbackRevision` + `visualCache.revision` (RC-A changed **source selection**, not freshness keys) |
| `legacyMidiEventsFromCommitted` | Always `materializeEditViewFromPasses` keyed by `playbackRevision` |

RC-A did **not** close any 4.6 freshness gap. PassMaterialize removal from the NOTE_EDIT selector is ownership of **request rebuild**, not derived-rep identity stamps.

### End-state scope (important)

OpenSpec task **4.6 text** lists playback, display, NOTE_EDIT, analysis, validation, **persistence, undo, redo, clear-slot, load** and “snapshot content as normal source.”

| Layer | Status vs end state |
|---|---|
| **Content source** (read snapshot when current) | Largely done in **4.4** (+ RC-A request policy) |
| **Freshness identity** on derived reps | **This task** — inventory gaps remain |
| **Undo/load select identities / storage format** | **§5**, not Stage 1–3 firmware stamps |
| **Retire materialize as normal** | **§6** |

So 4.6 **as planned here** (identity stamps on migrated consumers) is still the right next work. Declaring OpenSpec `4.6` fully done requires either (a) completing the freshness stage list **and** documenting that persistence/undo remain §5, or (b) expanding into §5 — do not silently treat Stage 1 as task closeout.

### Stage 1 still the right first slice

`VisualCache` has no `builtFromSnapshotIdentity` field (`include/VisualCache.h`). Pattern to copy: `PlaybackMergedMidiEvents`. Native tests already couple `visualCache.builtFromRevision` to `publishedSnapshotRevision()` in places — Stage 1 must keep revision coverage contracts (`153517`) while adding identity.

**Subtlety:** `publishedSnapshotIsCurrent()` is `box.revision == playbackRevision`, so identity and revision often move together. Identity stamp still matters for (1) fallback builds that stamp revision but must carry `nullptr` identity, (2) matching the design rule “record the snapshot identity from which it was built,” (3) future root replace edge cases.
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

### Stage 1 — VisualCache snapshot-identity stamp (first firmware) — SHIPPED

**Field placement decision:** `VisualCache::builtFromSnapshotIdentity`, matching
`PlaybackMergedMidiEvents::builtFromSnapshotIdentity` (same type/semantics).
Identity is provenance metadata **owned by the derived representation**.
**No `Loop` sidecar** (would split freshness ownership from `builtFromRevision`).

| Build | Stamp identity | Stamp revision |
|---|---|---|
| Current `LoopSnapshot` | `publishedSnapshotIdentity()` (non-null) | `playbackRevision` (existing coverage rules) |
| Fallback / non-snapshot build | **`nullptr`** | existing revision rules (`153517`) |

**`nullptr` must not mean current.** Three cases for “snapshot-current source”:

| Cache build | Identity | Current-source result |
|---|---|---|
| Current `LoopSnapshot` | matching non-null identity | **current** |
| Current `LoopSnapshot`, root replaced | different non-null identity | **stale** |
| Fallback/materialized build | `nullptr` | **not snapshot-current** |

Predicate: `DisplayWindowUtils::visualCacheIsCurrentPublishedSnapshot`
(revision-only `visualCacheIsCurrentSourceRevision` retained for legacy usable checks):

```text
revision matches playbackRevision
AND builtFromSnapshotIdentity != nullptr
AND builtFromSnapshotIdentity == publishedSnapshotIdentity()
```

Do **not** allow `nullptr == nullptr` to pass as snapshot-current. Legacy
revision-only consumers that must still treat a fallback cache as “usable”
keep a separate predicate if needed — Stage 1 does not invent a new owner.

**Shipped:**

1. `VisualCache::builtFromSnapshotIdentity`; cleared in `clear()`.
2. Idle `slice_clean` and `rebuildVisualCacheFromPasses` stamp identity when
   coverage stamps revision (current-root → `publishedSnapshotIdentity()`;
   else → `nullptr`).
3. `applyDeferredVisualCacheWrapAppend` uses
   `visualCacheIsCurrentPublishedSnapshot`.
4. Native:
   `test_visual_cache_identity_stale_when_published_root_replaced_same_revision`
   (A→B at fixed R); revision fixtures (`153517`) remain green.
5. Device: HITL `base` (capture id filled at closeout); RAM1 locals **7008**.

**Falsify Stage 1 if:** identity ignored across A→B at same R; `nullptr` treated as current; PassMaterialize/full flatten on request paths; `Loop` sidecar; new scheduler.

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
| Field placement | **`VisualCache::builtFromSnapshotIdentity`** — match playback; no `Loop` sidecar |
| `nullptr` semantics | Not current; fallback builds are not snapshot-current |
| Primary native proof | Identity A→B at fixed `playbackRevision` R ⇒ stale |
| §5 / §6 | Out of Stage 1 |

### Open before Stage 1 coding

None — Stage 1 is ready to implement.

### Stage 1 closeout (2026-09-10)

| Gate | Result |
|---|---|
| Native A→B@R + `153517` | PASS (`test_loop_take_survival`, `test_display_window_utils`, `test_overdub_source_view`) |
| Full `pio test -e native` | **1821 succeeded, 1 skipped** |
| RAM1 locals | **7008** |
| HITL `base` | Attempt [`144648`](../../captures/session_20260910_144648.log): musical path green (overdub latency 8), `persistence_stage_missing:dispatch` (device backlog). Later retries: `overdub_first_note_too_late` / other noise. `RECS,stage,visual_cache_request` present in serial. |
| HITL occupy | [`overdub_occupy_paint`](../../captures/) FAIL this session (`occupy_source_pass_shorten:4` + stop-stage scoping). Not treated as Stage 1 RC. |

OpenSpec `4.6` remains **unchecked** until remaining Stage 2+ freshness
consumers (and §5 exclusion note) are done. Device smoke still needed before
calling Stage 1 fully closed.
