#pragma once
#include <vector>
#include <map>
#include <cstdint>
#include "MidiEvent.h"

namespace NoteUtils {
/****
 * @struct DisplayNote
 * @brief Simplified note representation for UI and overlap logic.
 *
 * Contains pitch, velocity, and start/end ticks for a MIDI note event.
 ****/
 struct DisplayNote {
     uint8_t note;
     uint8_t velocity;
     uint32_t startTick;
     uint32_t endTick;
 };

/**
 * @brief Reconstructs a list of DisplayNote from raw MIDI events using LIFO pairing.
 *
 * Matches NoteOn/NoteOff (or NoteOn with zero velocity) events per pitch in LIFO order,
 * wrapping any notes still active at loop end. Does not split wrapped notes into two segments.
 *
 * @param midiEvents  The full list of MIDI events from a Track.
 * @param loopLength  The loop length in ticks.
 * @return Vector of paired DisplayNote entries.
 */
std::vector<DisplayNote> reconstructNotes(const std::vector<MidiEvent>& midiEvents, uint32_t loopLength);

} // namespace NoteUtils 