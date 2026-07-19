# Persistence / Overlay Large Slot Focus Restore Bug Hunt

Status: parked / unresolved, 2026-07-19.

## Symptom

While transport is PLAYING, quickly moving track focus and selecting a large still-loading slot can leave the OLED selected-slot display stale, draw no note events, or freeze the Teensy. MIDI playback can continue for some variants, but the main UI path stops updating. The most useful repros involved large slots around 64-65 bars:

- Track index 6, slot 0 / Loop 1, length 49920 ticks.
- Track index 1, slot 3 / Loop 4, length 49152 ticks.

## Proven Fix From This Hunt

The earlier soft hang where bar LEDs kept updating but 16th LEDs stopped was traced to `SC_CAPTURE_FLUSH` / USB serial pressure. Keep these changes:

- Timing-critical CAP flushes must not call blocking `Serial.println`.
- CAP flush under `maxRecords <= 8` may drop non-critical records rather than writing USB serial.
- Overflow notices must stay pending when USB write room is unavailable.
- `emitCapLineOrSerial()` must not fall back to blocking direct serial.
- No full CAP ring draining on PLAYING hot paths.

Evidence: after those changes, the original `SC_CAPTURE_FLUSH` hang from `captures/session_20260719_012717.log` stopped reproducing in `captures/session_20260719_013911.log`.

## Rejected Paths

Do not re-apply these as fixes:

- Forcing `DisplayManager::update()` during an active focused `SlotLoadSession`.
  - It allowed `#CAP,DSEL,paint` to appear but caused an empty display and a freeze.
  - Evidence: `captures/session_20260719_015955.log` painted selected slots, including `DSEL,paint,1,3`, but the user saw no events and the Teensy froze.
- Moving `DisplayManager::update()` to `FLASHMEM`.
  - This was only done to fit heavy debug markers in RAM1. It changed timing and must not be treated as a fix.
- High-frequency display-stage markers (`DSEL`, `DPNT`) and load-progress markers (`QSW,load_prog`).
  - These changed timing enough that the bug stopped reproducing.
  - Evidence: `captures/session_20260719_020618.log` and `captures/session_20260719_020834.log`.

## Runtime Evidence

`captures/session_20260719_015710.log`:

- `TrackManager::setSelectedSlotIndex()` completed fully for Loop 1 and Loop 2 while PLAYING.
- `SSEL` reached `selected_changed_leave`, `invalidate_leave`, `led_leave`, `footer_req`, and `exit`.
- `queuePlayingSlotSwitch()` reached `queue_prewarm_done` and `queue_exit`.
- Conclusion: selected-slot state mutation and queue setup returned; the failure was downstream of selection state.

`captures/session_20260719_015955.log`:

- After removing the focused-load display skip, the display painted selected slots.
- For Loop 4, `SSEL` completed, `LoadLoopJob` began, and `DSEL,paint,1,3` appeared during the load.
- User observed no events drawn and an entire Teensy freeze.
- Conclusion: painting selected slot before focused restore commit is unsafe for this large-slot path.

`captures/session_20260719_020438.log`:

- Normal selected-slot paints reached `DPNT,oled_leave`.
- The failing large-slot path ended at `[StorageManager] LoadLoopJob done 1/3`.
- There was no later `DSEL,committed` or display-stage marker.
- Conclusion: the freeze happened before the next display frame, after `LoadLoopJob` commit reached the done print.

`captures/session_20260719_020834.log`:

- With reduced but still active markers, the large-slot path completed storage cleanup, same-turn prewarm, and loop-end commit markers.
- Because the bug did not reproduce, the remaining markers were still affecting timing.

`captures/session_20260719_021041.log`:

- After further timing changes, track index 6 / slot 0 reached `LoadLoopJob done 6/0` after `queue_exit`.
- This capture is useful as a near-repro boundary for the large slot on Track 7 / Loop 1.

## Current Code State After Parking

The intrusive diagnostic markers were removed from production sources:

- `MidiButtonActions.cpp`: removed `debugQueueSwitchMarker()`.
- `TrackManager.cpp`: removed selected-slot and loop-end commit marker helpers.
- `StorageManager.cpp`: removed `debugLoadLoopJobProgress()` and load-progress calls.
- `main.cpp`: removed `MLDF` markers.
- `DisplayManager.cpp`: removed `DSEL` / `DPNT` markers and restored `DisplayManager::update()` to normal placement.

The behavior change that forced display update while focused slot load was active was reverted. The scheduler still skips OLED while `focusSlotRestoreWork && SlotLoadSession::isActive()`.

## Next Resume Point

Start from the boundary proven by `captures/session_20260719_020438.log` and `captures/session_20260719_021041.log`:

- The storage load reaches `LoadLoopJob done`.
- The selected-slot and queue paths return.
- Heavy serial instrumentation changes timing enough to hide the bug.

Use non-serial or near-zero-cost evidence next. Good candidates:

- A tiny RAM-only breadcrumb word updated at key boundaries and dumped only after reboot or explicit HITL command.
- GPIO pulse / LED pulse around `LoadLoopJob` cleanup and `Track::ensurePlaybackMergedEventsForSlot()`.
- One single direct marker only after `LoadLoopJob done`, not per-read / per-frame.

Avoid:

- `Serial.println` on the PLAYING focus-restore path.
- CAP marker bursts inside `stepLoadLoopJob()`, `setSelectedSlotIndex()`, or display update.
- Forcing display repaint before the focused slot restore has committed.

