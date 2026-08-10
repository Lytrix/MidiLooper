//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#include "EditManager.h"
#include "MidiEvent.h"
#include "NoteEditFocus.h"
#include "Utils/NoteEditMem.h"

NOTE_EDIT_MEM uint32_t noteEditGeometryApplyStorageTickToDisplayPhase(uint32_t tick,
                                                                      uint32_t loopLength);

NOTE_EDIT_MEM uint32_t noteEditGeometryApplyBracketDisplayTickFromStorage(uint32_t storageTick,
                                                                          uint32_t loopStartTick,
                                                                          uint32_t loopLength);

NOTE_EDIT_MEM void noteEditGeometryApplyStampPairedNoteId(MidiEvent* noteOn, MidiEvent* noteOff);

NOTE_EDIT_MEM NoteEditFocus& noteEditGeometryApplyEditFocus(EditManager& manager);

NOTE_EDIT_MEM MidiEvent* noteEditGeometryApplyFindNoteOnAtStart(MidiEventVec& midiEvents,
                                                                uint8_t channel, uint8_t pitch,
                                                                uint32_t startTick);

NOTE_EDIT_MEM MidiEvent* noteEditGeometryApplyResolveMovingNoteOffForEdit(
    MidiEventVec& midiEvents, MidiEvent* noteOnEvent, NoteId movingNoteId, uint8_t channel,
    uint8_t pitch, uint32_t startTick, uint32_t displayEndTick, uint32_t loopLength);

NOTE_EDIT_MEM uint32_t noteEditGeometryApplyDisplayFocusEndTickForMove(uint32_t startTick,
                                                                       uint32_t noteLen,
                                                                       uint32_t loopLength);

NOTE_EDIT_MEM void noteEditGeometryApplyScrubStaleWrapHeadOffsForMovedNote(
    MidiEventVec& midiEvents, uint8_t channel, uint8_t pitch, uint32_t tailOnTick,
    uint32_t linearOffTick, uint32_t loopLength, NoteId movingNoteId);

NOTE_EDIT_MEM uint32_t noteEditGeometryApplyResolveMovingNoteLengthTicks(
    MidiEventVec& midiEvents, uint8_t channel, uint8_t pitch, uint32_t startTick,
    uint32_t fallbackEndTick, uint32_t loopLength, NoteId movingNoteId);
