---
name: Loop-wrap punch-in alignment
overview: Arm new-slot recording on the next phase-0 boundary of the **currently playing** loop (captured as `previousSlot` before switching the active index), keep second-press immediate punch-in, and after stop for immediate takes apply bar-grid alignment using the same half-bar grace as today’s `stopRecording` length logic—typically via shifting recorded event ticks so pickup/onset material fits the loop origin.
todos:
  - id: pending-ref-slot
    content: "TrackManager: store pendingRecordRefSlot + queueRecordingTrack(track, slot, refSlot); clear everywhere pending is cleared"
    status: completed
  - id: handle-quantized-wrap
    content: "handleQuantizedStart: branch on ref slot — modulo wrap vs bar; fallback if ref invalid"
    status: completed
  - id: midi-actions-pass-ref
    content: "MidiButtonActions: pass previousSlot/baseSlot when queueing (short + hold paths)"
    status: completed
  - id: stop-align-pickup
    content: "Track: after immediate punch-in stopRecording path, bar-align origin via shiftMidiEvents + tests on device"
    status: completed
  - id: display-armed-pending
    content: "TrackManager::getTrackState show A whenever pendingRecord && (PLAYING || OVERDUBBING); verify OLED row"
    status: completed
isProject: false
---

# Loop-wrap punch-in and post-stop bar alignment

## Context (today)

- First press on an empty slot while **playing** calls [`queueRecordingTrack`](../../src/TrackManager.cpp); [`handleQuantizedStart`](../../src/TrackManager.cpp) only fires on **global bar boundaries** (`currentTick % barTicks == 0`), not on the playing loop’s wrap.
- Second press clears the queue and calls [`startRecordingTrack`](../../src/TrackManager.cpp) with `now` → [`Track::startRecording`](../../src/Track.cpp) sets `loop.startLoopTick = currentTick` (no bar snap in code despite the comment).
- [`stopRecording`](../../src/Track.cpp) quantizes **length** to whole bars using half-bar grace (`TICKS_PER_BAR / 2`); it does **not** adjust for pickup notes before a notional downbeat.

Reference for phase: you chose **the slot that was playing** → in [`handleToggleRecordForSlot`](../../src/MidiButtonActions.cpp) that is **`previousSlot` before** [`trackManager.setActiveLoopIndex`](../../src/TrackManager.cpp) (line 131). The reference loop geometry is [`Track::getLoop(previousSlot)`](../../src/Track.cpp) — use its `loopLengthTicks`, `startLoopTick` (same fields used in [`playMidiEvents`](../../src/Track.cpp) line 668).

## Desired behavior

1. **First press (empty target while playing):** Arm recording so it starts at the **next instant** where the **reference loop’s** phase hits 0 — i.e. next `currentTick` such that `(currentTick - ref.startLoopTick) % ref.loopLengthTicks == 0` (with the same “skip the queue tick” guard as today via [`pendingRecordQueuedAtTick`](../../src/TrackManager.cpp)). Optionally require `ref.loopLengthTicks > 0` and that `previousSlot` had material (or fall back to bar-based behavior if no valid ref).
2. **Second press:** Unchanged UX: immediate punch-in (`startRecordingTrack` now).
3. **After stop for immediate punch-in only:** Apply **bar alignment** consistent with existing stop logic: reuse the same grace idea as lines 390–401 in `stopRecording` to choose a **musical origin** (e.g. nearest bar boundary to `startLoopTick`), then **shift** all recorded events by a single signed tick delta so onset/pickup falls before tick 0 or early in the loop as intended. Exact rule to implement in code should mirror the length-quantization style (floor vs ceil bar with half-bar threshold) so behavior matches “current logic” you cited.

## Implementation outline

| Area | Change |
|------|--------|
| [`TrackManager`](../../src/TrackManager.h) / [`TrackManager.cpp`](../../src/TrackManager.cpp) | Extend pending state: store `pendingRecordRefSlot[track]` (or `0xFF` = use bar mode). [`queueRecordingTrack`](../../src/TrackManager.cpp) gains overload or parameters: `(trackIndex, targetSlot, refSlotForPhase)`. Clear ref slot in [`clearQueuedRecordingTrack`](../../src/TrackManager.cpp), [`finalizeCaptureAndSelectSlot`](../../src/TrackManager.cpp), [`handleTransportStop`](../../src/TrackManager.cpp). |
| Scheduling | Replace or branch [`handleQuantizedStart`](../../src/TrackManager.cpp): if `pendingRecordRefSlot[i] != 0xFF` and ref loop valid, evaluate wrap condition each `updateAllTracks` tick (cheap modulo on `currentTick`); on hit, `setActiveLoopIndex(target)`, `startRecordingTrack`, clear pending (same as today). Else keep **existing bar-only** path for backward compatibility (e.g. no ref / invalid ref). |
| [`MidiButtonActions::handleToggleRecordForSlot`](../../src/MidiButtonActions.cpp) | When queueing from playing + empty slot, pass `previousSlot` as ref (captured **before** `setActiveLoopIndex`). **Hold** path ([`beginSlotLayerHold`](../../src/MidiButtonActions.cpp)) should pass appropriate ref (likely `baseSlot` at hold start) for consistency. |
| [`Track`](../../src/Track.h) / [`Track.cpp`](../../src/Track.cpp) | Add a narrow API, e.g. `alignRecordedLoopOriginToBarsAfterImmediatePunchIn()` or flag set on `startRecording` / cleared after stop: only when **immediate** second-press path was used. Inside, after existing `stopRecording` length math (or factored helper), compute alignment delta and call existing [`shiftMidiEvents`](../../src/Track.cpp); then set `loop.startLoopTick = 0` as today. Guard: only if state transition is first-layer recording stop, not overdub layers, unless you explicitly want alignment on all stops (default: **first recording stop only**). |

## Edge cases to handle in implementation

- **Reference validity:** If `previousSlot` loop has `loopLengthTicks == 0` or no playback possible, fall back to current **bar** quantization (do not leave pending armed forever).
- **uint32 wrap:** Modulo math uses same assumptions as playback (`currentTick` and `startLoopTick` comparable).
- **Playback while active index is empty:** Today [`playMidiEvents`](../../src/Track.cpp) returns when the **active** loop has no events or zero length. After switching to an empty slot, audible loop from the **previous** slot may stop until layering or another mechanism applies. Verify on device; if arming mutes the reference loop, consider temporarily treating ref slot like [`heldLayerSlot`](../../src/TrackManager.cpp) playback for the armed window only (scope only if repro confirms).

## Verification

- Playing 4-bar loop on slot A, arm empty slot B: record starts on next **A wrap** (log `currentTick` and phase).
- Second press on B while queued: starts **now**, stops with bar-aligned content; intentional pickup before downbeat lands correctly in the stored loop.
- No ref / master-only scenarios: bar-based behavior unchanged.

## Files (expected)

- [`include/TrackManager.h`](../../include/TrackManager.h), [`src/TrackManager.cpp`](../../src/TrackManager.cpp)
- [`src/MidiButtonActions.cpp`](../../src/MidiButtonActions.cpp)
- [`include/Track.h`](../../include/Track.h), [`src/Track.cpp`](../../src/Track.cpp) (stop / shift alignment)

## Track state display (“A” while armed)

[`DisplayManager::drawTrackStatus`](../../src/DisplayManager.cpp) uses [`TrackManager::getTrackState`](../../src/TrackManager.cpp) → [`trackStateToLetter`](../../src/DisplayManager.cpp) maps `TRACK_ARMED` to **A**.

**In repo today:** `getTrackState` already returns `TRACK_ARMED` when `pendingRecord[trackIndex] && tracks[trackIndex].getState() == TRACK_PLAYING`. Any queued punch-in path that sets `pendingRecord` and leaves the track in **PLAYING** should show **A** on that track’s row.

**Why it can still look “broken” on hardware:**

- **OVERDUBBING:** The overlay only checks `TRACK_PLAYING`. If the track is in **`TRACK_OVERDUBBING`** when you queue another slot, the letter stays **O** until recording starts. **Plan fix:** treat `(PLAYING || OVERDUBBING)` the same for the `pendingRecord` → `TRACK_ARMED` mapping (todo `display-armed-pending`).
- **Build / flash:** Confirm the firmware running on the Teensy includes the `getTrackState` overlay (not an older binary).
- **Which row:** The letter is per **track index** (1–8), not per slot; armed is for the whole track while `pendingRecord` is set.

**Loop-wrap work:** Does not remove `pendingRecord`; once `queueRecordingTrack` runs, behavior is unchanged for display except as above. No separate OLED symbol for “armed for loop wrap” vs “armed for bar” unless you add one later.
