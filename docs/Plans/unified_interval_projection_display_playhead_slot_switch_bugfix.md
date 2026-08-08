# Display playhead slot-switch alignment — bugfix handoff

**Date:** 2026-07-06  
**Trigger:** [`captures/session_20260706_014145.log`](../captures/session_20260706_014145.log) — after slot 0↔1 switch while playing, OLED position lagged audible MIDI by ~1 bar.

## Root cause

UIP Phase 4 moved MIDI playback phase to **`projectionCycleStartTick`** (re-anchored on slot-switch grid commit). **`DisplayManager::resolvePlayheadInLoop`** and **`MidiLedManager`** still used **`startLoopTick`** (record-time origin). After **`commitQueuedPlaybackStart`**, those anchors diverge.

## Shipped fix

| Area | Change |
|------|--------|
| `DisplayManager.cpp` | Active playing slot: `tickPhaseInProjectionCycle` + `noteRelativeTick`; `invalidateForSlotChange` centers detailed window during playback |
| `MidiLedManager.cpp` | Same projection-cycle alignment for 16th/bar LEDs and bar index |
| `test_interval_projection` | `test_display_playhead_aligns_with_projection_cycle_after_slot_commit` |

OpenSpec: UIP **D25**, tasks **5.3a**; `slot-selection-focus` spec § display cache invalidation.

## Verification

- `pio test -e native` — full suite PASS
- Manual: play slot 0, short-press slot 1 while playing — position string and playhead match heard notes after grid commit (pending hardware)
