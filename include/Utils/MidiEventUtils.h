//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once
#include "MidiEvent.h"
#include "EditManager.h"
#include <vector>
#include <cstdint>

/**
 * @namespace MidiEventUtils
 * @brief Common MIDI event operations to reduce code duplication
 */
namespace MidiEventUtils {

/**
 * @brief Creates a DeletedNote structure from a DisplayNote
 * @param note The display note to convert
 * @param loopLength The current loop length
 * @param wasShortened Whether this note was shortened (vs completely deleted)
 * @param shortenedToTick If shortened, the tick it was shortened to (0 if deleted)
 * @return Populated DeletedNote structure
 */
EditManager::MovingNoteIdentity::DeletedNote createDeletedNote(
    const NoteUtils::DisplayNote& note, 
    uint32_t loopLength,
    bool wasShortened = false, 
    uint32_t shortenedToTick = 0);

/**
 * @brief Finds a NoteOn event matching the specified criteria
 * @param midiEvents The MIDI events to search
 * @param pitch The note pitch to match
 * @param tick The tick position to match
 * @return Iterator to the found event, or midiEvents.end() if not found
 */
std::vector<MidiEvent>::iterator findNoteOnEvent(
    std::vector<MidiEvent>& midiEvents,
    uint8_t pitch,
    uint32_t tick);

/**
 * @brief Finds a NoteOff event matching the specified criteria
 * @param midiEvents The MIDI events to search
 * @param pitch The note pitch to match  
 * @param tick The tick position to match
 * @return Iterator to the found event, or midiEvents.end() if not found
 */
std::vector<MidiEvent>::iterator findNoteOffEvent(
    std::vector<MidiEvent>& midiEvents,
    uint8_t pitch,
    uint32_t tick);

/**
 * @brief Finds both NoteOn and NoteOff events for a note
 * @param midiEvents The MIDI events to search
 * @param pitch The note pitch to match
 * @param startTick The start tick position
 * @param endTick The end tick position  
 * @return Pair of iterators (NoteOn, NoteOff), either may be end() if not found
 */
std::pair<std::vector<MidiEvent>::iterator, std::vector<MidiEvent>::iterator> 
findNoteEventPair(
    std::vector<MidiEvent>& midiEvents,
    uint8_t pitch,
    uint32_t startTick,
    uint32_t endTick);

/**
 * @brief Creates a MidiEvent for a note
 * @param isNoteOn Whether this is a NoteOn (true) or NoteOff (false) event
 * @param pitch The MIDI note pitch
 * @param velocity The note velocity (ignored for NoteOff)
 * @param tick The tick position
 * @return The created MidiEvent
 */
MidiEvent createNoteEvent(bool isNoteOn, uint8_t pitch, uint8_t velocity, uint32_t tick);

} // namespace MidiEventUtils 