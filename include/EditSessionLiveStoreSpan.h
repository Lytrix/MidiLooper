//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#include "MidiEvent.h"
#include "NoteEditFocus.h"

bool readLiveLinearSpan(const MidiEventVec& liveStore, NoteId noteId, uint8_t channel,
                        NoteBaseline& out);

bool liveStoreHasNotePair(const MidiEventVec& liveStore, NoteId noteId, uint8_t channel);
