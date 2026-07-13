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
- **Split focus (2026-07-09)** — playing slot vs preview slot vs MIDI-safe edit commit (see design D18–D21)
- **Boot (DEC-021 amendment)** — queue all 64 SD loop payloads cooperatively at boot
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
- **Preview vs playing (2026-07-09):** display + edit commit immediate on preview; LEDs/phase on playing slot until commit

## Non-Goals

- Per-slot `TrackState`
- Jam D13 capture into slot
- Multi-window stacking
- **Multi-slot overdub** — capturing overdub passes into more than one slot at once (or overdub on a non-active slot while others play). Too complex; overdub remains **Record button only**, scoped to the **single active capture slot** (`activeLoopIndex`). Layered **playback** of multiple enabled slots may continue; only one slot receives live overdub capture.

## Layered multi-slot playback (today vs target)

**Shipped today (legacy, pre-gesture-remap):** hold-to-build enabled-set (`beginSlotLayerHold` / `pendingMultiSlotCommit`) plays multiple slots via `playMidiEvents` + `playMidiEventsForSlot`. This is **playback layering only** — not overdub, not chain sequencing.

**Target (`slot-performance-interaction`):** slot long-press → `LoopTriggerSequence` chain; remove hold-layering on loop buttons. Until LTS ships, document legacy hold behaviour in [`Loops.md`](../../../docs/Guides/control-surface/Loops.md) and treat layered playback as a **display + projection** concern (see design D23), not a capture feature.
