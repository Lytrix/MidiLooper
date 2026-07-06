## Why

Loop slot buttons today mix performance, capture, and management gestures: mute without note-offs, `loopStartTick` edits desync playback from display, and slot buttons duplicate record/overdub/undo/redo that belong on the Record button. Evidence: [`captures/session_20260706_120450.log`](../../../captures/session_20260706_120450.log).

This change separates **slot performance** (launch, mute, restart) from **capture/history** (Record button only), wires **long press** to **`LoopTriggerSequence`** chain entry (UIP Phase 7), adds **SlotActionQueue**, and aligns **PlaybackWindow** vocabulary.

Architecture: [`docs/plans/slot_playback_window_interaction_architecture.md`](../../../docs/plans/slot_playback_window_interaction_architecture.md).

## Domain model

| Concept | Meaning |
|---------|---------|
| **Loop** | Persistent MIDI pass storage |
| **Slot** | `LoopId` + persisted **PlaybackWindow** metadata |
| **PlaybackWindow** | Active movable segment over a Loop |
| **PlaybackMergedMidiEvents** | Engine merge cache (rename from `PlaybackWindow.h` today) |

## What Changes

### Prerequisite rename (Phase −1)

- `struct PlaybackWindow` (merge cache) → **`PlaybackMergedMidiEvents`**
- `Track::invalidatePlaybackWindow` → **`invalidatePlaybackMergedMidiEvents`**
- `LoopPlaybackRuntime::primaryWindow` field → `mergedMidiEvents` (or equivalent)

### Slot gestures (BREAKING) — notes 50–57

| Gesture | Behaviour | Quantisation |
|---------|-----------|--------------|
| Short | Launch / mute / select empty | **`LoopEnd`** for launch & mute |
| Double | Launch (other) / restart (selected) | **`NextGrid`** |
| Long press | **`LoopTriggerSequence` chain** (UIP Phase 7) | Build: long A + short B; play: cue sequence |
| Triple | **Slot management overlay** | Immediate |
| Very long (~4s) | **Delete slot** + **global undo** checkpoint | Immediate |

**Removed from slot buttons entirely:** arm, record, overdub, undo, redo, immediate clear on long release.

### Record button (36) — unchanged ownership

Short / double / triple / long remain: record-overdub / undo / redo / clear — per [`MidiButtonConfig.cpp`](../../../src/Utils/MidiButtonConfig.cpp).

### Other

- **`SlotActionQueue`** — `LaunchSlot`, `RestartSlot`, `MuteSlot`, `UnmuteSlot` with per-action quantisation
- Note-offs on mute, disable, delete, relaunch
- LOOP_EDIT PlaybackWindow metadata resync while playing
- Global undo on destructive slot ops (delete, overlay replace/clear)

## Capabilities

- **New:** `slot-performance-interaction`
- **Modified:** `multi-loop-slots` (spec id only; prose: loop slots)

## Locked decisions (2026-07-06)

- Capture/history: **Record button only**
- Long press slots: **`LoopTriggerSequence` chain** (not multi-hold layering; not overlay)
- Very long slots: **delete** + global undo
- Overlay: **triple press**
- Short launch/mute: **`LoopEnd`**; double restart/launch: **`NextGrid`**
- Merge cache name: **`PlaybackMergedMidiEvents`**

## Non-Goals

- Per-slot `TrackState`
- Jam D13 capture into slot
- Multi-window stacking
