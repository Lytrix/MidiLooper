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
                 uint32_t currentEnd,
                 uint32_t newStart,
                 uint32_t newEnd,
                 int delta,
                 uint32_t loopLength,
                 std::vector<std::pair<NoteUtils::DisplayNote, uint32_t>>& notesToShorten,
                 std::vector<NoteUtils::DisplayNote>& notesToDelete) {
    
    // Calculate display end position for the moving note (handle wrapping)
    uint32_t displayNewEnd = newEnd % loopLength;
    
    for (const auto& note : currentNotes) {
        // Note: currentNotes is already filtered to only contain notes of the same pitch
        // that are NOT the moving note, so we can safely process all notes in this list
        
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
                // Note starts before moving note - shorten it to end 1 tick before moving note starts
                // This prevents any ambiguity during note selection
                if (newStart == 0) {
                    // Handle wrap-around case - if moving note starts at 0, shortened note ends at loop end - 1
                    newNoteEnd = loopLength - 1;
                } else {
                    newNoteEnd = newStart - 1;
                }
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
        // Find the current note-off event tick (might have been shortened before)
        uint32_t currentOffTick = dn.endTick;
        
        // Check if we already have a deleted note entry for this note
        auto existingEntry = std::find_if(manager.movingNote.deletedNotes.begin(), 
                                         manager.movingNote.deletedNotes.end(),
                                         [&](const auto& deletedNote) {
                                             return deletedNote.note == dn.note && 
                                                    deletedNote.startTick == dn.startTick &&
                                                    deletedNote.wasShortened;
                                         });
        
        if (existingEntry != manager.movingNote.deletedNotes.end()) {
            // Update the existing entry with the new shortened position
            currentOffTick = existingEntry->shortenedToTick;  // Use current position before updating
            logger.log(CAT_MIDI, LOG_DEBUG, "Updating existing shortened note entry: pitch=%d, start=%lu, old_shortened_to=%lu, new_shortened_to=%lu",
                      dn.note, dn.startTick, existingEntry->shortenedToTick, newEnd);
            logger.log(CAT_MIDI, LOG_DEBUG, "Note was already shortened, looking for note-off at current position: %lu instead of original %lu", 
                      currentOffTick, dn.endTick);
            existingEntry->shortenedToTick = newEnd;
        } else {
            // Record original for undo - currentOffTick remains as dn.endTick (original position)
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
        }
        
        // Adjust its NoteOff event via index
        auto offKey = (NoteUtils::Key(dn.note) << 32) | currentOffTick;
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
        } else {
            logger.log(CAT_MIDI, LOG_DEBUG, "Warning: Could not find note-off event for shortening: pitch=%d, expected at tick=%lu", 
                      dn.note, currentOffTick);
        }
    }
    
    // Delete overlapping notes entirely  
    for (const auto& dn : notesToDelete) {
        // Use a two-phase approach: first find the specific NoteOn/NoteOff pair,
        // then delete them to avoid deleting the wrong events when multiple notes
        // share the same start position
        
        MidiEvent* noteOnToDelete = nullptr;
        MidiEvent* noteOffToDelete = nullptr;
        
        // First pass: find the specific NoteOn at the start position
        for (auto& evt : midiEvents) {
            if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 && 
                evt.data.noteData.note == dn.note && evt.tick == dn.startTick) {
                
                // Look ahead to find the corresponding NoteOff for this specific NoteOn
                for (auto& offEvt : midiEvents) {
                    bool isOff = (offEvt.type == midi::NoteOff) || (offEvt.type == midi::NoteOn && offEvt.data.noteData.velocity == 0);
                    if (isOff && offEvt.data.noteData.note == dn.note && offEvt.tick == dn.endTick) {
                        // Check that this pair forms the note we want to delete
                        uint32_t noteLength = calculateNoteLength(evt.tick, offEvt.tick, loopLength);
                        uint32_t expectedLength = calculateNoteLength(dn.startTick, dn.endTick, loopLength);
                        
                        if (noteLength == expectedLength) {
                            noteOnToDelete = &evt;
                            noteOffToDelete = &offEvt;
                            goto found_pair; // Break out of both loops
                        }
                    }
                }
            }
        }
        
        found_pair:
        if (noteOnToDelete && noteOffToDelete) {
            logger.log(CAT_MIDI, LOG_DEBUG, "Temporarily deleting MIDI event pair: NoteOn pitch=%d tick=%lu, NoteOff pitch=%d tick=%lu",
                      noteOnToDelete->data.noteData.note, noteOnToDelete->tick,
                      noteOffToDelete->data.noteData.note, noteOffToDelete->tick);
            
            // Remove the specific events (remove the later one first to preserve indices)
            auto it1 = std::find_if(midiEvents.begin(), midiEvents.end(), [noteOnToDelete](const MidiEvent& e) { return &e == noteOnToDelete; });
            auto it2 = std::find_if(midiEvents.begin(), midiEvents.end(), [noteOffToDelete](const MidiEvent& e) { return &e == noteOffToDelete; });
            
            if (it1 != midiEvents.end() && it2 != midiEvents.end()) {
                // Remove the later iterator first to preserve indices
                if (it2 > it1) {
                    midiEvents.erase(it2);
                    midiEvents.erase(it1);
                } else {
                    midiEvents.erase(it1);
                    midiEvents.erase(it2);
                }
            }
        } else {
            logger.log(CAT_MIDI, LOG_DEBUG, "Warning: could not find specific MIDI event pair for note pitch=%d, start=%lu, end=%lu", 
                      dn.note, dn.startTick, dn.endTick);
        }
        
        // Check if we already have a deleted note entry for this note
        auto existingDeleted = std::find_if(manager.movingNote.deletedNotes.begin(), 
                                           manager.movingNote.deletedNotes.end(),
                                           [&](const auto& deletedNote) {
                                               return deletedNote.note == dn.note && 
                                                      deletedNote.startTick == dn.startTick &&
                                                      deletedNote.endTick == dn.endTick &&
                                                      !deletedNote.wasShortened;
                                           });
        
        if (existingDeleted == manager.movingNote.deletedNotes.end()) {
            // Save deleted note for undo (only if not already stored)
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
        } else {
            logger.log(CAT_MIDI, LOG_DEBUG, "Skipping duplicate deleted note entry: pitch=%d, start=%lu, end=%lu",
                      dn.note, dn.startTick, dn.endTick);
        }
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
    
    // If there's no actual movement, just update the bracket position and return
    if (delta == 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "No movement (delta=0), just updating bracket position to %lu", targetTick);
        manager.setBracketTick(targetTick);
        return;
    }
    
    // Update movement direction
    if (delta > 0) {
        manager.movingNote.movementDirection = 1; // Moving right (positive delta)
    } else if (delta < 0) {
        manager.movingNote.movementDirection = -1; // Moving left (negative delta)
    }
    
    // Use the moving note identity - we'll check for pitch changes AFTER finding the current events
    uint8_t movingNotePitch = manager.movingNote.note;
    uint32_t currentStart = manager.movingNote.lastStart;
    uint32_t currentEnd = manager.movingNote.lastEnd;
    
    // Store original position for phantom note detection (before any updates)
    uint32_t originalStart = currentStart;
    
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
    
    // STEP 1: Create a filtered list of notes that excludes the moving note
    // This prevents any confusion about which note is the moving note
    std::vector<NoteUtils::DisplayNote> allNotes = NoteUtils::reconstructNotes(midiEvents, loopLength);
    std::vector<NoteUtils::DisplayNote> otherNotesOfSamePitch;
    for (const auto& note : allNotes) {
        // Only include notes of the same pitch that are NOT the moving note
        if (note.note == movingNotePitch && 
            !(note.startTick == currentStart && note.endTick == displayCurrentEnd)) {
            otherNotesOfSamePitch.push_back(note);
        }
    }
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Found %zu other notes of same pitch (excluding moving note)", otherNotesOfSamePitch.size());
    
    // Store notes to delete and restore
    std::vector<NoteUtils::DisplayNote> notesToDelete;
    std::vector<EditManager::MovingNoteIdentity::DeletedNote> notesToRestore;
    
    // STEP 2: Detect and categorize overlaps using the filtered list
    std::vector<std::pair<NoteUtils::DisplayNote, uint32_t>> notesToShorten;
    findOverlaps(otherNotesOfSamePitch, movingNotePitch, currentStart, currentEnd, newStart, newEnd, delta, loopLength,
                notesToShorten, notesToDelete);
    
    // STEP 3: Find current MIDI events and check for pitch changes
    auto onIt = std::find_if(midiEvents.begin(), midiEvents.end(), [&](MidiEvent& evt) {
        return evt.type == midi::NoteOn &&
               evt.data.noteData.note == movingNotePitch &&
               evt.tick == currentStart &&
               evt.data.noteData.velocity > 0;
    });
    auto offIt = std::find_if(midiEvents.begin(), midiEvents.end(), [&](MidiEvent& evt) {
        bool isOff = (evt.type == midi::NoteOff) || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0);
        return isOff && evt.data.noteData.note == movingNotePitch &&
               evt.tick == currentEnd;
    });
    
    // If we can't find the moving note, it might have been accidentally deleted - try to restore it
    if (onIt == midiEvents.end() || offIt == midiEvents.end()) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Moving note MIDI events not found - checking if it was accidentally deleted");
        
        // Look for the moving note in the deleted notes list
        for (auto it = manager.movingNote.deletedNotes.begin(); it != manager.movingNote.deletedNotes.end(); ++it) {
            if (it->note == movingNotePitch && it->startTick == currentStart && 
                (it->endTick == currentEnd || (!it->wasShortened && it->endTick == currentEnd))) {
                
                logger.log(CAT_MIDI, LOG_DEBUG, "Found accidentally deleted moving note - restoring it: pitch=%d, start=%lu, end=%lu", 
                          it->note, it->startTick, it->endTick);
                
                // Restore the moving note
                MidiEvent onEvt;
                onEvt.tick = it->startTick;
                onEvt.type = midi::NoteOn;
                onEvt.data.noteData.note = it->note;
                onEvt.data.noteData.velocity = it->velocity;
                midiEvents.push_back(onEvt);
                
                MidiEvent offEvt;
                offEvt.tick = it->endTick;
                offEvt.type = midi::NoteOff;
                offEvt.data.noteData.note = it->note;
                offEvt.data.noteData.velocity = 0;
                midiEvents.push_back(offEvt);
                
                // Remove from deleted list
                manager.movingNote.deletedNotes.erase(it);
                
                // Re-find the restored events
                onIt = std::find_if(midiEvents.begin(), midiEvents.end(), [&](MidiEvent& evt) {
                    return evt.type == midi::NoteOn &&
                           evt.data.noteData.note == movingNotePitch &&
                           evt.tick == currentStart &&
                           evt.data.noteData.velocity > 0;
                });
                offIt = std::find_if(midiEvents.begin(), midiEvents.end(), [&](MidiEvent& evt) {
                    bool isOff = (evt.type == midi::NoteOff) || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0);
                    return isOff && evt.data.noteData.note == movingNotePitch &&
                           evt.tick == currentEnd;
                });
                break;
            }
        }
    }
    
    if (onIt != midiEvents.end() && offIt != midiEvents.end()) {
        // Check if pitch has changed during move operation by examining the actual current pitch
        uint8_t actualCurrentPitch = onIt->data.noteData.note;
        if (actualCurrentPitch != manager.movingNote.note) {
            logger.log(CAT_MIDI, LOG_DEBUG, "Note pitch changed during move: %d -> %d, updating moving note identity", 
                      manager.movingNote.note, actualCurrentPitch);
            
            // Update the moving note identity with the new pitch
            uint8_t oldPitch = manager.movingNote.note;
            manager.movingNote.note = actualCurrentPitch;
            movingNotePitch = actualCurrentPitch;
            
            // Reindex any deleted notes that were for the old pitch to the new pitch
            for (auto& deletedNote : manager.movingNote.deletedNotes) {
                if (deletedNote.note == oldPitch) {
                    logger.log(CAT_MIDI, LOG_DEBUG, "Reindexing deleted note pitch: %d -> %d at start=%lu", 
                              deletedNote.note, actualCurrentPitch, deletedNote.startTick);
                    deletedNote.note = actualCurrentPitch;
                }
            }
        }
        
        // Move the events to new position
        onIt->tick = newStart;
        offIt->tick = newEnd;
        manager.movingNote.lastStart = newStart;
        manager.movingNote.lastEnd = newEnd;
        logger.log(CAT_MIDI, LOG_DEBUG, "Moved note events: pitch=%u start->%lu end->%lu", movingNotePitch, newStart, newEnd);
    } else {
        logger.log(CAT_MIDI, LOG_DEBUG, "Warning: could not find MIDI events for moving note pitch=%u at start=%lu end=%lu", 
                  movingNotePitch, currentStart, currentEnd);
    }
    
    // CRITICAL: Rebuild event index AFTER moving the note but BEFORE applying shortening
    // This ensures the index reflects the current state and doesn't accidentally modify the wrong events
    auto [onIndex, offIndex] = NoteUtils::buildEventIndex(midiEvents);
    
    // STEP 4: Check if we should restore any previously deleted/shortened notes
    for (const auto& deletedNote : manager.movingNote.deletedNotes) {
        // Consider restoring notes of the same pitch as the note being moved
        // Note: deletedNote.note may have been updated to match the new pitch during reindexing
        if (deletedNote.note == movingNotePitch) {
            // CRITICAL: Never restore phantom notes that match the moving note's ORIGINAL identity
            // These are artifacts from tracking issues and should not be restored
            if (deletedNote.startTick == originalStart) {
                logger.log(CAT_MIDI, LOG_DEBUG, "Skipping phantom note restore: pitch=%d, start=%lu, end=%lu (matches original moving note start)", 
                          deletedNote.note, deletedNote.startTick, deletedNote.endTick);
                continue;
            }
            
            // Additional validation: ensure the note has valid length (> 0 and < loop length)
            uint32_t deletedNoteLength = calculateNoteLength(deletedNote.startTick, deletedNote.endTick, loopLength);
            if (deletedNoteLength == 0 || deletedNoteLength >= loopLength) {
                logger.log(CAT_MIDI, LOG_DEBUG, "Skipping invalid note restore: pitch=%d, start=%lu, end=%lu (invalid length=%lu)", 
                          deletedNote.note, deletedNote.startTick, deletedNote.endTick, deletedNoteLength);
                continue;
            }
            
            // Check if the deleted note overlaps with the new position
            bool hasOverlap = notesOverlap(newStart, newEnd,
                                         deletedNote.startTick, deletedNote.endTick, loopLength);
            
            // Additional check: only restore if we're moving away from the deleted note
            bool movingAway = false;
            if (manager.movingNote.movementDirection > 0) {
                // Moving right (positive delta) - restore notes to the left of our new position
                movingAway = (deletedNote.endTick <= newStart);
            } else if (manager.movingNote.movementDirection < 0) {
                // Moving left (negative delta) - restore notes to the right of our new position
                movingAway = (deletedNote.startTick >= newStart + noteLen);
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