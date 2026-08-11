//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef MIDI_BUTTON_GESTURE_POLICY_H
#define MIDI_BUTTON_GESTURE_POLICY_H

// Incomplete USB Host button gestures have unknowable duration. Firmware must not
// invent short/long/double/triple actions from NoteOff-without-NoteOn (or the
// reverse). Lost-NoteOff recovery on a later same-button NoteOn clears stale
// state and accepts the new press without synthesizing the prior release.
namespace MidiButtonGesturePolicy {

constexpr bool kDispatchOrphanNoteOffAsShortPress = false;

inline bool mayDispatchActionFromOrphanNoteOff() {
  return kDispatchOrphanNoteOffAsShortPress;
}

}  // namespace MidiButtonGesturePolicy

#endif  // MIDI_BUTTON_GESTURE_POLICY_H
