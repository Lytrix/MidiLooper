# Boot title LoadLoopJob idle-slice crash

**Status:** Active — sub-step pinned to LCR/gather; quiet capture env for next gate  
**Date:** 2026-08-16  
**Kind:** investigation  
**Trigger:** [`session_20260816_152405.log`](../../captures/session_20260816_152405.log) — title boot reset after first focus `LoadLoopJob`  
**Parent:** Layer B device gate blocked — [`note_edit_undo_warm_missing_recon_investigation.md`](note_edit_undo_warm_missing_recon_investigation.md)  
**Sibling (RAM1 class, different last line):** grooming Slice 1 [`132439`](../../captures/session_20260816_132439.log) died at `BOOT,scan,start`

This file is **investigation only**. Do not patch gather, reconstruct, LCR, or `commitLoadLoopJobPublish` until a sub-step is pinned and the architecture checkpoint for that patch is both **NO**.

---

## Debugging boundary

```text
Layer B assignMissingNoteIdsInCommittedCapturePasses
        ← not this path (NOTE_EDIT open only; boot is LOOP_EDIT)
        ↓
commitLoadLoopJobPublish (0/5)     ← trust; "done" printed
        → adoptPersistedSnapshot → VCACHE,stale_all + stale (ring)
        ↓
next loop() SC_CAPTURE_FLUSH       ← stale lines appear
        ↓
Track::processDeferredIdleMaintenance (STOPPED)
        → Loop::rebuildVisualCacheIdleSlice(4, 0)   ← current investigation
        → VCACHE,slice_clean                       ← 145518 next line; 152405 never emits
```

Do not reopen Layer B NoteId. Do not start grooming 4e or Slice 5 hydrate. Do not treat `#CAP,BOOT,scan,t*` after `BOOT,heap` as leftover session markers — those are this boot’s `emitBootMilestone` ring flush (same in [`145518`](../../captures/session_20260816_145518.log)).

---

## What [`152405`](../../captures/session_20260816_152405.log) proves

USB drops (`#CAPTURE_RECONNECT`, `[Errno 6] Device not configured`) in a reconnect loop.

| Shape | Last firmware line | Count in this capture |
|-------|--------------------|------------------------|
| Mid-scan | `BOOT,scan,t0`…`t3` | several |
| Title drain | `LoadLoopJob done 0/5` then flushed `VCACHE,stale_all` / `VCACHE,stale` (total 4) | every completed scan |

Never: `VCACHE,slice_clean`, `DIAG,stored_notes`, `BOOT,usb_host,begin`. Title stays because `finishBootSetup` never runs.

`#CAP,BOOT,scan,*` after `BOOT,heap` is this boot’s ring flush (`emitBootMilestone` → `appendCaptureTextLine`). Same block in [`145518`](../../captures/session_20260816_145518.log) at the same place.

---

## What [`145518`](../../captures/session_20260816_145518.log) does at the same point

After `LoadLoopJob done 0/5`:

1. `VCACHE,stale_all` / `VCACHE,stale` (total 4)
2. `VCACHE,slice_clean` **notes,113** (track 0 slot 5)
3. `DIAG,stored_notes,track,0,slot,5,notes,113`
4. further slot restores
5. `DisplayManager: Boot setup complete` → `BOOT,usb_host,begin`

---

## Owners (code)

`loop()` order in [`main.cpp`](../../src/main.cpp): `SC_CAPTURE_FLUSH` → `processDeferredIdleMaintenance` → `runDeferredLoadAndDisplayFrame`.

`adoptPersistedSnapshot` queues `stale_all` then `stale` on the ring **before** `LoadLoopJob done` is printed. Those lines appear on the **next** flush. The next STOPPED idle work for a dirty committed loop is `Loop::rebuildVisualCacheIdleSlice` from `Track::processDeferredIdleMaintenance`.

| Step | Owner | File |
|------|--------|------|
| Commit 0/5 | `commitLoadLoopJobPublish` | [`LoadLoopJob.cpp`](../../src/StorageManager/LoadLoopJob.cpp) |
| Stale emit | `Loop::adoptPersistedSnapshot` → `markDisplayCachesStale` / `notifyCommittedContentChanged` | [`LoopEditPasses.cpp`](../../src/Loop/LoopEditPasses.cpp), [`LoopVisualCache.cpp`](../../src/Loop/LoopVisualCache.cpp), [`Loop.cpp`](../../src/Loop.cpp) |
| Next-turn flush | `SC_CAPTURE_FLUSH` | [`main.cpp`](../../src/main.cpp) |
| Missing `slice_clean` | `Loop::rebuildVisualCacheIdleSlice` | [`LoopVisualCache.cpp`](../../src/Loop/LoopVisualCache.cpp) |
| `stored_notes` (not reached) | `Track::maybeLogStoredNoteCount` | [`TrackDeferredMaintenance.cpp`](../../src/Track/TrackDeferredMaintenance.cpp) |

Idle slice sub-steps (in order): `tryResolvePreparedWindow` → else `gatherCommittedEventsInWindow` → `reconstructDisplayNotes` → `appendOverdubPassDisplayNotes` → merge → `emitVisualCacheState("slice_clean")`.

Ring `SC_VCACHE` after a crash in the same turn is not visible (flush already ran). Breadcrumbs for this pin must be immediate `Serial` (same as `LoadLoopJob done`).

---

## Architecture checkpoint (before firmware)

1. Ownership change? Breadcrumbs on `rebuildVisualCacheIdleSlice`: **NO**. Changing gather / reconstruct / LCR / commit owner: **YES** — stop.
2. State transition change? Logs only: **NO**. Deferring title drain, skipping idle slice, or moving rebuild onto commit: **YES** — design session.

---

## Next pin (breadcrumbs)

Immediate `Serial` under `SESSION_CAPTURE` in `rebuildVisualCacheIdleSlice`:

| Line | When |
|------|------|
| `VCACHE,slice_enter,bars,<n>` | after window length known, before LCR/gather |
| `VCACHE,slice_gathered,ev,<n>` | after LCR or gather |
| `VCACHE,slice_recon,notes,<n>` | after `reconstructDisplayNotes` |

### [`153545`](../../captures/session_20260816_153545.log)

First completed boot: `LoadLoopJob done 0/5` → `VCACHE,stale_*` → **`VCACHE,slice_enter,bars,4`** → `#CAPTURE_RECONNECT`. No `slice_gathered`. Fault is `tryResolvePreparedWindow` or `gatherCommittedEventsInWindow`.

Full `teensy41-capture-serial` also dumped LED / MO / DIAG before that. Those tags are now compile-gated. This investigation uses `teensy41-capture-serial-vcache-slice` (`SESSION_CAPTURE_VCACHE_SLICE=1`, LED/MO/DIAG off). Default `teensy41-capture-serial` keeps the HITL contract; slice breadcrumbs stay off there.

Device gate: one title boot on the vcache-slice env. Last breadcrumb before `#CAPTURE_RECONNECT` is the sub-step.

---

## Does not start

- Layer B remaining A copies/clone
- Layer C/D, grooming 4e, Slice 5 hydrate
- Patching `applyNoteEditPass` / apply-owned erase
- GitHub Bug until a sub-step is pinned
- `LoadLoopJob` paint / PLAYING drain work
