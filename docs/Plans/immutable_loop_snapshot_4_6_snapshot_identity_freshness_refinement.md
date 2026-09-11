# Immutable loop snapshot — OpenSpec 4.6 snapshot-identity freshness

**Status:** Stages 1–6 **SHIPPED** (2026-09-11). OpenSpec task `4.6` closed for derived-rep identity freshness; persistence/undo/load remain **§5**; materialize retirement **§6**.  
**OpenSpec:** `immutable-loop-snapshot-passes` task `4.6` — checkoff after Stage 6 + §5 exclusion.  
**Parent:** 4.5 SHIPPED ([`immutable_loop_snapshot_4_5_cross_gate_verify_refinement.md`](immutable_loop_snapshot_4_5_cross_gate_verify_refinement.md)).  
**Does not start:** §5 undo/load format, §6 compatibility retirement, hydrate 4b–5, raise `N`, new scheduler / `RuntimeWorkBudget::admit`, Phase 2/3 of the PassMaterialize deferral roadmap.
**Related (separate):** [`overdub_live_wrap_playhead_tail_display_bugfix.md`](overdub_live_wrap_playhead_tail_display_bugfix.md) — plan only.

## Codebase / end-state review (2026-09-10)

### Still accurate

| Claim | Code check |
|---|---|
| Playback already identity-stamps | `PlaybackMergedMidiEvents.builtFromSnapshotIdentity` + match in `ensurePlaybackMergedMidiEventsBuilt` |
| VisualCache freshness revision-only | **Closed Stage 1** — now `builtFromRevision` + `builtFromSnapshotIdentity` |
| DrawSource uses revision + current flag | **Closed Stage 2** — `publishedSnapshotIdentity` on `DrawSourceGatherKey` |
| Overdub span reuse | **Closed Stage 3** — `overdubSourceSpanCachePublishedIdentity_` |
| NOTE_EDIT projection keys | `playbackRevision` + `visualCache.revision` + `publishedSnapshotIdentity` (Stage 4) |
| `legacyMidiEventsFromCommitted` | Snapshot fill + revision/identity via `CommittedMidiScratch` (Stage 5) |

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
| NOTE_EDIT projection caches | `EditManager` | revision **+** `publishedSnapshotIdentity` | **Closed Stage 4** | Source Snapshot\|VisualCache (RC-A) |
| `materializedLoopEventsForNoteEditFocus` | `EditManager` | revision + slot + length **+** `publishedSnapshotIdentity` | **Closed Stage 6** | Content already via `copyNoteEditCommitBase` |
| `legacyMidiEventsFromCommitted` | `Track` | revision **+** `committedMidiScratchPublishedIdentity_` | **Closed Stage 5** | Snapshot fill; pass fallback stamps `nullptr` |
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

### Stage 2 — DrawSourceGatherKey snapshot-identity — SHIPPED

**Field:** `DrawSourceGatherKey::publishedSnapshotIdentity` next to
`publishedSnapshotRevision`. Stamped from `Loop::publishedSnapshotIdentity()` in
`makeDrawSourceGatherKey`. Compared in `drawSourceCommittedSourceKeysMatch`
(pointer equality; `nullptr == nullptr` is a valid “no current root” match for
reuse keys — distinct from VisualCache currency where nullptr is never current).

**Primary native proof:** same `playbackRevision` / `publishedSnapshotRevision`
with identity A→B ⇒ `RebuildCommittedDrawSource`
(`test_rebuild_committed_draw_source_on_snapshot_identity_same_revision`).

**Falsify Stage 2 if:** identity ignored across A→B at same R; PassMaterialize on
draw-source request; new owner/scheduler.

### Stage 2 closeout (2026-09-10)

| Gate | Result |
|---|---|
| Native A→B@R draw-source | PASS (`test_display_window_utils`) |
| Full `pio test -e native` | **1822 succeeded, 1 skipped** |
| RAM1 locals | **7008** |
| HITL `base` | **PASS** [`154842`](../../captures/session_20260910_154842.log) — first-note latency advisory; serial-follow partial-line race fixed so `RECS,stage,publish` is not dropped |

OpenSpec `4.6` remains **unchecked** until remaining Stage 3+ freshness
consumers (and §5 exclusion note) are done.

### Stage 3 — Overdub span-cache reuse identity — SHIPPED

**Field:** `overdubSourceSpanCachePublishedIdentity_` beside
`overdubSourceSpanCachePublishedRevision_`. Stamp non-null
`publishedSnapshotIdentity()` when fill origin is `"snapshot"`; else `nullptr`.
`overdubSourceSpanCacheReusable()` when a current root exists: require revision
match **and** identity match (non-null). When no current root, keep today’s
ready-cache reuse (`publishedSnapshotIsCurrent()` false → reusable if ready).

**Primary native proof:** A→B at fixed playback/published revision ⇒ span cache
not reusable; rebuild fills from B
(`test_span_cache_identity_stale_when_published_root_replaced_same_revision`).

**Falsify Stage 3 if:** identity ignored across A→B at same R; PassMaterialize on
occupy request; new owner/scheduler.

### Stage 3 closeout (2026-09-10)

| Gate | Result |
|---|---|
| Native A→B@R span cache | PASS (`test_overdub_source_view`) |
| Full `pio test -e native` | **1823 succeeded, 1 skipped** |
| RAM1 locals | **7008** |
| HITL `base` | **PASS** [`155756`](../../captures/session_20260910_155756.log) |
| HITL occupy | Machine **PASS** [`160337`](../../captures/session_20260910_160337.log) — shorten=5 hide=4 undo=2 redo=2; process exit non-zero from base `persistence_stage_missing:dispatch` only |

### Stage 4 — NOTE_EDIT committed-base cache identity — SHIPPED

**Field:** `noteEditSelectableDisplayCachePublishedSnapshotIdentity_` beside
playback/visual revision cache stamps. Compared in
`noteEditCommittedBaseKeysMatch` (pointer equality; `nullptr == nullptr` is a
valid “no current root” match — DrawSource-style). Stamped from
`Loop::publishedSnapshotIdentity()` when rebuilding the committed base in
`ensureNoteEditDisplayProjectionCachesBuilt`. Paint-only keys
(`sessionPreviewRevision`, fingerprint, capture revision) unchanged.

**Primary native proof:** same loopLength / playback / visual revision with
identity A→B ⇒ `RebuildCommittedBase`
(`test_rebuild_note_edit_committed_base_on_snapshot_identity_same_revision`).

**Falsify Stage 4 if:** identity ignored across A→B at same R; PassMaterialize on
NOTE_EDIT request; paint keys treated as committed-base keys; new owner/scheduler.

### Stage 4 closeout (2026-09-10)

| Gate | Result |
|---|---|
| Native A→B@R committed-base | PASS (`test_display_window_utils`) |
| Full `pio test -e native` | **1825 succeeded, 1 skipped** (with length-fader gate) |
| RAM1 locals | **7008** |
| HITL `base` | **PASS** [`172224`](../../captures/session_20260910_172224.log) — earlier [`172050`](../../captures/session_20260910_172050.log) FAIL was record seal **3840** (stop late), not Stage 4 identity; length-fader gate held (`ignored: capture active`) |

OpenSpec `4.6` remains **unchecked** until remaining freshness consumers
(`materializedLoopEventsForNoteEditFocus`, `legacyMidiEventsFromCommitted`, …)
and §5 exclusion note are done.

### Stage 5 — `legacyMidiEventsFromCommitted` content + freshness — APPROVED WITH TIGHTENING

**Architect (2026-09-11):** proceed after tightenings below. Genuine **content-source correction plus freshness correction**, not stamp-only. Keep Stage 5 and Stage 6 as **separate commits**. Do not broaden inventory during Stage 5.

**Root cause (one commit):** revision-only freshness and pass-effective content instead of published-snapshot content/provenance.

**Ownership (unchanged):**

- `Track` owns `committedMidiScratch_` and freshness/provenance stamp
- `Loop` owns snapshot access/copy
- No new cache, manager, scheduler, or interval owner

#### Provenance / validity

```text
scratch provenance =
    committedMidiScratchPublishedIdentity_ != nullptr
    iff scratch contents came from that published snapshot

scratch valid iff
    committedMidiScratchRevision_ == playbackRevision
    AND committedMidiScratchPublishedIdentity_ == currentPublishedSnapshotIdentity

For non-snapshot fallback:
    committedMidiScratchPublishedIdentity_ == nullptr
```

`nullptr` = scratch built from non-snapshot material (never “current”).

#### Snapshot-copy precondition

Helper path acceptable **only when** it copies the **currently published** snapshot corresponding to the **current committed state** (`publishedSnapshotIsCurrent()` / `revision == playbackRevision`).

Ordering:

```text
published LoopSnapshot → copy existing snapshot edit view from that observed snapshot
→ If the currently published snapshot cannot supply the requested edit-view events,
  retain the existing materializeEditViewFromPasses() fallback and stamp
  committedMidiScratchPublishedIdentity_ = nullptr.
```

Failure = **No usable published snapshot for this request → existing pass-materialization fallback.** Must not copy a merely available but stale snapshot.

#### Single publication observation

```text
observe published snapshot once (ref + identity) under current-root precondition
→ attempt copy from that observed snapshot
→ if successful: stamp exactly that observed identity + playbackRevision
→ otherwise: materializeEditViewFromPasses() + stamp nullptr + playbackRevision
```

Do not match on one identity observation, copy via a second lookup, then stamp a third `publishedSnapshotIdentity()` call.

#### Atomic invalidation

Every clear path (`invalidateLoopDerivedCaches`, `tryClearCommittedMidiScratch`, any other revision reset):

```text
committedMidiScratchRevision_ = UINT32_MAX
committedMidiScratchPublishedIdentity_ = nullptr
```

Successful fill assigns revision and identity in the same logical operation.

#### Native exit criteria (all four required)

| Case | Expected |
|---|---|
| same R + same published identity | reuse scratch; no snapshot copy; no pass materialization |
| same R + A→B identity change | rebuild; returned events are **B**, not A |
| revision change | rebuild |
| no usable published snapshot @ R | pass materialization; `identity == nullptr` |

A→B@R must prove **source correctness** (events are B), not only that rebuild occurs.

#### Stage 5 commit boundary

**Include:** identity member, all invalidation updates, fill ordering, native four-case fixtures, Stage 5 doc notes (leave OpenSpec `4.6` unchecked).

**Exclude:** Stage 6, OpenSpec 4.6 checkbox, §5/§6 work.

#### Device gates (after native exit criteria)

`pio run -e teensy41-capture-serial` (RAM1 ≥ 4000), `pio test -e native`, HITL `base`.

#### Stage 5 closeout (2026-09-11) — SHIPPED

| Gate | Result |
|---|---|
| Native four-case exit | PASS (`test_legacy_committed_scratch_snapshot_identity_freshness`) |
| RAM1 locals | **6976** (`teensy41-capture-serial`) |
| Full `pio test -e native` | **1838 succeeded, 1 skipped** |
| HITL `base` | **PASS** [`140021`](../../captures/session_20260911_140021.log) — persistence `result=ok`; undo/redo 1/1. Earlier [`135920`](../../captures/session_20260911_135920.log) FAIL was `persistence_stage_missing:dispatch` (not Stage 5 identity). |

**Shipped:** `CommittedMidiScratch::refresh` / `invalidate`; `Track::committedMidiScratchPublishedIdentity_`; `Loop::tryCopyCurrentSnapshotEditViewEvents(..., observedIdentity)`; atomic clear in `invalidateLoopDerivedCaches` + `tryClearCommittedMidiScratch`.

OpenSpec `4.6` remains **unchecked** until Stage 6 + §5 exclusion note.

### Stage 6 — focus materialize identity + §5 exclusion — SHIPPED

**Root cause:** `materializedLoopEventsForNoteEditFocus` keyed revision/slot/length only.

**Shipped:**

1. `noteEditFocusMaterializePublishedSnapshotIdentity_` + `noteEditFocusMaterializeKeysMatch`
2. Stamp `Loop::publishedSnapshotIdentity()` on rebuild; clear identity on `invalidateNoteEditDerivedCaches`
3. Native A→B@R: `test_rebuild_note_edit_focus_materialize_on_snapshot_identity_same_revision`
4. **§5 exclusion:** OpenSpec task `4.6` text names persistence, undo, redo, clear-slot, and load. Those remain **tasks 5.1–5.6** (snapshot-identity undo / storage format). Stage 1–6 of this plan close **derived-representation identity freshness** only. Checking off `4.6` documents that exclusion — it does **not** implement §5.
5. Leave `passesMaterializedStore_` / `Loop::midiEvents()` to OpenSpec **§6** (compatibility retirement).

#### Stage 6 closeout (2026-09-11)

| Gate | Result |
|---|---|
| Native A→B@R focus materialize keys | PASS (`test_rebuild_note_edit_focus_materialize_on_snapshot_identity_same_revision`) |
| RAM1 locals | **6976** |
| Full `pio test -e native` | **1839 succeeded, 1 skipped** |
| HITL `base` | **PASS** [`141547`](../../captures/session_20260911_141547.log) — persistence `result=ok`. Earlier [`141205`](../../captures/session_20260911_141205.log) FAIL was `persistence_stage_missing:dispatch` (not Stage 6 identity). |

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
| HITL `base` | **PASS** [`151433`](../../captures/session_20260910_151433.log) — first-note 11/10, persistence `result=ok`, undo/redo 1/1. Earlier noisy attempts (`144648` persistence backlog; later first-note flakes) superseded. |
| HITL occupy | **PASS** [`151700`](../../captures/session_20260910_151700.log) — shorten=9 hide=12 undo=2 redo=2. |

OpenSpec `4.6` remains **unchecked** until remaining Stage 2+ freshness
consumers (and §5 exclusion note) are done.
