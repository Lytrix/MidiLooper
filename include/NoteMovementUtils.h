//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <vector>
#include <cstdint>
#include "MidiEvent.h"
#include "NoteUtils.h"
#include "EditManager.h"
#include "Track.h"

namespace NoteMovementUtils {
    
    /**
     * Move a note with full overlap handling (shortening/deletion/restoration)
     * This is the proven logic from EditStartNoteState::onEncoderTurn
     * 
     * @param track The track containing the MIDI events
     * @param manager The edit manager with moving note state
     * @param currentNote The note being moved (for reference)
     * @param targetTick The new start position for the note
     * @param delta The movement delta (positive = right, negative = left)
     */
    void moveNoteWithOverlapHandling(Track& track, EditManager& manager, 
                                   const NoteUtils::DisplayNote& currentNote, 
                                   uint32_t targetTick, int delta);
    
    /**
     * Helper functions extracted from EditStartNoteState
     */
    uint32_t wrapPosition(int32_t position, uint32_t loopLength);
    uint32_t calculateNoteLength(uint32_t start, uint32_t end, uint32_t loopLength);
    bool notesOverlap(uint32_t start1, uint32_t end1, uint32_t start2, uint32_t end2, uint32_t loopLength);
    
    void findOverlaps(const std::vector<NoteUtils::DisplayNote>& currentNotes,
                     uint8_t movingNotePitch,
                     uint32_t currentStart,
                     uint32_t newStart,
                     uint32_t newEnd,
                     int delta,
                     uint32_t loopLength,
                     std::vector<std::pair<NoteUtils::DisplayNote, uint32_t>>& notesToShorten,
                     std::vector<NoteUtils::DisplayNote>& notesToDelete);
    
    void applyShortenOrDelete(std::vector<MidiEvent>& midiEvents,
                             const std::vector<std::pair<NoteUtils::DisplayNote, uint32_t>>& notesToShorten,
                             const std::vector<NoteUtils::DisplayNote>& notesToDelete,
                             EditManager& manager,
                             uint32_t loopLength,
                             NoteUtils::EventIndexMap& onIndex,
                             NoteUtils::EventIndexMap& offIndex);
    
    void restoreNotes(std::vector<MidiEvent>& midiEvents,
                     const std::vector<EditManager::MovingNoteIdentity::DeletedNote>& notesToRestore,
                     EditManager& manager,
                     uint32_t loopLength,
                     NoteUtils::EventIndexMap& onIndex,
                     NoteUtils::EventIndexMap& offIndex);
    
    void finalReconstructAndSelect(std::vector<MidiEvent>& midiEvents,
                                  EditManager& manager,
                                  uint8_t movingNotePitch,
                                  uint32_t newStart,
                                  uint32_t newEnd,
                                  uint32_t loopLength);
} 