//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteMovementUtils.h"
#include "Logger.h"
#include "Globals.h"
#include <algorithm>

namespace NoteMovementUtils {

uint32_t wrapPosition(int32_t position, uint32_t loopLength) {
    if (position < 0) {
        position = (int32_t)loopLength + position;
        while (position < 0) {
            position += (int32_t)loopLength;
        }
    } else if (position >= (int32_t)loopLength) {
        position = position % (int32_t)loopLength;
    }
    return (uint32_t)position;
}

uint32_t calculateNoteLength(uint32_t start, uint32_t end, uint32_t loopLength) {
    if (end >= start) {
        return end - start;
    } else {
        return (loopLength - start) + end;
    }
}

bool notesOverlap(uint32_t start1, uint32_t end1, uint32_t start2, uint32_t end2, uint32_t loopLength) {
    // Convert to unwrapped positions for comparison
    uint32_t unwrappedEnd1 = end1;
    uint32_t unwrappedEnd2 = end2;
    
    // Check if notes are wrapped (end < start means wrapped)
    bool wrapped1 = (end1 < start1);
    bool wrapped2 = (end2 < start2);
    
    if (wrapped1) {
        unwrappedEnd1 = end1 + loopLength;
    }
    if (wrapped2) {
        unwrappedEnd2 = end2 + loopLength;
    }
    
    // Now check overlap using unwrapped positions
    // Note1: [start1, unwrappedEnd1], Note2: [start2, unwrappedEnd2]
    bool overlap = (start1 < unwrappedEnd2) && (start2 < unwrappedEnd1);
    
    // If both notes are unwrapped, also check for loop-wrapped overlaps
    if (!wrapped1 && !wrapped2) {
        // Check if note1 wraps around and overlaps with note2
        bool note1WrapsAndOverlaps = (start1 + loopLength < unwrappedEnd2) && (start2 < end1 + loopLength);
        // Check if note2 wraps around and overlaps with note1
        bool note2WrapsAndOverlaps = (start2 + loopLength < unwrappedEnd1) && (start1 < end2 + loopLength);
        overlap = overlap || note1WrapsAndOverlaps || note2WrapsAndOverlaps;
    }
    
    return overlap;
}

void findOverlaps(const std::vector<NoteUtils::DisplayNote>& currentNotes,
                 uint8_t movingNotePitch,
                 uint32_t currentStart,
                 uint32_t newStart,
                 uint32_t newEnd,
                 int delta,
                 uint32_t loopLength,
                 std::vector<std::pair<NoteUtils::DisplayNote, uint32_t>>& notesToShorten,
                 std::vector<NoteUtils::DisplayNote>& notesToDelete) {
    
    // Calculate display end position for the moving note (handle wrapping)
    uint32_t displayNewEnd = newEnd % loopLength;
    
    for (const auto& note : currentNotes) {
        if (note.note != movingNotePitch) continue;
        if (note.startTick == currentStart) continue; // Skip the moving note itself
        
        bool overlaps = notesOverlap(newStart, displayNewEnd, note.startTick, note.endTick, loopLength);
        if (!overlaps) continue;
        
        // Check if the overlapping note is completely contained within the moving note's new position
        bool noteCompletelyContained = false;
        
        // Handle both wrapped and unwrapped cases
        if (displayNewEnd >= newStart) {
            // Moving note doesn't wrap around
            noteCompletelyContained = (note.startTick >= newStart && note.endTick <= displayNewEnd);
        } else {
            // Moving note wraps around the loop boundary
            noteCompletelyContained = (note.startTick >= newStart || note.endTick <= displayNewEnd);
        }
        
        if (noteCompletelyContained) {
            // Delete the note entirely if it's completely contained within the moving note
            notesToDelete.push_back(note);
            logger.log(CAT_MIDI, LOG_DEBUG, "Will delete completely contained note: pitch=%d, start=%lu, end=%lu (within moving note %lu-%lu)", 
                      note.note, note.startTick, note.endTick, newStart, displayNewEnd);
        } else {
            // Try to shorten the overlapping note
            uint32_t newNoteEnd;
            
            if (note.startTick < newStart) {
                // Note starts before moving note - shorten it to end at moving note's start
                newNoteEnd = newStart;
            } else {
                // Note starts after moving note starts - this shouldn't happen in normal overlap cases
                // but handle it by deleting the note
                notesToDelete.push_back(note);
                logger.log(CAT_MIDI, LOG_DEBUG, "Will delete overlapping note that starts after moving note: pitch=%d, start=%lu, end=%lu", 
                          note.note, note.startTick, note.endTick);
                continue;
            }
            
            uint32_t shortenedLength = calculateNoteLength(note.startTick, newNoteEnd, loopLength);
            
            // Check if shortened length would be less than 49 ticks
            if (shortenedLength < 49) {
                notesToDelete.push_back(note);
                logger.log(CAT_MIDI, LOG_DEBUG, "Will delete note (too short after shortening): pitch=%d, start=%lu, end=%lu->%lu, length=%lu < 49", 
                          note.note, note.startTick, note.endTick, newNoteEnd, shortenedLength);
            } else {
                notesToShorten.push_back({note, newNoteEnd});
                logger.log(CAT_MIDI, LOG_DEBUG, "Will shorten note: pitch=%d, start=%lu, end=%lu->%lu, length=%lu", 
                          note.note, note.startTick, note.endTick, newNoteEnd, shortenedLength);
            }
        }
    }
    logger.log(CAT_MIDI, LOG_DEBUG, "Found %zu notes to shorten and %zu notes to delete", 
              notesToShorten.size(), notesToDelete.size());
}

void applyShortenOrDelete(std::vector<MidiEvent>& midiEvents,
                         const std::vector<std::pair<NoteUtils::DisplayNote, uint32_t>>& notesToShorten,
                         const std::vector<NoteUtils::DisplayNote>& notesToDelete,
                         EditManager& manager,
                         uint32_t loopLength,
                         NoteUtils::EventIndexMap& onIndex,
                         NoteUtils::EventIndexMap& offIndex) {
    // Shorten overlapping notes using index
    for (const auto& [dn, newEnd] : notesToShorten) {
        // Record original for undo
        EditManager::MovingNoteIdentity::DeletedNote original;
        original.note = dn.note;
        original.velocity = dn.velocity;
        original.startTick = dn.startTick;
        original.endTick = dn.endTick;
        original.originalLength = calculateNoteLength(dn.startTick, dn.endTick, loopLength);
        original.wasShortened = true;
        original.shortenedToTick = newEnd;
        
        manager.movingNote.deletedNotes.push_back(original);
        logger.log(CAT_MIDI, LOG_DEBUG, "Stored original note before shortening: pitch=%d, start=%lu, original_end=%lu, shortened_to=%lu, length=%lu",
                  original.note, original.startTick, original.endTick, original.shortenedToTick, original.originalLength);
        
        // Adjust its NoteOff event via index
        auto offKey = (NoteUtils::Key(dn.note) << 32) | dn.endTick;
        auto itOff = offIndex.find(offKey);
        if (itOff != offIndex.end()) {
            size_t idx = itOff->second;
            logger.log(CAT_MIDI, LOG_DEBUG, "Updated note-off event: pitch=%d, from tick=%lu to tick=%lu", 
                      dn.note, midiEvents[idx].tick, newEnd);
            midiEvents[idx].tick = newEnd;
            // Update index for new tick
            offIndex.erase(itOff);
            auto newKey = (NoteUtils::Key(dn.note) << 32) | newEnd;
            offIndex[newKey] = idx;
        }
    }
    
    // Delete overlapping notes entirely
    for (const auto& dn : notesToDelete) {
        int deletedCount = 0;
        auto it = midiEvents.begin();
        while (it != midiEvents.end()) {
            bool matchOn  = (it->type == midi::NoteOn && it->data.noteData.velocity > 0 && 
                           it->data.noteData.note == dn.note && it->tick == dn.startTick);
            bool matchOff = ((it->type == midi::NoteOff || (it->type == midi::NoteOn && it->data.noteData.velocity == 0)) && 
                           it->data.noteData.note == dn.note && it->tick == dn.endTick);
            
            if (matchOn || matchOff) {
                logger.log(CAT_MIDI, LOG_DEBUG, "Temporarily deleting MIDI event: type=%s, pitch=%d, tick=%lu",
                          (matchOn ? "NoteOn" : "NoteOff"), dn.note, (matchOn ? dn.startTick : dn.endTick));
                it = midiEvents.erase(it);
                deletedCount++;
            } else {
                ++it;
            }
        }
        
        if (deletedCount != 2) {
            logger.log(CAT_MIDI, LOG_DEBUG, "Warning: deleted %d events for pitch %d (expected 2)", deletedCount, dn.note);
        }
        
        // Save deleted note for undo
        EditManager::MovingNoteIdentity::DeletedNote deleted;
        deleted.note = dn.note;
        deleted.velocity = dn.velocity;
        deleted.startTick = dn.startTick;
        deleted.endTick = dn.endTick;
        deleted.originalLength = calculateNoteLength(dn.startTick, dn.endTick, loopLength);
        deleted.wasShortened = false;
        deleted.shortenedToTick = 0;
        
        manager.movingNote.deletedNotes.push_back(deleted);
        logger.log(CAT_MIDI, LOG_DEBUG, "Stored deleted note: pitch=%d, start=%lu, end=%lu, length=%lu",
                  deleted.note, deleted.startTick, deleted.endTick, deleted.originalLength);
    }
}

void restoreNotes(std::vector<MidiEvent>& midiEvents,
                 const std::vector<EditManager::MovingNoteIdentity::DeletedNote>& notesToRestore,
                 EditManager& manager,
                 uint32_t loopLength,
                 NoteUtils::EventIndexMap& onIndex,
                 NoteUtils::EventIndexMap& offIndex) {
    
    logger.log(CAT_MIDI, LOG_DEBUG, "=== RESTORING TEMPORARY NOTES ===");
    logger.log(CAT_MIDI, LOG_DEBUG, "Total notes to restore: %zu", notesToRestore.size());
    
    std::vector<EditManager::MovingNoteIdentity::DeletedNote> restored;
    
    for (const auto& nr : notesToRestore) {
        bool didRestore = false;
        
        if (nr.wasShortened) {
            // Restore a shortened note by extending it back to original length
            logger.log(CAT_MIDI, LOG_DEBUG, "Restoring shortened note: pitch=%d, start=%lu, was shortened to %lu, restoring to %lu", 
                      nr.note, nr.startTick, nr.shortenedToTick, nr.endTick);
            
            // Find the note-on event
            auto onKey = (NoteUtils::Key(nr.note) << 32) | nr.startTick;
            auto itOn = onIndex.find(onKey);
            if (itOn != onIndex.end()) {
                // Find the corresponding note-off event at the shortened position
                auto offKey = (NoteUtils::Key(nr.note) << 32) | nr.shortenedToTick;
                auto itOff = offIndex.find(offKey);
                if (itOff != offIndex.end()) {
                    size_t idx = itOff->second;
                    logger.log(CAT_MIDI, LOG_DEBUG, "Extending note-off event: pitch=%d, from tick=%lu to tick=%lu", 
                              nr.note, midiEvents[idx].tick, nr.endTick);
                    midiEvents[idx].tick = nr.endTick;
                    
                    // Update index for new tick
                    offIndex.erase(itOff);
                    auto newKey = (NoteUtils::Key(nr.note) << 32) | nr.endTick;
                    offIndex[newKey] = idx;
                    didRestore = true;
                } else {
                    logger.log(CAT_MIDI, LOG_DEBUG, "Failed to find note-off for shortened note: pitch=%d, start=%lu", 
                              nr.note, nr.startTick);
                }
            } else {
                logger.log(CAT_MIDI, LOG_DEBUG, "Failed to find note-on for shortened note: pitch=%d, start=%lu", 
                          nr.note, nr.startTick);
            }
        } else {
            // Recreate a completely deleted note
            logger.log(CAT_MIDI, LOG_DEBUG, "Restoring deleted note: pitch=%d, start=%lu, end=%lu", 
                      nr.note, nr.startTick, nr.endTick);
            
            MidiEvent onEvt;
            onEvt.tick = nr.startTick;
            onEvt.type = midi::NoteOn;
            onEvt.data.noteData.note = nr.note;
            onEvt.data.noteData.velocity = nr.velocity;
            midiEvents.push_back(onEvt);
            
            MidiEvent offEvt;
            offEvt.tick = nr.endTick;
            offEvt.type = midi::NoteOff;
            offEvt.data.noteData.note = nr.note;
            offEvt.data.noteData.velocity = 0;
            midiEvents.push_back(offEvt);
            
            didRestore = true;
        }
        
        if (didRestore) {
            restored.push_back(nr);
        }
    }
    
    // Remove restored notes from deleted list
    for (const auto& r : restored) {
        manager.movingNote.deletedNotes.erase(
            std::remove_if(manager.movingNote.deletedNotes.begin(), manager.movingNote.deletedNotes.end(),
                [&](const auto& dn){ return dn.note==r.note && dn.startTick==r.startTick && dn.endTick==r.endTick; }),
            manager.movingNote.deletedNotes.end());
    }
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Restored %zu notes, %zu notes still in deleted list", 
              restored.size(), manager.movingNote.deletedNotes.size());
}

void finalReconstructAndSelect(std::vector<MidiEvent>& midiEvents,
                              EditManager& manager,
                              uint8_t movingNotePitch,
                              uint32_t newStart,
                              uint32_t newEnd,
                              uint32_t loopLength) {
    // Sort events by tick
    std::sort(midiEvents.begin(), midiEvents.end(),
              [](const MidiEvent &a, const MidiEvent &b){ return a.tick < b.tick; });
    
    // Reconstruct final notes and select moved note FIRST
    auto finalNotes = NoteUtils::reconstructNotes(midiEvents, loopLength);
    int newSelectedIdx = -1;
    
    // Find the moved note in the reconstructed list
    for (int i = 0; i < (int)finalNotes.size(); ++i) {
        if (finalNotes[i].note == movingNotePitch &&
            finalNotes[i].startTick == newStart &&
            finalNotes[i].endTick == newEnd) {
            newSelectedIdx = i;
            break;
        }
    }
    
    if (newSelectedIdx >= 0) {
        int oldSelectedIdx = manager.getSelectedNoteIdx();
        manager.setSelectedNoteIdx(newSelectedIdx);
        logger.log(CAT_MIDI, LOG_DEBUG, "Updated selectedNoteIdx: %d -> %d (note at new position)", 
                  oldSelectedIdx, newSelectedIdx);
        
        // ONLY update bracket tick AFTER we've secured the selection
        // This prevents the selection system from picking up other notes at this position
        manager.setBracketTick(newStart);
    } else {
        logger.log(CAT_MIDI, LOG_DEBUG, "Warning: Could not find moved note in reconstructed list");
        // Still update bracket tick even if we couldn't find the note
        manager.setBracketTick(newStart);
    }
}

void moveNoteWithOverlapHandling(Track& track, EditManager& manager, 
                                const NoteUtils::DisplayNote& currentNote, 
                                uint32_t targetTick, int delta) {
    auto& midiEvents = track.getMidiEvents();
    uint32_t loopLength = track.getLoopLength();
    
    logger.log(CAT_MIDI, LOG_DEBUG, "NoteMovementUtils::moveNoteWithOverlapHandling called: targetTick=%lu, delta=%d", targetTick, delta);
    
    if (loopLength == 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop length is 0, cannot move notes");
        return;
    }
    
    // Update movement direction
    if (delta > 0) {
        manager.movingNote.movementDirection = 1; // Moving right (positive delta)
    } else if (delta < 0) {
        manager.movingNote.movementDirection = -1; // Moving left (negative delta)
    }
    
    // Use the moving note identity instead of reconstructing from selected index
    uint8_t movingNotePitch = manager.movingNote.note;
    uint32_t currentStart = manager.movingNote.lastStart;
    uint32_t currentEnd = manager.movingNote.lastEnd;
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Moving note: pitch=%d, start=%lu, end=%lu", 
              movingNotePitch, currentStart, currentEnd);
    
    // Calculate note length and new positions with wrap-around
    uint32_t displayCurrentEnd = (currentEnd >= loopLength) ? (currentEnd % loopLength) : currentEnd;
    uint32_t noteLen = calculateNoteLength(currentStart, displayCurrentEnd, loopLength);
    uint32_t newStart = targetTick;
    uint32_t newEnd = newStart + noteLen;
    uint32_t displayNewEnd = newEnd % loopLength;
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Movement: start %lu->%lu, end actual %lu (display %lu), length=%lu", 
              currentStart, newStart, newEnd, displayNewEnd, noteLen);
    
    // CRITICAL: Reconstruct notes for overlap detection BEFORE moving the note
    // This ensures the moving note is still at its original position during overlap detection
    std::vector<NoteUtils::DisplayNote> currentNotes = NoteUtils::reconstructNotes(midiEvents, loopLength);
    
    // Store notes to delete and restore
    std::vector<NoteUtils::DisplayNote> notesToDelete;
    std::vector<EditManager::MovingNoteIdentity::DeletedNote> notesToRestore;
    
    // Build event index once and reuse
    auto [onIndex, offIndex] = NoteUtils::buildEventIndex(midiEvents);
    
    // STEP 1: Detect and categorize overlaps BEFORE moving the note
    std::vector<std::pair<NoteUtils::DisplayNote, uint32_t>> notesToShorten;
    findOverlaps(currentNotes, movingNotePitch, currentStart, newStart, newEnd, delta, loopLength,
                notesToShorten, notesToDelete);
    
    // STEP 2: Move the selected note's NoteOn/NoteOff events to new position
    auto onIt = std::find_if(midiEvents.begin(), midiEvents.end(), [&](MidiEvent& evt) {
        return evt.type == midi::NoteOn &&
               evt.data.noteData.note == movingNotePitch &&
               evt.tick == currentStart;
    });
    auto offIt = std::find_if(midiEvents.begin(), midiEvents.end(), [&](MidiEvent& evt) {
        bool isOff = (evt.type == midi::NoteOff) || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0);
        return isOff && evt.data.noteData.note == movingNotePitch &&
               evt.tick == currentEnd;
    });
    
    if (onIt != midiEvents.end() && offIt != midiEvents.end()) {
        onIt->tick = newStart;
        offIt->tick = newEnd;
        manager.movingNote.lastStart = newStart;
        manager.movingNote.lastEnd = newEnd;
        logger.log(CAT_MIDI, LOG_DEBUG, "Moved note events: pitch=%u start->%lu end->%lu", movingNotePitch, newStart, newEnd);
    } else {
        logger.log(CAT_MIDI, LOG_DEBUG, "Warning: could not find MIDI events for moving note pitch=%u", movingNotePitch);
    }
    
    // STEP 3: Check if we should restore any previously deleted/shortened notes
    for (const auto& deletedNote : manager.movingNote.deletedNotes) {
        // Only consider restoring notes of the same pitch as the note being moved
        if (deletedNote.note == movingNotePitch) {
            // Check if the deleted note overlaps with the new position
            bool hasOverlap = notesOverlap(newStart, newEnd,
                                         deletedNote.startTick, deletedNote.endTick, loopLength);
            
            // Additional check: only restore if we're moving away from the deleted note
            bool movingAway = false;
            if (manager.movingNote.movementDirection > 0) {
                // Moving right (positive delta) - restore notes to the left
                movingAway = (deletedNote.endTick <= currentStart);
            } else if (manager.movingNote.movementDirection < 0) {
                // Moving left (negative delta) - restore notes to the right  
                movingAway = (deletedNote.startTick >= currentStart + noteLen);
            }
            
            if (!hasOverlap && movingAway) {
                notesToRestore.push_back(deletedNote);
                logger.log(CAT_MIDI, LOG_DEBUG, "Will restore note: pitch=%d, start=%lu, end=%lu (no overlap with %lu-%lu, moving away)", 
                          deletedNote.note, deletedNote.startTick, deletedNote.endTick, newStart, newEnd);
            } else {
                logger.log(CAT_MIDI, LOG_DEBUG, "Cannot restore note: pitch=%d, start=%lu, end=%lu (overlap=%s, movingAway=%s)", 
                          deletedNote.note, deletedNote.startTick, deletedNote.endTick, 
                          hasOverlap ? "yes" : "no", movingAway ? "yes" : "no");
            }
        }
    }
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Found %zu notes to restore, %zu total deleted notes", 
              notesToRestore.size(), manager.movingNote.deletedNotes.size());
    
    // Apply shorten/delete using shared index
    applyShortenOrDelete(midiEvents, notesToShorten, notesToDelete, manager, loopLength, onIndex, offIndex);
    
    // Restore notes that should be restored based on movement, reusing index
    restoreNotes(midiEvents, notesToRestore, manager, loopLength, onIndex, offIndex);
    
    // Helper to finalize reconstruction and selection after movement
    finalReconstructAndSelect(midiEvents, manager, movingNotePitch, newStart, newEnd, loopLength);
}

} // namespace NoteMovementUtils 