> **Historical — the legacy runtime (removed in E9.8d, 2026-11-11).** This guide documents the legacy v3 runtime, its owners and its test tooling, none of which is in the firmware any more; `teensy41-capture-serial` now builds the fixed-memory engine. Kept as history: do not apply its APIs, limits, gestures or procedures to the engine. Current runtime: [FIXED_MEMORY_ENGINE.md](../FIXED_MEMORY_ENGINE.md) (structure, limits, diagnostics), [ENGINE_UI_UX_GUIDE.md](../ENGINE_UI_UX_GUIDE.md) (controls and screens), [ENGINE_STORAGE.md](../ENGINE_STORAGE.md), [ENGINE_TIMING.md](../ENGINE_TIMING.md).

# Scenes (control surface)

**Applies to:** v3 (`dev`) — row reserved; scenes not in firmware yet (roadmap).

**Scenes** row on the reference **8×8** grid is reserved for **scene** or project snapshot workflows (Phase 3 / multi-loop direction). Behavior and MIDI mapping are **not** fully wired in the current firmware; see **[`../../FEATURE_PLANS.md`](../../FEATURE_PLANS.md)** and **[`../../Plans/phase-3-multi-loop.md`](../../Plans/phase-3-multi-loop.md)**.

When implemented, note/CC layout will live in [`include/MidiConfig.h`](../../../include/MidiConfig.h) and [`MIDI_CONFIG_GUIDE.md`](../MIDI_CONFIG_GUIDE.md).
