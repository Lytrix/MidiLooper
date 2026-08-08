# Clear slot re-arm after playing clear — bugfix

**Status:** Done (shipped `1cb7ffa` on `chore/codebase-hygiene-sprint1`)  
**Evidence:** [`captures/session_20260803_170251.log`](../../captures/session_20260803_170251.log)

## Symptom

After long-press Record/Overdub clears the selected slot, short-press Record no longer arms. Log shows `MIDI: Clear selected slot` then `Record/Overdub (short)` with no `Start Recording` / queue / arm line.

## Root cause

1. `Track::reconcileTransportStateAfterSlotMutation` left `TRACK_PLAYING` when the cleared active slot was empty but siblings still had data.
2. `handleToggleRecordForSlot` used SD-inclusive `slotHasLoopContent` for the playing+selected mute path, so deferred-save SD payload made a just-cleared slot look filled → silent mute toggle instead of re-arm.

## Fix

- Reconcile: `PLAYING` + empty active → `STOPPED`
- `restorePlaybackAfterSlotClear`: if no replacement enabled slot, force `STOPPED`
- Mute path requires RAM `hasDataInSlot`; cleared slot falls through to arm

## Architecture checkpoint

- Ownership change? **NO**
- State transition change? **YES** — `PLAYING → STOPPED` when active has no RAM data (valid existing transition; corrects stuck state)
