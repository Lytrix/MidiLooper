---
name: Fix slot arm-record flow
overview: "Implement a slot-aware recording UX that supports your selected behavior: empty slot while playing schedules record on next wrap, second press in ARMED starts immediate punch-in, and hold enables temporary multi-slot layering. Also fix the stuck RECORDING state on transport stop."
todos:
  - id: slot-pending-state
    content: Add per-slot pending armed/record state in TrackManager and wire quantized start to target slot
    status: completed
  - id: slot-button-flow
    content: "Implement explicit slot button flow: empty+playing => arm next-wrap, armed+second press => immediate punch-in"
    status: completed
  - id: hold-layering
    content: Add hold gesture behavior for temporary multi-slot simultaneous playback
    status: completed
  - id: stop-state-fix
    content: Fix transport stop handling to clear stuck RECORDING/ARMED conditions per slot
    status: completed
  - id: zero-length-guard
    content: Guard note reconstruction against loopLength=0 spam and verify logs
    status: completed
  - id: verify-scenarios
    content: Run manual scenario checks for slots 1/2/3 and transport stop behavior
    status: completed
isProject: false
---

# Slot Arm/Record UX and State Fix Plan

## Goals
- Make per-slot record flow deterministic and musical.
- Keep current single-active-slot playback as default.
- Add temporary multi-slot layering only while holding the target slot button.
- Fix the case where recording can get stuck after stop.

## Confirmed behavior to implement
- First short press on an **empty** slot while playing: **schedule recording at next wrap**.
- Second short press while slot is **ARMED**: **start recording immediately** (punch-in now).
- Playback scope: **single active slot by default**; while **holding** another slot button, keep currently playing slot sounding and arm/record target slot as overlay.

## Files to change
- [src/MidiButtonActions.cpp](../../src/MidiButtonActions.cpp)
- [include/Track.h](../../include/Track.h)
- [src/Track.cpp](../../src/Track.cpp)
- [include/TrackManager.h](../../include/TrackManager.h)
- [src/TrackManager.cpp](../../src/TrackManager.cpp)
- [src/Utils/MidiButtonConfig.cpp](../../src/Utils/MidiButtonConfig.cpp) (if hold mapping needs adjustments)

## Implementation outline
1. Add per-slot pending-record state in Track/TrackManager
- Introduce per-track/per-slot pending flags instead of only track-level `pendingRecord[track]`.
- Ensure quantized start consumes `pendingRecord` for the **target slot**, not whichever slot becomes active later.

2. Make slot button state machine explicit in MidiButtonActions
- In `handleToggleRecordForSlot(slot)`:
  - If slot has no data and track is playing: set slot to ARMED+pending-next-wrap.
  - If slot is ARMED: second short press starts immediate recording (clear pending).
  - Keep existing record stop / overdub stop branches, but slot-aware.
- Prevent accidental transition to overdub for empty slots.

3. Add hold-to-layer behavior
- On long-press/hold for target slot:
  - Keep currently audible active slot playing.
  - Activate target slot as layered candidate for arm/record.
  - Route playback to include both current playing slot and held slot only while held.
- On release: return to single active slot playback.

4. Fix stuck RECORDING on stop
- In transport stop path (`TrackManager::handleTransportStop`), enforce consistent teardown for all slot-recording permutations:
  - RECORDING -> stopRecordingToStopped.
  - ARMED with pending -> clear pending and set EMPTY/STOPPED appropriately for that slot.
  - Ensure pending flags are cleared for all slots so restart cannot re-enter stale recording behavior.

5. Add guardrails for loop-length 0 reconstruction spam
- The logs show frequent note reconstruction with `loop length: 0` and wrapped ticks at large unsigned values.
- Add early return in note reconstruction call sites when `loopLengthTicks == 0` to avoid invalid wrap/debug spam during pre-record/armed states.

## Validation plan
- Case A: Slot 1 playing, short press empty slot 2 -> ARMED, starts recording exactly at next wrap.
- Case B: Slot 2 ARMED, second short press before wrap -> immediate punch-in.
- Case C: Hold slot 2 while slot 1 is playing -> both audible during hold, release returns to single-slot playback.
- Case D: Start recording slot 3, stop transport -> no stuck RECORDING; can return to STOPPED/EMPTY and continue normally.
- Case E: Confirm no repeated `loop length: 0` reconstruction spam while idle/armed.

## Notes
- This preserves your current architecture and adds minimal new state instead of introducing a broad new global track state model.
- Multi-slot playback remains temporary and gesture-driven (hold), matching your requested UX.