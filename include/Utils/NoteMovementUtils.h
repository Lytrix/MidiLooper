//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <vector>
#include <cstdint>
#include "Utils/NoteMovementWrap.h"
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
     * Apply a pitch change using the same overlap/delete/restore ledger as movement.
     *
     * @param currentNoteValue Existing pitch of the moving note
     * @param newNoteValue Target pitch for the moving note
     * @param noteStart In/out moving note start tick (may expand through adjacent merge)
     * @param noteEnd In/out moving note end tick (may expand through adjacent merge)
     * @return true when both note-on and note-off were updated to the new pitch
     */
    bool applyPitchChange(Track& track, EditManager& manager,
                          uint8_t currentNoteValue, uint8_t newNoteValue,
                          uint32_t& noteStart, uint32_t& noteEnd);
    
    /**
     * Helper functions extracted from EditStartNoteState
     */
    bool notesOverlap(uint32_t start1, uint32_t end1, uint32_t start2, uint32_t end2, uint32_t loopLength);
    /** True when notes overlap or share a boundary tick (same-pitch lane merge). */
    bool notesTouchOrOverlap(uint32_t start1, uint32_t end1, uint32_t start2, uint32_t end2,
                             uint32_t loopLength);

    /** True when [noteStart, noteEnd] lies within the moving note's original span. */
    bool isNoteWithinMovingSpan(uint32_t noteStart, uint32_t noteEnd,
                                uint32_t spanStart, uint32_t spanEnd,
                                uint32_t loopLength);
    
    void findOverlaps(const std::vector<NoteUtils::DisplayNote>& currentNotes,
                     uint8_t movingNotePitch,
                     uint32_t currentStart,
                     uint32_t newStart,
                     uint32_t newEnd,
                     int delta,
                     uint32_t loopLength,
                     const EditManager& manager,
                     std::vector<std::pair<NoteUtils::DisplayNote, uint32_t>>& notesToShorten,
                     std::vector<NoteUtils::DisplayNote>& notesToDelete);
    
    void applyShortenOrDelete(MidiEventVec& midiEvents,
                             const std::vector<std::pair<NoteUtils::DisplayNote, uint32_t>>& notesToShorten,
                             const std::vector<NoteUtils::DisplayNote>& notesToDelete,
                             EditManager& manager,
                             uint32_t loopLength,
                             NoteUtils::EventIndexMap& onIndex,
                             NoteUtils::EventIndexMap& offIndex);
    
    void restoreNotes(MidiEventVec& midiEvents,
                     const std::vector<EditManager::MovingNoteIdentity::DeletedNote>& notesToRestore,
                     EditManager& manager,
                     uint32_t loopLength,
                     uint8_t channel,
                     NoteUtils::EventIndexMap& onIndex,
                     NoteUtils::EventIndexMap& offIndex);
    
    void finalReconstructAndSelect(MidiEventVec& midiEvents,
                                  EditManager& manager,
                                  uint8_t movingNotePitch,
                                  uint32_t newStart,
                                  uint32_t newEnd,
                                  uint32_t loopLength);
    
    // Find the corresponding note-off event for a given note-on event using LIFO pairing logic
    MidiEvent* findCorrespondingNoteOff(MidiEventVec& midiEvents, MidiEvent* noteOnEvent, uint8_t pitch, std::uint32_t startTick, std::uint32_t endTick);
    
    // Extend shortened notes dynamically
    void extendShortenedNotes(MidiEventVec& midiEvents,
                             const std::vector<std::pair<EditManager::MovingNoteIdentity::DeletedNote, std::uint32_t>>& notesToExtend,
                             EditManager& manager,
                             std::uint32_t loopLength);
} 