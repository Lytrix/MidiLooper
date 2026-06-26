# Handoff — hybrid layout 1.1, then M2 display + tests

**Date:** 2026-06-26  
**Branch:** `refactor/timeline-data-model`  
**Tip:** `f4acf2e` — CurrentSet v6, SavedSet M2 backend, save-status display  
**Status:** M1 + M2 backend **committed** · hybrid layout **OpenSpec only** · display/tests **pending**

Prior session transcript:
[`52b1e5c6-d900-4ee4-9a25-b0bcdad2dd8c`](../../.cursor/projects/Users-eelkejager-Documents-PlatformIO-Projects-250513-215524-teensy41/agent-transcripts/52b1e5c6-d900-4ee4-9a25-b0bcdad2dd8c/52b1e5c6-d900-4ee4-9a25-b0bcdad2dd8c.jsonl)

Broader context: [`workspace_session_persistence_handoff.md`](workspace_session_persistence_handoff.md)

---

## Apply order (locked for next agent)

| Step | OpenSpec change | Task | Focus |
|------|-----------------|------|-------|
| **1** | `currentset-savedset-storage-layout` | **1.1** | Remove transport-stop full-slot dirty marking |
| **2** | same | **1.2–1.3** | Confirm deferred save skips clean slots; native stop-path write test |
| **3** | `workspace-session-persistence` | **2.13** | CURRENT row, `From:` provenance, auto-save toast |
| **4** | same | **2.14** | Native `loadSetIntoCurrent` dirty/clean paths |
| **5** | `workspace-session-persistence` | **M3** (3.1–3.6) | Slot loop import |
| **6** | same | **M4** (4.1–4.9) | RecoveryPoint + browser polish (4.7 overlaps 2.13) |
| **7** | `currentset-savedset-storage-layout` | **§2–3** | Slot metadata index, SavedSet packed `loops.bin` |

**Why 1.1 first:** transport stop currently forces up to 64 slot rewrites and masks incremental-write policy before browser/UI work. Spec: `current-set-live-storage` — transport stop SHALL NOT mark all slots dirty in normal operation.

**Coordinate** `DisplayManager` set-browser work with `long-loop-piano-roll-window` M2 to avoid merge conflicts.

---

## Step 1 — Task 1.1 (transport-stop dirty policy)

### Problem

```347:348:src/TrackManager.cpp
  StorageManager::markAllCurrentSetLoopSlotsDirty();
  StorageManager::requestDeferredSaveState(looperState.getLooperState());
```

Every transport stop marks **all** 64 loop slots dirty → deferred save rewrites every `loop_TT_SS.bin` even when nothing changed.

### Intended fix

1. **Remove** `markAllCurrentSetLoopSlotsDirty()` from `TrackManager::handleTransportStop()`.
2. **Keep** `requestDeferredSaveState` if transport/meta still needs a meta-only flush (verify: dirty-slot bitmap empty → meta write only, no payload rewrites).
3. **Do not touch** explicit full-rewrite paths — these already use `markAllCurrentSetLoopSlotsDirtyInternal(false)` + `forceCurrentSetFullLoopWrite`:
   - `migrateV5MonolithToCurrentSet`
   - `tryLoadLatestRecoveryPoint`
   - `tryLoadNewestSavedSet`

### Architecture checkpoint

- **Ownership:** no change — `StorageManager` still owns dirty bitmap and deferred FSM.
- **Transitions:** no new modes — only removes an over-broad dirty hook on transport stop.
- **Proceed** with a minimal patch.

### Verification (1.1 + 1.3)

- Native test: simulate transport stop with zero dirty slots → assert no slot payload write steps enqueued (may need test hook on deferred FSM or write counters).
- HITL baseline after firmware change: `scripts/host_midi_hitl.py run --preset base` — compare `PERS,result_stats` write counts vs pre-1.1 if available.
- `pio test -e native` — full matrix.

OpenSpec: `openspec/changes/currentset-savedset-storage-layout/tasks.md` §1.

---

## Step 3 — Task 2.13 (set browser display basics)

Backend already in place:

| API / data | Location |
|------------|----------|
| `saveNewSet`, `loadSetIntoCurrent` | `StorageManager` |
| SetIndex reconcile, folder naming, default labels | `SavedSetCatalog` |
| Dirty anchor / provenance fields | `CurrentSetStorage::patchAnchorFields`, meta v6 |
| Auto-save before load | `loadSetIntoCurrent` when `hasMaterialChangesSinceAnchor` |

### Display requirements (spec)

`openspec/changes/workspace-session-persistence/specs/set-browser-display/spec.md`:

- **CURRENT** row always highlighted; not replaced by SavedSet row after load.
- `Last active: {date} {time}` from CurrentSet meta.
- `From: {folderId}` when `loadedFromSequence` ≠ 0 (subtitle on CURRENT).
- SavedSet list: sequence newest-first; date or UID labels via `SavedSetCatalog` helpers.
- Brief toast on auto **saveNewSet** before load (`Saved 260625_004`, 1–2 s, non-blocking).

### Primary files

- `src/DisplayManager.cpp` / `include/DisplayManager.h`
- `src/StorageManager.cpp` — expose list metadata readers if missing
- `src/MidiButtonActions.cpp` — gestures still **TBD** (2.6); display can ship before buttons

Task **4.7** in M4 duplicates CURRENT/`From:`/toast — implement once in 2.13 and check off both.

---

## Step 4 — Task 2.14 (native loadSetIntoCurrent)

Test matrix:

| Case | Expect |
|------|--------|
| Dirty CurrentSet + load SavedSet N | Auto `saveNewSet` creates N+1; `_current` replaced from N; anchor cleared |
| Clean CurrentSet + load SavedSet N | No auto-save; `_current` replaced; `loadedFromSequence` = N |

**Testability note:** `loadSetIntoCurrent` is SD-backed. Options used elsewhere in repo:

- Refactor copy/load helpers behind injectable file ops (larger diff), or
- Native suite with temp directory + stub SD layer if one exists, or
- Split pure RAM copy logic into testable unit (preferred minimal surface).

Fixture paths: `test/test_saved_set_catalog`, `test/test_current_set_storage` as patterns.

---

## What is already committed (`f4acf2e`)

| Area | Summary |
|------|---------|
| **M1** | CurrentSet v6, deferred per-slot FSM, v5 migration, boot recovery stub |
| **M2 backend** | SetIndex, `saveNewSet`, `loadSetIntoCurrent`, 8h failsafe, SavedSet meta trailer |
| **Save status** | Sidebar spinner, `DeferredSaveDisplayStatus`, `#CAP,SAVE` telemetry |
| **Tests** | `test_current_set_storage`, `test_v5_migration`, `test_saved_set_catalog`, `test_save_status_display` — **205/205** native |
| **HITL** | Baseline **PASS** (`captures/host_midi_automation_baseline_20260626_192325.json`) on uploaded firmware |

### OpenSpec task checklist

**`workspace-session-persistence`:** 1.1–1.12, 2.1–2.12, 2.8–2.9, 6.1, 6.3 done · **2.13, 2.14, M3–M5, 6.2, 6.4–6.5** open

**`currentset-savedset-storage-layout`:** all open (validated OpenSpec)

**`save-status-display`:** code done · manual **4.3** + archive pending

---

## Constraints

- [`docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md)
- [`docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md`](../Guides/DEFERRED_RUNTIME_PERSISTENCE.md)
- Build/upload default: `teensy41-capture-serial` — ask before upload
- Do **not** run `capture_session.py` and HITL on the same serial port concurrently

---

## Suggested first prompt

> Read `docs/plans/hybrid_layout_workspace_m2_display_handoff.md`. Implement `currentset-savedset-storage-layout` task **1.1** (remove transport-stop full-slot dirty), add task **1.3** native coverage, run `pio test -e native`, then continue with `workspace-session-persistence` **2.13** and **2.14**.
