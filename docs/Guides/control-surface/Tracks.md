> **Historical — the legacy runtime (removed in E9.8d, 2026-11-11).** This guide documents the legacy v3 runtime, its owners and its test tooling, none of which is in the firmware any more; `teensy41-capture-serial` now builds the fixed-memory engine. Kept as history: do not apply its APIs, limits, gestures or procedures to the engine. Current runtime: [FIXED_MEMORY_ENGINE.md](../FIXED_MEMORY_ENGINE.md) (structure, limits, diagnostics), [ENGINE_UI_UX_GUIDE.md](../ENGINE_UI_UX_GUIDE.md) (controls and screens), [ENGINE_STORAGE.md](../ENGINE_STORAGE.md), [ENGINE_TIMING.md](../ENGINE_TIMING.md).

# Tracks (control surface)

**Applies to:** v3 (`dev`) — DROID 8×8 grid, track row.

**Tracks** row: **eight** buttons on the reference grid — one **track** each (typical mapping: Channel 16 notes 60–67; confirm in [`include/MidiConfig.h`](../../../include/MidiConfig.h) and [`MidiButtonConfig.cpp`](../../../src/Utils/MidiButtonConfig.cpp)).

## Gesture model (select-primary)

| Gesture | Action |
|---------|--------|
| **Short** | Select that track |
| **Double** | Mute / unmute that track |
| **Long** | Solo / unsolo (exclusive solo; long again on the same track clears solo) |

**Rationale:** Select is the primary action. Double = mute and long = solo keep secondary actions deliberate.

**Implementation:** `SOLO_TRACK` → `MidiButtonActions::handleSoloTrack` → `TrackManager::toggleSoloTrack`. Audibility: `TrackManager::isTrackAudible` (per-track mute + solo mask).

## Not the same as MUTE/DE

The legacy **MUTE/DE** strip control (e.g. note 37, Channel 16) uses **short = next track**, **long = mute current**, **double/triple = undo/redo clear**. See **[`Main-controls.md`](Main-controls.md)** and [`MIDI_CONFIG_GUIDE.md`](../MIDI_CONFIG_GUIDE.md).

## LED feedback

Track row LED feedback uses **Channel 15** with the same note numbers as the button row (see **MIDI_CONFIG_GUIDE** — LED section).
