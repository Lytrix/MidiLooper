# Display visual-cache snapshot reuse

**Status:** Stage 1 in tree (native gate open). Stage 2 parked until Stage 1 device PASS. GitHub [#41](https://github.com/Lytrix/MidiLooper/issues/41).  
**Date:** 2026-08-31  
**Kind:** refinement (Stage 1 is a wrap-stale bugfix; Stage 2 is 4b.1b snapshot fill)  
**GitHub:** [#41](https://github.com/Lytrix/MidiLooper/issues/41) — one work item, two stages. Work: [Work](https://github.com/users/Lytrix/projects/1)  
**Parent inventory:** [`playback_window_build_owner_boundary_refinement.md`](playback_window_build_owner_boundary_refinement.md) § 4b.1b now points here.  
**Evidence:** [`192422`](../../captures/session_20260831_192422.log) wrap `VCACHE,stale,notes=255,dsz=0,dcnt=0`; `loop_rem,idle_maint,50450` + `lcr,src,why=idle,from=passes`.

**Does not start:** all-track idle fan-out, `T4 track_update`, N / pages knobs, OpenSpec `3.5` / `3.6` checkoff, LCR as a second `visualCache`, `overdubSourceView` consume authority, cooperative `reconstructDisplayNotes`.

---

## One work item

[#41](https://github.com/Lytrix/MidiLooper/issues/41) is the capability: `visualCache` stays the piano-roll list; published `LoopSnapshotNote` membership becomes the committed fill source.

| Stage | Invariant | Status |
|---|---|---|
| **1 — wrap-stale** | After wrap commit (and the following `beginCapture(Overdub)`), `visualCache.notes` stay. `dirtyBars` is the affected bar set, never empty-then-all while notes exist. | Native **1511/1511**. RAM1 locals **6944**. Device recapture open. |
| **2 — snapshot fill** | A VisualCache slice whose dirty bars are covered by the published snapshot copies those `LoopSnapshotNote`s. No `materializeToEventVector` / `reconstructDisplayNotes` / `from=passes` on that slice. | Parked until Stage 1 device PASS. This is former 4b.1b. |

Stage 2 first would copy snapshot notes, then wrap would wipe them again.

---

## Stage 1 root cause

Wrap `commitCapturePass(OverdubWrap)` / `commitStopFinalizeFromStore` call `notifyCommittedContentChanged` → `Loop::markPassDerivedStale`. After `slice_clean`, `dirtyBars` is empty, so stale emits `dsz=0`. Idle then `markAllVisualCacheBarsDirty`. Reconstruct runs with `allowOverdubWrapAppend = false`, so wrap-crossing overdub notes disappear until `append_resume`. `playbackRevision++` invalidates the source-span cache; the same VisualCache turn does `rebuildOverdubSourceView("idle")` `from=passes`.

Overdub **button** start already must not `markDisplayCachesStale` (DEC-036 3b). Stop already uses `markAffectedDisplayCacheRanges`. Wrap does not.

**Architecture checkpoint:** ownership NO, musical transitions NO, Row-22 unchanged. Reuse `markAffectedDisplayCacheRanges`.

---

## Stage 1 change

- Wrap publish marks affected bars via `markAffectedDisplayCacheRanges`, not `markPassDerivedStale`.
- `markPassDerivedStale`: nonempty notes + empty `dirtyBars` must not emit a zero-size dirty set.
- Idle: empty `dirtyBars` + nonempty notes must not mean `markAllVisualCacheBarsDirty`.
- Do not `visualCache.notes.clear()` / `invalidateDisplayCaches` on wrap or overdub start.

Native: wrap a clean 16-bar cache; note count holds; `dirtyBars` is the wrap-pass span. Record-stop `markDisplayCachesStale` unchanged.

Device: no wrap `VCACHE,stale` with `dsz=0` while `notes>0`; no `lcr,src,why=idle,from=passes` on that wrap.

---

## Stage 2 change

Idle already gathers snapshot **events** then `reconstructDisplayNotes`. `LoopSnapshotNote` is already the display span. Copy membership for dirty bars. `tryGatherPublishedSnapshotEventsInWindow` is fallback when the published root does not cover those bars. Live capture and unpublished wrap journals stay an overlay.

Native: one dirty bar after a published root copies snapshot notes; reconstruct path is not taken.

---

## Sequence

1. Stage 1 firmware + native + RAM1 + commit.
2. Device recapture. Only then Stage 2.
3. Stage 2 stays parked until Stage 1 device PASS. Do not check off OpenSpec `3.5` / `3.6`.

## Pre-implementation review

### Ready
- Wrap publish: `Loop::commitCapturePass(OverdubWrap)` → `notifyCommittedContentChanged` → `markPassDerivedStale`.
- Track wrap: `Track::commitOverdubWrapAtSessionStart` seals companions after publish.
- Stop already calls `markAffectedDisplayCacheRanges` after companion seal.
- Idle expand: `rebuildVisualCacheIdleSlice` when `dirtyBars.size() < totalBars`.
- `beginCapture(Overdub)` does not clear `visualCache.notes`.

### Resolved
| Topic | Decision |
|---|---|
| Wrap dirty mark | Reuse `markAffectedDisplayCacheRanges` at wrap publish and after companion seal |
| Empty dirty set | Do not emit `dsz=0`; idle does not `markAllVisualCacheBarsDirty` |
| Record-stop | `markDisplayCachesStale` unchanged |
| Stage 2 | Not this session |

### Open before coding
None.

### Proceed?
YES
