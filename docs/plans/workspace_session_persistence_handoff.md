# Handoff — workspace session persistence + storage layout

**Date:** 2026-06-26  
**Branch:** `refactor/timeline-data-model`  
**Tip:** `f4acf2e` — M1 + M2 backend + save-status display **committed**  
**Status:** M1 + M2 backend **shipped** · display/tests **pending** · hybrid layout **OpenSpec only**

**Next work:** [`hybrid_layout_workspace_m2_display_handoff.md`](hybrid_layout_workspace_m2_display_handoff.md) — task **1.1** first, then **2.13**, **2.14**, M3+.

Prior chat transcript:
[`19cef367-7beb-4322-98c6-3e619ef0449c`](../../.cursor/projects/Users-eelkejager-Documents-PlatformIO-Projects-250513-215524-teensy41/agent-transcripts/19cef367-7beb-4322-98c6-3e619ef0449c/19cef367-7beb-4322-98c6-3e619ef0449c.jsonl)

Use this doc to continue without re-reading the full prior thread.

---

## Goal

Replace monolithic `/midilooper_state.raw` (v5) with a layered SD model under `/Sets/`:

| Layer | SD path | Role | User-visible |
|-------|---------|------|--------------|
| **CurrentSet** | `Sets/_current/` | Live instrument; continuous deferred save | **CURRENT** row in browser (M2) |
| **SavedSet** | `Sets/260625_001/` or `Sets/00001/` | Intentional anchors (manual, auto-before-load, 8h failsafe) | SavedSet browser list |
| **RecoveryPoint** | `Sets/_current/checkpoints/_YYMMDD_HHMM/` | Crash recovery only | Hidden |

Vocabulary: **Set** on SD — not bare "Session" (avoids collision with **NoteEditSession**, future **JamSession**).

---

## What shipped (commit `f4acf2e`)

### M1 — CurrentSet persistence (v6)

| Item | Evidence |
|------|----------|
| `RtcTime` module | `src/RtcTime.cpp`, `include/RtcTime.h` |
| CurrentSet path helpers | `src/CurrentSetStorage.cpp`, `include/CurrentSetStorage.h` |
| Deferred FSM retarget | `StorageManager.cpp` → `Sets/_current/meta.bin` + `loop_TT_SS.bin` |
| Per-slot dirty tracking | `markCurrentSetLoopSlotDirty` on record/overdub/edit/clear hooks |
| v5 migration | load `/midilooper_state.raw` → write CurrentSet → quarantine legacy |
| Boot recovery stub | CurrentSet fail → RecoveryPoint → SavedSet → empty |
| Native tests | `test_current_set_storage`, `test_v5_migration` — **PASS** |
| Guide update | `docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md` (v6 layout, scheduler, sidebar) |

### M2 — SavedSet snapshots (backend)

| Item | Evidence |
|------|----------|
| SetIndex + reconcile | `SavedSetCatalog`, `StorageManager::writeSetIndexToSd` |
| `saveNewSet` / `loadSetIntoCurrent` | `StorageManager.cpp` — copy CurrentSet tree, metadata trailer |
| 8h failsafe | `processSavedSetFailsafe` in idle maintenance (`main.cpp`) |
| Dirty anchor hooks | record/overdub/edit/clear → `markCurrentSetMaterialChange` |
| Native tests | `test_saved_set_catalog` — **PASS** |

OpenSpec `workspace-session-persistence` tasks **1.1–1.12, 2.1–2.12, 2.8–2.9, 6.1, 6.3** checked in `tasks.md`.

### Snappy save refinements (fixes display hang vs early M1)

| Fix | Detail |
|-----|--------|
| Display before SD | `main.cpp`: `displayManager.update()` runs **before** `processDeferredSaveState` |
| Narrow display gating | `isDeferredSaveActive()` = `deferredSaveSdIoActive` only (SD I/O window), not whole job |
| PLAYING saves allowed | Removed deferral while `isPlaying()` — saves progress during playback |
| Capture-only block | Deferred save still blocked during `RECORDING` / `OVERDUBBING` |
| Clear race fix | `handleClearTrack()` drains pending save via `hasDeferredSaveWork()` + blocking `saveState()` |
| HITL telemetry | `PERS,result` plain `ok`/`failed`; details in `PERS,result_stats` |

**User policy (locked):** do **not** force urgent save at record/overdub stop; use PLAYING / second overdub / idle windows for SD slices.

**Open issue:** `TrackManager::handleTransportStop()` still calls `markAllCurrentSetLoopSlotsDirty()` — forces up to 64 slot rewrites on transport stop. Hybrid layout OpenSpec task **1.1** targets removing this for normal runtime.

### Save status display (sidebar spinner)

| Item | Evidence |
|------|----------|
| API | `include/DeferredSaveDisplayStatus.h`, `StorageManager::getDeferredSaveDisplayStatus` |
| Display | `DisplayManager::drawSaveStatusIndicator` — 4 dots, bottom-right sidebar |
| Telemetry | `#CAP,SAVE,<phase>,rotateStep` on phase transitions |
| Native tests | `test_save_status_display` — **PASS** |
| OpenSpec | `openspec/changes/save-status-display/` — task **4.3 manual** unchecked |

### HITL script fixes

| File | Fix |
|------|-----|
| `scripts/hitl/scenarios/edit_minimal.py` | CLI args, track select/clear/transport preconditions, record stop press, verifier without DEBUG-only markers |

`edit_minimal` scenario **PASS** after fixes (prior session).

---

**Native matrix:** `pio test -e native` → **205/205 PASS** (2026-06-26).  
**HITL baseline:** PASS — `captures/host_midi_automation_baseline_20260626_192325.json`.

---

## Active OpenSpec changes

| Change | Status | Apply |
|--------|--------|-------|
| `workspace-session-persistence` | M1 + M2 backend done; display/M3+ pending | `/opsx:apply` → **2.13** after hybrid **1.1** |
| `save-status-display` | Code done; manual 4.3 pending | Archive after hardware check |
| `currentset-savedset-storage-layout` | **OpenSpec only** — validated | `/opsx:apply` when ready (after or parallel to M2 planning) |

Validate:

```bash
openspec validate workspace-session-persistence
openspec validate save-status-display
openspec validate currentset-savedset-storage-layout
```

---

## Canonical SD layout (current + planned)

### CurrentSet (live — shipped M1)

```text
/Sets/_current/meta.bin              # v6 header, transport, track/slot meta, footer, undo
/Sets/_current/loop_TT_SS.bin        # 2-digit zero-padded, per dirty slot
/Sets/_current/checkpoints/_YYMMDD_HHMM/   # stub only until M4
```

### SavedSet (M2 — per-slot copy initially)

```text
/Sets/index.bin                      # SetIndex: nextSequence (starts at 1)
/Sets/260625_001/                    # date form when RTC ≥ 2026-01-01
/Sets/00001/                         # 5-digit UID when RTC invalid
```

M2 `saveNewSet` copies CurrentSet tree (per-slot files). **Hybrid layout change** (`currentset-savedset-storage-layout`) will switch SavedSet to packed **`meta.bin` + `loops.bin`** while keeping CurrentSet per-slot.

### Hybrid layout (planned — OpenSpec only)

| Scope | Files | Rationale |
|-------|-------|-----------|
| **CurrentSet** | `meta.bin` + per-slot `loop_TT_SS.bin` | Incremental dirty-slot writes |
| **SavedSet** | `meta.bin` + packed `loops.bin` | Low file count, fast snapshot |
| **Slot index** | Slot-summary rows in `meta.bin` | Browser/search without opening loop files |

See `openspec/changes/currentset-savedset-storage-layout/design.md` Decisions 1–5.

---

## Key design decisions (locked)

### Deferred persistence

- One FSM: `requestDeferredSaveState` / `processDeferredSaveState` — no sync `saveState()` on hot path.
- One bounded slice per main-loop call; chunk batch ≤ `CHUNK_CAPACITY`.
- Atomic per file: `.tmp` → verify `STORAGE_COMPLETE_MAGIC` → rename.
- Dirty flags clear only after `PERS,result,...,ok`.

### CurrentSet dirty anchor (`meta.bin`)

Fields: `loadedFromSequence`, `lastAnchoredSequence`, `lastMaterialChangeUnix`, `hasMaterialChangesSinceAnchor`.

- Set dirty on record / overdub / edit / clear / import.
- Cleared only by **saveNewSet** or **loadSetIntoCurrent** — deferred writes alone do **not** clear anchor dirty.

### Boot chain

CurrentSet → latest RecoveryPoint → newest SavedSet → empty default.

---

## Implementation milestones (remaining)

| Milestone | Focus | Status |
|-----------|-------|--------|
| **M1** | CurrentSet, deferred FSM, v5 migration, boot stub | **Done (uncommitted)** |
| **M2** | SetIndex, saveNewSet, 8h failsafe, loadSetIntoCurrent | **Backend done** · UI 2.13–2.14 pending |
| **M3** | Slot loop import (long-press → IMPORT LOOP) | Pending |
| **M4** | RecoveryPointManager, prune, full boot recovery, browser polish | Pending |
| **M5** | Favorites | Future |

**Hybrid layout** (parallel track): slot metadata index → SavedSet packed writer → migration/compat tests.

Coordinate `DisplayManager` browser work with **`long-loop-piano-roll-window`** M2 to avoid merge conflicts.

---

## Primary firmware files

| File | Role |
|------|------|
| `src/StorageManager.cpp` | Deferred FSM, dirty tracking, boot/migration, display status |
| `src/CurrentSetStorage.cpp` | Path helpers, meta.bin schema |
| `src/RtcTime.cpp` | SNVS time for `lastActiveUnix`, SavedSet naming |
| `src/main.cpp` | Display-before-save ordering |
| `src/DisplayManager.cpp` | Save spinner, future set browser |
| `src/TrackManager.cpp` | `handleTransportStop` full dirty (candidate for 1.1 fix) |
| `src/MidiButtonActions.cpp` | Clear-track save drain; future SAVE NEW gesture |
| `include/DeferredSaveDisplayStatus.h` | Spinner phase resolver (allocation-free) |

---

## Constraints

- [`docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md)
- [`docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md`](../Guides/DEFERRED_RUNTIME_PERSISTENCE.md)
- Naming: **Set**, **CurrentSet**, **SavedSet**, **RecoveryPoint**
- Build/upload default: `teensy41-capture-serial`; **ask before upload**

---

## Verification gates

| Gate | Command / when |
|------|----------------|
| Native | `pio test -e native` — **195 PASS** |
| HITL baseline | `scripts/host_midi_hitl.py run --preset base` + serial capture |
| HITL edit minimal | `scripts/hitl/scenarios/edit_minimal.py` |
| Save spinner manual | Record → PLAYING → watch Pending → rotate → Completed flash; `#CAP,SAVE` lines |
| OpenSpec validate | Before archive per change |
| Firmware build | `pio run -e teensy41-capture-serial` |

### HITL canonical command

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset base \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 5 --midi-channel 5 \
  --record-bars 2 --overdub-bars 2 \
  --no-fixed-grid-notes --start-transport \
  --overdub-start-delay-bars 0 --overdub-start-delay-beats 1 \
  --phase-wait-ms 500 --final-wait-ms 3000 --press-ms 120 \
  --undo-redo-delay-ms 3000
```

---

## Open questions / TBD

1. **Transport-stop full dirty** — remove `markAllCurrentSetLoopSlotsDirty` from `handleTransportStop`? (hybrid layout task 1.1)
2. **SavedSet format timing** — implement M2 with per-slot copy first, or jump straight to packed `loops.bin`?
3. **Slot-summary field set** — exact rows for browser (hybrid layout OpenSpec TBD)
4. **RecoveryPoint FSM** — extend deferred FSM vs separate queue (M4)
5. **DROID gestures** — SAVE NEW, load set, IMPORT LOOP (M2/M3 tasks)
6. **Commit strategy** — shipped as single commit `f4acf2e`; next work split per [`hybrid_layout_workspace_m2_display_handoff.md`](hybrid_layout_workspace_m2_display_handoff.md)

---

## Suggested first prompt

> Read `docs/plans/hybrid_layout_workspace_m2_display_handoff.md`. Implement hybrid layout task **1.1**, then workspace **2.13** and **2.14**.
