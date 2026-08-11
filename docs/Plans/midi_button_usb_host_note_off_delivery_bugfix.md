# MIDI button USB Host NoteOff delivery

**Status:** Planned — RC3  
**Evidence:** [`session_20260811_013056.log`](../../captures/session_20260811_013056.log)

## Problem

The Record/Overdub press at 522.889 seconds delivered USB Host Ch16 Note36 NoteOn but no matching
NoteOff. The release-triggered gesture therefore did not dispatch Stop Overdub. A later MIDI
Transport Stop ended overdub.

`MidiHandler::handleMidiInput()` drains USB and serial MIDI in bounded batches, but calls
`usbHostMIDI.read()` only once per main-loop pass.

## Invariant

Input delivery must be serviced without inventing gesture meaning. If a NoteOff is absent, the
button duration is unknowable and the incomplete gesture dispatches no action.

## Architecture checkpoint

- **Owners:** `MidiHandler` for USB Host input; `MidiButtonProcessor` for gesture state.
- **Ownership change:** NO.
- **State-transition change:** NO.
- **Gesture policy:** Preserve stale-press discard. Never synthesize short, long, double, or
  triple action from an incomplete press.

## Scope

1. Instrument USB Host read depth and bounded-drain cap hits.
2. Drain USB Host MIDI up to the existing input batch bound per service pass.
3. Keep the current stale same-button NoteOn recovery: discard prior state, then accept the new
   press.
4. Add native gesture tests proving incomplete presses emit no action and subsequent complete
   presses still work.
5. Re-run hardware input capture. If NoteOff loss occurs without cap pressure, investigate the
   controller/USB transport instead of adding gesture recovery.

## Out of scope

Capture state transitions, stop/commit behavior, and synthetic NoteOff generation.
