//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "EditPass.h"
#include "MidiEvent.h"
#include "LoopEventStore.h"
#include "LoopPasses.h"

/// Apply one note edit pass row to a flat MIDI list.
void applyNoteEditPass(MidiEventVec& events, const EditPass& editPass, uint32_t loopLengthTicks);

/// Apply ordered note edit pass rows with move/length identity tracking.
void applyNoteEditPassSequence(MidiEventVec& events, const EditPassVec& rows,
                               uint32_t loopLengthTicks);

/// Find note-on index by stable **NoteId**; returns -1 if not found.
int findNoteOnById(const MidiEventVec& events, NoteId noteId);

/// Remove note-on/off pair for **noteId**.
void deleteNoteById(MidiEventVec& events, NoteId noteId);
