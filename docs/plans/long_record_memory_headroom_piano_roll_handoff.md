# Handover — long-record memory headroom, then piano-roll window

**Date:** 2026-06-23 (updated after ship)  
**Branch:** `refactor/timeline-data-model`  
**Change 1:** **shipped** — commit `c6c4042`, archived at `openspec/changes/archive/2026-06-23-long-record-memory-headroom/`, spec at `openspec/specs/long-record-memory-headroom/spec.md`, guide at `docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md`.

**Apply order (locked):**

```
long-record-memory-headroom (shipped)  →  long-loop-piano-roll-window (next)
```

Display work is useless until 48/64-bar record + persistence no longer crash — **that gate passed** (48/64 record-only, 64+64 overdub, canonical 2+2 baseline).

---

## Why these two changes exist

Recording past ~32 bars crashed (USB serial drops). Root cause was **RAM2 heap exhaustion**, not stop-path timing:

| Run | `getFreeHeap` at stop | Persistence |
|-----|----------------------|-------------|
| 16-bar record-only | 77824 | `PERS,result,...,ok` |
| 48-bar record-only (pre-fix) | **4096** at `record_stop` entry | `PERS,dispatch` then silence, no `PERS,result` |

**Mechanism:** `ExtMemAllocator::allocate()` called `malloc()` (RAM2) first. Length-scaling buffers filled 512 KB RAM2 before 8 MB PSRAM. Event chunk pool was already PSRAM-first (`LoopEventStore::poolAlloc`).

`record-stop-64-bar-crash` is **archived** — superseded by `long-record-memory-headroom`. See `openspec/changes/archive/2026-06-22-record-stop-64-bar-crash/PARKED.md`.

---

## Change 1 — `long-record-memory-headroom` (shipped)

**Archive:** `openspec/changes/archive/2026-06-23-long-record-memory-headroom/`  
**Spec:** `openspec/specs/long-record-memory-headroom/spec.md`

### What shipped (M1 + M2)
- `PsramFirstAllocator` — PSRAM-first length-scaling buffers
- RAM2 safety-floor admission (`LoopEventStore::hasRam2HeadroomForNonCriticalWork`)
- Central deferred runtime save — one bounded SD slice per main-loop iteration (`StorageManager::requestDeferredSaveState` / `processDeferredSaveState`)
- Display `visualCache` fallback after overdub stop (transient empty piano roll)
- Boot stability after load (`stabilizeBootMemoryAfterLoad`, deferred-save active only during in-flight write)

### Key files
`PsramFirstAllocator.h`, `Loop.h`/`Loop.cpp`, `LoopPasses.cpp`, `TrackUndo.cpp`, `LoopEventStore.*`, `StorageManager.cpp`, `main.cpp`, `DisplayManager.cpp`

### Verification (done)
- Native: `pio test -e native` (146 tests)
- HITL: 48/64-bar record-only, 64+64 overdub, canonical 2+2 overdub baseline

---

## Change 2 — `long-loop-piano-roll-window` (do this next)

**OpenSpec:** `openspec/changes/long-loop-piano-roll-window/`  
**Plan:** `docs/plans/long_loop_piano_roll_overview_enhancement.md`  
**Apply:** `/opsx:apply` on `tasks.md`

### M1 — display-only
- Cap detailed piano roll at **16 bars** when loop > 16 bars
- **Fixed at loop start** (tick 0) until user moves in M2 — no auto-follow playhead
- Overview strip: binary note presence bins, **window box**, playhead on strip when outside detailed view
- Extend `#CAP DISP` or add `#CAP OVW` for HITL

### M2 — LOOP_EDIT navigation
- Move window along loop (fader 3 proposed); resize 1–16 bars (hold-turn encoder proposed). Control map TBD — see design open questions.

### Key files
`DisplayManager.cpp`/`DisplayManager.h`, `LoopEditManager.cpp`, `DebugSessionCapture.h`, HITL scripts

### Exit criteria
- Native window-filter + grouping tests
- HITL 32/64-bar display gates; no D1 regression (`edit-record-display-length-mode`)

### Non-goals
No storage/capture/persistence changes; no NOTE_EDIT piano-roll changes.

---

## Other in-flight work (same branch)

- `note-edit-session-undo-gpio` (GpioButtonManager, NoteEditSessionUndo)
- `edit-record-display-length-mode` (EditManager/DisplayManager live display) — active, see `STATUS.md`
- `overlap-hitl-track-c` (Track C HITL sign-off)

Keep commits focused per change when possible.

---

## Commands cheat sheet

```bash
# Host tests (before push)
pio test -e native

# Firmware build (default env for HITL)
pio run -e teensy41-capture-serial

# OpenSpec
openspec validate long-loop-piano-roll-window --strict

# HITL record-only — example 48-bar
.venv/bin/python scripts/host_midi_automation_baseline.py \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 \
  --track-number 5 --midi-channel 5 \
  --record-bars 48 --overdub-bars 0 --no-fixed-grid-notes --start-transport
```

Full 64+64 baseline: `.cursor/rules/HITL-Test-Flow.mdc`

---

## Suggested first message for new chat

> Apply `long-loop-piano-roll-window` M1 (16-bar detailed window + overview strip). Branch `refactor/timeline-data-model`. Read `openspec/changes/long-loop-piano-roll-window/tasks.md` and `docs/plans/long_loop_piano_roll_overview_enhancement.md`. Prerequisite `long-record-memory-headroom` is shipped.
