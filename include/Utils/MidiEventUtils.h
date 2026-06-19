//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once
#include "MidiEvent.h"
#include <vector>
#include <cstdint>

/**
 * @namespace MidiEventUtils
 * @brief Common MIDI event operations to reduce code duplication
 */
namespace MidiEventUtils {

/**
 * @brief Finds a NoteOn event matching the specified criteria
 * @param midiEvents The MIDI events to search
 * @param pitch The note pitch to match
 * @param tick The tick position to match
 * @return Iterator to the found event, or midiEvents.end() if not found
 */
MidiEventVec::iterator findNoteOnEvent(
    MidiEventVec& midiEvents,
    uint8_t pitch,
    uint32_t tick);

/**
 * @brief Finds a NoteOff event matching the specified criteria
 * @param midiEvents The MIDI events to search
 * @param pitch The note pitch to match  
 * @param tick The tick position to match
 * @return Iterator to the found event, or midiEvents.end() if not found
 */
MidiEventVec::iterator findNoteOffEvent(
    MidiEventVec& midiEvents,
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
std::pair<MidiEventVec::iterator, MidiEventVec::iterator> 
findNoteEventPair(
    MidiEventVec& midiEvents,
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
