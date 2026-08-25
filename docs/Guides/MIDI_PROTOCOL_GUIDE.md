# MIDI protocol guide

**Applies to:** current v3 (`dev`) firmware and the MIDI 1.0 transport paths.

**Normative source:** [M2-104-UM UMP Format & MIDI 2.0 Protocol v1.1.2](https://amei-music.github.io/midi2.0-docs/amei-pdf/M2-104-UM_v1-1-2_UMP_and_MIDI_2-0_Protocol_Specification.pdf), published 2023-11-10.

This guide records the parts of the protocol that affect MidiLooper’s input,
output, note identity, timing, and overlap-resolution design. It is a project
mapping guide, not a replacement for the MIDI Association and AMEI
specification.

---

## Current firmware scope

The firmware currently uses MIDI 1.0 channel-voice messages through the
Teensy MIDI APIs:

- USB MIDI device input/output;
- USB Host MIDI input/output;
- DIN serial MIDI input/output;
- 7-bit MIDI note, velocity, controller, and program values;
- MIDI channels 1–16.

The current firmware does **not** parse or emit Universal MIDI Packets (UMP),
MIDI 2.0 channel-voice messages, Groups, Jitter Reduction timestamps,
Delta Clockstamps, MIDI-CI endpoint configuration, or MIDI 2.0 per-note
messages. This is an explicit scope boundary, not an incomplete MIDI 2.0
implementation.

For channel, note, controller, and control-surface mappings, see
[`MIDI_CONFIG_GUIDE.md`](MIDI_CONFIG_GUIDE.md).

---

## Protocol facts relevant to the looper

### MIDI 1.0 Note On and Note Off

In the protocol’s MIDI 1.0 channel-voice representation, Note On and Note Off
contain a status/channel field, a note number, and velocity. There is no
application-level `NoteId` in those messages.

MIDI 1.0 Note On with velocity zero has the same function as Note Off. MIDI 2.0
does **not** preserve that rule: a MIDI 2.0 Note On with zero velocity is still
a Note On. The protocol’s MIDI 2.0-to-MIDI 1.0 translation rules explicitly
avoid producing a zero-velocity MIDI 1.0 Note On when translating a MIDI 2.0
Note On.

Project mapping:

- `MidiEvent::isNoteOn()` accepts only MIDI 1.0 Note On with velocity greater
  than zero.
- `MidiEvent::isNoteOff()` accepts MIDI 1.0 Note Off and zero-velocity MIDI 1.0
  Note On.
- `MidiHandler::handleMidiMessage()` routes zero-velocity input through the
  NoteOff handling path.
- `MidiHandler::sendMidiEvent()` sends the stored message type through the
  selected MIDI outputs.

This normalization is correct for the current MIDI 1.0 scope. It must not be
copied unchanged into a future MIDI 2.0 or UMP message decoder.

### Wire identity versus loop identity

MIDI 1.0 identifies a channel-voice note by channel and note number. It does
not identify which internal overlapping note instance a Note Off belongs to.
`NoteId` is therefore a MidiLooper timeline identity, not a wire-level MIDI
identity.

Consequences:

- overlap resolution operates on the source snapshot and capture state;
- the resolver must not depend on an outbound Note Off carrying `NoteId`;
- same-pitch voice allocation and Note Off handling inside a receiving device
  are receiver behavior, not a MIDI 1.0 identity guarantee;
- the complete `LoopSnapshot` still needs correct internal note spans for
  persistence, undo, display, and future resolution.

Current source capture retains the existing same-channel/same-pitch
`pendingNotes` and LIFO behavior. A source-level replace-on-retrigger guard that
would insert an internal synthetic NoteOff before replacing an open capture
entry is intentionally deferred; this snapshot change does not alter live MIDI
capture pairing. Existing record/overdub stop finalization remains responsible
for closing notes that are still open at stop.

MIDI 2.0 adds per-note controller and per-note management messages for
independent per-note control of multiple notes with the same note number. That
is not the same as adding the loop’s internal `NoteId` to ordinary MIDI 1.0
Note On or Note Off messages.

### Same-time event order

The protocol states that simultaneous MIDI Clip events are stored and
transmitted in presentation order. MidiLooper’s project policy for equal-tick
same-pitch note events is Note Off before Note On.

The current code contains this policy in:

- `NoteUtils::midiEventChronologicalLess()`;
- `LoopEventValidation::eventLess()`;
- `LoopContentResolution::resolvedEventLess()`.

This ordering matters when a source note is shortened to the tick where a new
same-pitch note begins. It is separate from the requirement to remove or
replace the source note’s old later Note Off.

### Timing messages

UMP defines optional Jitter Reduction timestamps and MIDI Clip files can use
Delta Clockstamps. These mechanisms do not apply to the current raw MIDI 1.0
byte-stream paths automatically. The current looper’s timing model remains
based on MIDI clock processing, `currentTick`, projected loop coordinates, and
the existing playback service path.

---

## Project logic audit

The protocol review found the following current mappings:

### Covered by current logic

- MIDI 1.0 Note On and Note Off are represented separately in
  `MidiEvent`.
- Zero-velocity MIDI 1.0 Note On is recognized as Note Off.
- Input Note On with zero velocity is routed as Note Off.
- MIDI values are clamped to the MIDI 1.0 note, velocity, and channel ranges.
- Internal `NoteId` is assigned to stored Note On events, not sent as a MIDI
  field.
- Same-tick Note Off-before-Note On ordering is represented in event
  comparison and validation logic.
- MIDI 1.0 output routes Note On and Note Off through the existing output
  transports.

### Not implemented by design

- UMP packet decoding or encoding;
- UMP Group addressing;
- MIDI 2.0 16-bit velocity and expanded controller resolution;
- MIDI 2.0 Note On/Off attributes;
- MIDI 2.0 per-note management;
- Jitter Reduction timestamps;
- Delta Clockstamp scheduling;
- MIDI 2.0 endpoint, Function Block, or stream configuration.

No missing firmware logic was identified for those items because they are
outside the current transport and protocol scope.

### Follow-up consistency check

`LoopContentResolution::resolvedEventLess()` orders equal-tick events using
the raw MIDI message type. `NoteUtils::midiEventChronologicalLess()` orders
using the logical `isNoteOff()` classification. A zero-velocity MIDI 1.0
Note On would therefore be classified consistently as Note Off by the latter
but not necessarily by the former.

Current input handling normalizes zero-velocity input through the NoteOff path,
and stored capture events use explicit Note Off messages. The protocol review
found no current failing path, but a future change that allows raw
zero-velocity Note On events into LCR must either normalize them first or make
the equal-tick ordering policy use the same logical classification everywhere.

---

## Overdub and snapshot rule

The wire protocol does not make playback responsible for resolving overlap
identity. During overdub, the resolution owner works from internal
`LoopSnapshot` content and capture state. The resulting complete snapshot must
represent:

```text
source note shortened to its resolved end
new note starting at the incoming start
new note ending at the incoming end
```

If source and incoming notes share a tick, the published event
representation preserves Note Off before Note On. If the source note previously
had a later wrapped Note Off, that old event must be replaced or removed; an
additional incoming Note Off is not a substitute for correcting the source
snapshot’s geometry.

---

## References

- [MIDI configuration guide](MIDI_CONFIG_GUIDE.md)
- [Loop MIDI storage, validation, and undo](LOOP_MIDI_STORAGE_AND_VALIDATION.md)
- [Overdub overlap-resolve note evaluations](OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md)
- [Overdub ledger note evaluations](OVERDUB_LEDGER_NOTE_EVALUATIONS.md)
- [Runtime architecture — playback consumer](../Authority/Architecture/Playback.md)
- [Runtime architecture — execution bounds](../Authority/Architecture/ExecutionBounds.md)
