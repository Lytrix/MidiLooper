//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once
#include "MidiEvent.h"

/**
 * @namespace MidiEventUtils
 * @brief Common MIDI event operations to reduce code duplication
 */
namespace MidiEventUtils {

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
