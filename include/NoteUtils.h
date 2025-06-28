//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once
#include <vector>
#include <map>
#include <cstdint>
#include "MidiEvent.h"
#include <unordered_map>
#include <utility> // for std::pair

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
 * @class CachedNoteList
 * @brief Caches reconstructed notes to avoid expensive recalculation
 */
class CachedNoteList {
private:
    std::vector<DisplayNote> cachedNotes;
    uint32_t lastMidiHash;
    uint32_t lastLoopLength;
    bool isValid;

public:
    CachedNoteList() : lastMidiHash(0), lastLoopLength(0), isValid(false) {}
    
    const std::vector<DisplayNote>& getNotes(const std::vector<MidiEvent>& midiEvents, uint32_t loopLength);
    void invalidate() { isValid = false; }
    
private:
    uint32_t computeMidiHash(const std::vector<MidiEvent>& midiEvents);
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

/**
 * @brief Fast lookup index for NoteOn/NoteOff events by (pitch<<32)|tick.
 * @param midiEvents The full list of MIDI events.
 * @returns A pair of maps: first is NoteOn index, second is NoteOff index.
 */
using Key = uint64_t;
using EventIndexMap = std::unordered_map<Key, size_t>;
using EventIndex = std::pair<EventIndexMap, EventIndexMap>;
EventIndex buildEventIndex(const std::vector<MidiEvent>& midiEvents);

} // namespace NoteUtils 