# Proposal — timeline pass model (remove Take)

**Change:** `timeline-pass-model`  
**Status:** Proposed (vocabulary locked 2026-06-20)  
**Depends on:** `m8-pass-vocabulary`  
**Brownfield:** [`docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../../../docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md)

## Why

**Take** duplicates scoped **pass** vocabulary (**recordPass**, **overdubPass**, **noteEditPass**).
Loop history is split across `takes[]` and `edits[]` with two-phase `applyEdits` replay. Product
language should be: **capture** a **pending** pass → **commit** into **`passes[]`** — without
overloading **committed** on pass nouns, containers, or undo kinds.

## What Changes

- **BREAKING:** Remove **Take** / `TakeId` / `takes[]` / `commitTake()` / **TakeCommitted**.
- **Pass vocabulary rule:** **committed** is **not** used for pass state, `passes[]`, or pass undo
  enum names. Only **pending** is qualified; membership in **`passes[]`** means the pass is settled.
- **Capture** remains the live append writer; **pendingCapturePass** holds the pass until
  `commitRecordPass()` / `commitOverdubPass()` moves it into **`passes[]`**.
- **`passes[]`**: **recordPass**, **overdubPasses[]**, **editPasses[]** (**editPass** +
  **`EditPassKind::NoteEdit`** | **`ControlChange`** — four pass **kinds**, two storage **families**).
- **saveNoteEditPass()** appends note **editPass** rows; CC kind + **saveControlChangeEditPass()**
  anticipated (stub in apply, no UI in this change).
- Global undo kinds: **RecordPassAdded**, **OverdubPassAdded**, **NoteEditPassClosed** (replaces
  **TakeCommitted** / **NoteEditSessionCommitted**).
- **`Loop::materializePasses()`** (name TBD in apply) replaces `applyEdits(takes, edits)`.
- SD v4/v5: Take wire → **recordPass** + **overdubPass** list; dual-read one release.
- Update naming rule + storage guide.

## Capabilities

### New Capabilities

- `timeline-passes`: pending → **passes[]** lifecycle, Take removal, materialize contract, undo rename.

### Modified Capabilities

- `timeline-epochs`: **Take** / **TakeCommitted** requirements superseded.

## Impact

Core: [`Loop.h`](../../../include/Loop.h), [`Loop.cpp`](../../../src/Loop.cpp), [`EditApply.cpp`](../../../src/EditApply.cpp),
[`TrackUndo.cpp`](../../../src/TrackUndo.cpp), [`StorageLoopIo.cpp`](../../../src/StorageLoopIo.cpp), tests, HITL grep.

## Non-Goals

- **controlChangeEditPass** implementation (name slot only).
- Empty-loop bootstrap UX (storage may allow no **recordPass**; guards separate).
- Chronological replay (O2) — deferred change.
- **committed** in unrelated domains (slot switch, pre-commit resolve, etc.) — unchanged.
