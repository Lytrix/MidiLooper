# MIDI button USB Host NoteOff delivery

**Status:** Implemented — native/build PASS; HITL pending  
**Evidence:** [`session_20260811_013056.log`](../../captures/session_20260811_013056.log), [`session_20260811_021117.log`](../../captures/session_20260811_021117.log)  
**Parent:** [`long_overdub_display_freeze_bugfix.md`](long_overdub_display_freeze_bugfix.md)

## Problem

USB Host button messages are not drained like USB-device / DIN MIDI. Under long capture load the DROID
can lose NoteOn or NoteOff at the processor.

| Capture | Evidence |
|---------|----------|
| `013056` | NoteOn without NoteOff; Stop Overdub never dispatched from Record button |
| `021117` | `Orphan note-off only: Ch16 Note36 (missed note-on), short press` then Stop Overdub |

`MidiHandler::handleMidiInput()` already drains USB device and DIN up to `kMidiInputBatchMax`, and is
called twice per `loop()`. USB Host still does one `usbHostMIDI.read()` per call.

Orphan NoteOff currently synthesizes a short press. That invents gesture duration and violates the
incomplete-press rule.

During `021117` record, inbound MIDI `Serial` DEBUG (~960 lines / 30s) left **zero `#CAP`** mid-pass
while USB CDC was saturated. Capture-build DEBUG flood is in scope for this RC as a delivery/telemetry
enabler.

## Debugging boundary

```text
USB Host packet queue
  → usbHost.Task + usbHostMIDI.read loop   ← RC3
  → MidiButtonProcessor gesture state
  → MidiButtonActions
display / capture append / persistence     ← out of scope
```

## Invariant

Host MIDI is drained in a bounded batch each service pass. Incomplete gestures (missing NoteOn or
NoteOff) dispatch **no** short/long/double/triple action. Duration cannot be reconstructed.

## Architecture checkpoint

- **Owners:** `MidiHandler` (USB Host input); `MidiButtonProcessor` (gesture state).
- **Ownership change:** NO.
- **State-transition change:** NO.
- **Reuse:** Existing `kMidiInputBatchMax` and dual `handleMidiInput()` call sites in `loop()`.

## Scope (this commit)

1. Drain USB Host with `while (reads < kMidiInputBatchMax && usbHostMIDI.read())`.
2. Raise `kMidiInputBatchMax` from 64 → 128 for USB/DIN/host.
3. Log when the host drain hits the batch cap (capture/debug).
4. Orphan NoteOff: discard — log only, no `handleButtonRelease` / no synthetic short press.
5. Keep lost-NoteOff recovery on a later same-button NoteOn: clear stale press, accept new press.
6. Gate inbound per-note MIDI `LOG_DEBUG` when `SESSION_CAPTURE` is defined (keep `#CAP,MI` and button INFO).

## Out of scope

- Synthetic NoteOff generation
- Capture stop / commit / persistence FSM
- Stage 5 append-pool exhaustion
- RC1 preview open identity; RC2 post-stop display handoff

## Acceptance

- Host drain loop present; cap-hit observable under burst.
- Orphan NoteOff emits no button action in logs after the change.
- Complete NoteOn/NoteOff short presses still dispatch.
- Capture-serial long record no longer floods Serial with per-note MIDI DEBUG.
- `pio test -e native` and `teensy41-capture-serial` build pass.
- HITL: Record/Overdub stop during long overdub without relying on Loop-slot override.
