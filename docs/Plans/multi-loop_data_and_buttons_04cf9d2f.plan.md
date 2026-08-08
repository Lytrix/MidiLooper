---
name: Multi-loop data and buttons
overview: "Reorder the multi-loop plan: implement the Loop struct and loops[8] data structure first, then add loop button logic (record/overdub/play/stop/delete) per slot. D5 switch-only is folded into D11 since switching cannot be tested without multi-slot recording."
todos: []
isProject: false
---

# Multi-loop data structure and button logic

## Rationale

Switching between filled slots (old D5) cannot be tested until multiple slots can hold data. So: **data structure first**, then **full button logic** per slot. No intermediate "switch-only" phase.

## Implementation order

### Step 1: Loop struct + loops[8] (D10)

- Add `Loop` struct per [phase-3-multi-loop.md](phase-3-multi-loop.md) §2.1:
  - MIDI event list, `loopLengthTicks`, `loopStartTick`, `startLoopTick`
  - Note cache / `playbackOrder` / dirty flags as needed
  - Slot state: empty | recording | playing | overdubbing
- Refactor `Track` to hold `Loop loops[MAX_LOOPS_PER_TRACK]` instead of a single `midiEvents` buffer
- Migration: slot 0 from current `Track` data; slots 1–7 empty
- Storage format bump (v3 or v4) to persist per-slot data
- `activeLoopIndex` selects which slot is audible; playback/recording target uses that slot

### Step 2: Loop button logic (D11 + D12 combined)

- Register **ch16 notes 50–57** as loop buttons in [MidiButtonConfig.cpp](../../src/Utils/MidiButtonConfig.cpp)
- Full gesture map per slot (matches Button A / note 36):
  - **Short** → `TOGGLE_RECORD_FOR_SLOT`: empty→arm/record, recording→stop+play, playing→overdub, overdubbing→stop
  - **Long** → `CLEAR_TRACK` (slot-scoped)
  - **Double** → `UNDO` (slot-scoped)
  - **Triple** → `REDO` (slot-scoped)
- Switch precedence: when short press would *only* change view (filled slot, not recording) → set `pendingActiveLoopIndex`, 16th-quantized commit
- Recording writes to active slot; playback reads from active slot (D12)
- Wire in [MidiButtonActions.cpp](../../src/MidiButtonActions.cpp), [MidiButtonManager.cpp](../../src/MidiButtonManager.cpp)
- Per-slot undo/redo stacks (or delegate slot 0 to existing TrackUndo until full per-slot stacks exist)

## Plan file changes

Update [multi-loop_leds_and_droid_lfo_3a62f325.plan.md](multi-loop_leds_and_droid_lfo_3a62f325.plan.md):

- **Remove D5** as a standalone deliverable; its switch logic is absorbed into D11
- **D11 dependency:** D10 only (remove D5)
- **Suggested order for slice 2:** D10 → D11 (with D12 folded in — recording/playback target is part of making D11 work)
- Add note in §4.2: "D5 switch-only folded into D11; no separate switch-only phase."

## Slice 1 remainder (unchanged)

- D3 (Storage v3 — activeLoopIndex only; per-slot storage is a larger v4 bump with D10)
- D8 (Droid LFO patch)
- D9 (Teensy LFO arm)

Note: D3 persists `activeLoopIndex` only. A full storage v4 for per-slot loop data will come with D10.
