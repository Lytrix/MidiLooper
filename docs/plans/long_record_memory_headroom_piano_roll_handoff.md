# Handover — long-record memory headroom, then piano-roll window

**Date:** 2026-06-22  
**Branch:** `refactor/timeline-data-model`  
**Last commits:** `b5232bb` (gitignore), `db26344` (checkpoint WIP firmware — **not** OpenSpec artifacts)  
**Uncommitted:** both OpenSpec changes + plan + workflow rule + `record-stop-64-bar-crash/PARKED.md` — commit before starting apply if you want a clean base.

**Apply order (locked):**

```
long-record-memory-headroom  →  long-loop-piano-roll-window
```

Display work is useless until 48/64-bar record + persistence no longer crash.

---

## Why these two changes exist

Recording past ~32 bars crashes (USB serial drops). Root cause is **RAM2 heap exhaustion**, not stop-path timing:

| Run | `getFreeHeap` at stop | Persistence |
|-----|----------------------|-------------|
| 16-bar record-only | 77824 | `PERS,result,...,ok` |
| 48-bar record-only | **4096** (already at `record_stop` entry) | `PERS,dispatch` then silence ~30 ms, no `PERS,result` |

**Mechanism:** `ExtMemAllocator::allocate()` calls `malloc()` (RAM2) **first** (`include/Utils/ExtMemAllocator.h`). Length-scaling buffers (note cache, playback order, `materializeToFlat`, undo snapshots) fill the 512 KB RAM2 heap before 8 MB PSRAM. Event chunk pool is already PSRAM-first (`LoopEventStore::poolAlloc`).

`record-stop-64-bar-crash` is **parked** — superseded by `long-record-memory-headroom`. Its shipped WIP (in `db26344`) is reused: chunk-stream writer, `RECS`/`PERS` markers, progressive deferred-save experiment (M2 will harden/fix).

---

## Change 1 — `long-record-memory-headroom` (do this first)

**OpenSpec:** `openspec/changes/long-record-memory-headroom/`  
**Apply:** `/opsx:apply` on `tasks.md`

### M1 — root cause
- Add `PsramFirstAllocator<T>` (PSRAM-first, `malloc` fallback) — confirm name before implementing.
- Re-target to PSRAM-first: per-loop `noteCache_` / `playbackOrder_` (`Loop.h`), `materializeToFlat` temps, undo snapshot vectors.
- RAM2 safety-floor admission guard (`getFreeHeap()` floor); never block MIDI/clock/playback.

### M2 — defense in depth
- Harden `DeferredSaveStage` in `StorageManager.cpp` — every slice ≤ `CHUNK_CAPACITY`, fix 48-bar slice that never reaches `PERS,result`.
- `processDeferredSaveState` already moved outside `timingCriticalTrackActive` block in `main.cpp` (checkpoint).

### Key files
`ExtMemAllocator.h` (new sibling), `Loop.h`/`Loop.cpp`, `LoopPasses.cpp`, `TrackUndo.cpp`, `LoopEventStore.*`, `StorageManager.cpp`, `main.cpp`, `MemoryMonitor.*`, `scripts/host_midi_automation_baseline.py`

### Exit criteria
- Native: `pio test -e native`
- HITL: 48/64-bar record-only — `getFreeHeap` above floor at stop, `PERS,result,...,ok`
- HITL: 64+64 baseline — `STOPPED_RECORDING -> PLAYING -> OVERDUBBING`, persist + reload

### Rules
- Default firmware build: `teensy41-capture-serial`; ask before upload.
- No `getPsramFreeBytes()` / `logStatus()` on stop path.
- Read `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md` before storage edits.

---

## Change 2 — `long-loop-piano-roll-window` (after change 1 ships)

**OpenSpec:** `openspec/changes/long-loop-piano-roll-window/`  
**Plan:** `docs/plans/long_loop_piano_roll_overview_enhancement.md`  
**Apply:** `/opsx:apply` on `tasks.md` — **only after** change 1 HITL passes.

### M1 — display-only
- Cap detailed piano roll at **16 bars** when loop > 16 bars.
- **Fixed at loop start** (tick 0) until user moves in M2 — no auto-follow playhead.
- Overview strip: binary note presence bins (1/2/4/8/16/32/64-bar grouping), **window box**, playhead on strip when outside detailed view.
- Extend `#CAP DISP` or add `#CAP OVW` for HITL.

### M2 — LOOP_EDIT navigation
- Move window along loop (fader 3 proposed); resize 1–16 bars (hold-turn encoder proposed). **Control map TBD** — see design open questions.

### Key files
`DisplayManager.cpp`/`DisplayManager.h`, `LoopEditManager.cpp`, `DebugSessionCapture.h`, HITL scripts.

### Exit criteria
- Native window-filter + grouping tests
- HITL 32/64-bar display gates; no D1 regression (`edit-record-display-length-mode`)

### Non-goals
No storage/capture/persistence changes; no NOTE_EDIT piano-roll changes.

---

## Other in-flight work (same branch, do not mix)

Checkpoint `db26344` also contains unrelated WIP:
- `note-edit-session-undo-gpio` (GpioButtonManager, NoteEditSessionUndo)
- `edit-record-display-length-mode` (EditManager/DisplayManager live display)

Keep commits focused per change when possible.

---

## Commands cheat sheet

```bash
# Host tests (before push)
pio test -e native

# Firmware build (default env)
pio run -e teensy41-capture-serial

# OpenSpec
openspec validate long-record-memory-headroom --strict
openspec validate long-loop-piano-roll-window --strict

# HITL record-only (after upload) — example 48-bar
.venv/bin/python scripts/host_midi_automation_baseline.py \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 5 --midi-channel 5 \
  --record-bars 48 --overdub-bars 0 --no-fixed-grid-notes --start-transport
```

Full 64+64 baseline: `.cursor/rules/HITL-Test-Flow.mdc`

---

## Suggested first message for new chat

> Apply `long-record-memory-headroom` starting with M1 task 1.1 (`PsramFirstAllocator`). Branch `refactor/timeline-data-model`. Read `openspec/changes/long-record-memory-headroom/tasks.md` and `docs/plans/long_record_memory_headroom_piano_roll_handoff.md`. Do not start `long-loop-piano-roll-window` until 48/64-bar HITL passes for change 1.
