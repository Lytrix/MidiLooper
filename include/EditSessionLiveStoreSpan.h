//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#include "MidiEvent.h"
#include "NoteEditFocus.h"

bool readLiveLinearSpan(const MidiEventVec& liveStore, NoteId noteId, uint8_t channel,
                        NoteBaseline& out);

/// Committed materialize span at exact pitch+start (noteId may differ from session store).
bool findCommittedLinearSpanForPitchStart(MidiEventVec& committedEvents, uint8_t channel,
                                            uint8_t pitch, uint32_t startTick,
                                            uint32_t loopLength, NoteBaseline& out);

bool liveStoreHasNotePair(const MidiEventVec& liveStore, NoteId noteId, uint8_t channel);

/// Session-store noteId at exact pitch+start (for enrich when committed note-ons lack noteId).
NoteId findLiveNoteIdForPitchStart(const MidiEventVec& liveStore, uint8_t channel, uint8_t pitch,
                                   uint32_t startTick);

/// Read span from live store at pitch+start; sets outNoteId from the matched note-on.
bool readLiveLinearSpanForPitchStart(const MidiEventVec& liveStore, uint8_t channel,
                                     uint8_t pitch, uint32_t startTick, NoteId& outNoteId,
                                     NoteBaseline& out);

void sortNoteIdVector(std::vector<NoteId, InternalHeapFirstAllocator<NoteId>>& ids);
