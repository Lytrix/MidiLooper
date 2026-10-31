//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef MIDI_BUTTON_GESTURE_POLICY_H
#define MIDI_BUTTON_GESTURE_POLICY_H

#include <cstdint>

// Incomplete USB Host button gestures have unknowable duration. Firmware must not
// invent short/long/double/triple actions from NoteOff-without-NoteOn (or the
// reverse). Lost-NoteOff recovery on a later same-button NoteOn clears stale
// state and accepts the new press without synthesizing the prior release.
namespace MidiButtonGesturePolicy {

constexpr bool kDispatchOrphanNoteOffAsShortPress = false;

inline bool mayDispatchActionFromOrphanNoteOff() {
  return kDispatchOrphanNoteOffAsShortPress;
}

/// What a slot CLEAR gesture resolves to. "Already empty" (no RAM rows and no persisted payload) is a
/// successful outcome, not a no-op: the HITL verified-clear path retries a CLEAR until the device names
/// an outcome, and it accepts the empty slot only from the named row below. A silent return here made a
/// successful clear of an already-empty slot look like a refusal (`midifile-chain-43`, 2026-10-30).
enum class ClearSlotOutcome : uint8_t {
  ClearRam = 0,
  RemovePersistedPayload = 1,
  AlreadyEmpty = 2,
};

inline ClearSlotOutcome classifyClearSlotOutcome(bool hasRamData, bool hasPersistedPayload) {
  if (hasRamData) {
    return ClearSlotOutcome::ClearRam;
  }
  if (hasPersistedPayload) {
    return ClearSlotOutcome::RemovePersistedPayload;
  }
  return ClearSlotOutcome::AlreadyEmpty;
}

/// The exact row `scripts/hitl_midifile_chain.py::_classify_clear` classifies as `empty`. Keep the text
/// and the classifier in step; the button action and the test both use this constant.
constexpr const char* kClearIgnoredEmptySlotLogFormat =
    "Clear ignored — selected slot is empty (track %u slot %u)";

}  // namespace MidiButtonGesturePolicy

#endif  // MIDI_BUTTON_GESTURE_POLICY_H
