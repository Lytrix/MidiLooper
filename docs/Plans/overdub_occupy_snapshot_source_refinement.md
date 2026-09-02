# Overdub occupy — committed source follows the draw-source predicate

**Status:** Native shipped — device recapture next  
**Date:** 2026-09-02  
**Kind:** refinement  
**Decision:** [DEC-052](../DECISION_LOG.md#dec-052--occupy-consumes-the-draw-source-predicate)  
**Evidence:** [`134815`](../../captures/session_20260902_134815.log)  
**Does not authorize:** `#41` Stage 2, visual-cache ownership, snapshot publication/construction, LCR while PLAYING, `resolveOccupancyAtNoteOn` retune, new Manager

---

## Debugging boundary

```
shouldGatherPublishedSnapshotForDrawSource
    ← trust (paint)
rebuildOverdubSourceSpanCache source selection
    ← this stage
resolveOccupancyAtNoteOn / ledger n= / a=
    ← downstream; do not retune
visualCache population / #41 Stage 2
    ← not this stage
```

This stage establishes **committed-source selection consistency** only. It does not make `LoopSnapshot` the display-cache owner.

---

## Contracts

### A. Single predicate

Occupy calls `shouldGatherPublishedSnapshotForDrawSource` with the same live `Loop` fields paint uses. Forbidden: `if (publishedSnapshot() && publishedSnapshotIsCurrent())` or any occupy-specific approximation.

### B. Snapshot exclusivity

When the predicate is true, all committed occupy note membership and geometry come exclusively from the published `LoopSnapshot`. No visual-cache, prepared-LCR, prior-cache, or reconstructed-pass data participates in that fill. The cache must change when the snapshot changes even if `visualCache` is stale.

### C. Fallback preservation

When the predicate is false:

```text
prepared → prior cache → covering visual → passes reconstruction
```

Do not clean up this order. [`113651`](../../captures/session_20260902_113651.log) prepared-8 stays opportunistic LCR and must not win over a current snapshot.

### D. Stage boundary

Unchanged: snapshot publication/construction, runtime scheduling, visual-cache population, `#41` Stage 2, `resolveOccupancyAtNoteOn`, LCR while PLAYING, `overdubPassWrapPairing`, ledger Shorten/Hide when `a=0`.

---

## Locked source-selection rule

Committed occupy source is selected by the same draw-source predicate as `gatherDrawSourceBars`. When that predicate selects the published snapshot, occupy derives committed geometry from that snapshot (unique by `noteId` across every bar).

| Predicate | Occupy fill | `from=` |
|---|---|---|
| true | Exclusive `LoopSnapshotNote` membership | `snapshot` |
| false | prepared → prior → covering visual → passes | unchanged |

Live incoming notes stay in `capture.store` / `capturePreview`. `pendingNoteChanges_` Add overlay after the committed fill stays.

---

## Occupy-paint pin closeout

[`134815`](../../captures/session_20260902_134815.log): `srcskip not_finished` + `from=visual`. Prepared never began (`sess=0 prep=0`). A/B/C on the occupy-paint path are closed as **not choosable** — see [`overdub_occupy_prepared_source_incomplete_bugfix.md`](overdub_occupy_prepared_source_incomplete_bugfix.md). [`113651`](../../captures/session_20260902_113651.log) `from=prepared,notes=8` is leftover LCR on a restamped finished session, not occupy-paint owner.

---

## Pre-implementation review

### Ready
- Owner: `Loop::rebuildOverdubSourceSpanCache`
- Shared predicate: `DisplayWindowUtils::shouldGatherPublishedSnapshotForDrawSource`
- Shared conversion: `displayNoteFromSnapshotNote` in `Utils/SnapshotNoteConversion.h`

### Resolved
| Topic | Decision |
|---|---|
| Helper home | Neutral snapshot→`DisplayNote` header; not `Loop::` and not DisplayManager |
| Predicate-false order | Unchanged |
| HITL `occupy_od2_snap_not_zero` | Divergence only: paint `snap!=0` and occupy `from!=snapshot` |

### Open before coding
None.

### Proceed
YES
