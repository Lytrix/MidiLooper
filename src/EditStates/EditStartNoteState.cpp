#include "EditStartNoteState.h"
#include "EditManager.h"
#include "MidiEvent.h"
#include "Track.h"
#include <vector>
#include <algorithm>
#include <cstdint>
#include "Logger.h"
#include <map>
#include "Globals.h"
#include "TrackUndo.h"
#include "NoteUtils.h"

using DisplayNote = NoteUtils::DisplayNote;

/**
 * @class EditStartNoteState
 * @brief Implements:
 *   1. onEnter() – set up moving note identity and bracket.
 *   2. onEncoderTurn() – move a note's start/end (detailed in Flow of onEncoderTurn; stores raw end and defers any wrapping to display/playback layers).
 *   3. onExit() – clear move mode state.
 *   4. onButtonPress() – exit move mode and return to NoteState.
 *
 * Flow of onEncoderTurn():
 *   1. Read current moving-note identity (pitch, lastStart, lastEnd).
 *   2. Compute newStart = lastStart + δ (wrapping at loopLength), then compute raw newEnd = newStart + noteLen (no chopping at loop boundary); store raw newEnd in MIDI and defer wrapping to display/playback.
 *   3. Reconstruct all DisplayNotes from MIDI events into currentNotes.
 *   4. From manager.movingNote.deletedNotes decide which notes to restore (no longer overlapping).
 *   5. Scan currentNotes for ones that overlap the moved note; for right-to-left motion try to shorten them, otherwise queue them for deletion.
 *   6. Move your NoteOn/NoteOff events to newStart/newEnd.
 *   7. Apply shortening, then deletion, and record all DeletedNote entries into manager.movingNote.deletedNotes.
 *   8. Re-insert any notes that no longer overlap.
 *   9. Sort the MIDI event list, update manager.movingNote.lastStart/lastEnd and bracket, and reselect the moved note index.
 */

// Helper functions for wrap-around calculations and overlap detection
static uint32_t wrapPosition(int32_t position, uint32_t loopLength) {
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

static uint32_t calculateNoteLength(uint32_t start, uint32_t end, uint32_t loopLength) {
    if (end >= start) {
        return end - start;
    } else {
        return (loopLength - start) + end;
    }
}

static bool notesOverlap(uint32_t start1, uint32_t end1, uint32_t start2, uint32_t end2, uint32_t loopLength) {
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

// 1. onEnter(): set up moving note identity and bracket.
void EditStartNoteState::onEnter(EditManager& manager, Track& track, uint32_t startTick) {
    logger.debug("Entered EditStartNoteState");
    // Commit-on-enter: snapshot and hash initial MIDIEVENTS
    initialHash = TrackUndo::computeMidiHash(track);
    TrackUndo::pushUndoSnapshot(track);
    logger.debug("Snapshot on enter, initial hash: %u", initialHash);
    int idx = manager.getSelectedNoteIdx();
    if (idx >= 0) {
        // Don't rebuild notes here - just preserve the current selection
        // and set up the moving note identity for tracking
        uint32_t loopLength = track.getLength();
        
        // Reconstruct notes using shared utility
        auto& midiEvents = track.getMidiEvents();
        std::vector<DisplayNote> notes = NoteUtils::reconstructNotes(midiEvents, loopLength);
        
        if (idx < (int)notes.size()) {
            // Record persistent identity for the currently selected note
            manager.movingNote.note = notes[idx].note;
            manager.movingNote.origStart = notes[idx].startTick;
            manager.movingNote.origEnd = notes[idx].endTick;
            manager.movingNote.lastStart = notes[idx].startTick;
            manager.movingNote.lastEnd = notes[idx].endTick;
            manager.movingNote.wrapCount = 0;
            manager.movingNote.active = true;
            manager.movingNote.movementDirection = 0;
            manager.movingNote.deletedEvents.clear();
            manager.movingNote.deletedEventIndices.clear();
            // Set bracket to this note's start
            manager.setBracketTick(notes[idx].startTick);
            logger.debug("Set up moving note identity: note=%d, start=%lu, end=%lu", 
                         notes[idx].note, notes[idx].startTick, notes[idx].endTick);
            // Reset commit-on-exit snapshot flag; will push on first actual move
            manager.movingNote.undoSnapshotPushed = false;
        }
    } else {
        manager.movingNote.active = false;
        manager.selectClosestNote(track, startTick);
    }
}

// 3. onExit(): clear move mode state.
void EditStartNoteState::onExit(EditManager& manager, Track& track) {
    logger.debug("Exited EditStartNoteState");
    
    // Clear any stored deleted events when exiting edit mode
    if (!manager.movingNote.deletedEvents.empty()) {
        logger.debug("Clearing %zu deleted events on exit", manager.movingNote.deletedEvents.size());
        manager.movingNote.deletedEvents.clear();
        manager.movingNote.deletedEventIndices.clear();
    }
    
    // Reset movement tracking
    manager.movingNote.movementDirection = 0;
    manager.movingNote.active = false;
}

// 2. onEncoderTurn(): move a note's start/end based on encoder spinning.
void EditStartNoteState::onEncoderTurn(EditManager& manager, Track& track, int delta) {
    logger.debug("EditStartNoteState::onEncoderTurn called with delta=%d", delta);
    
    auto& midiEvents = track.getMidiEvents();
    uint32_t loopLength = track.getLength();
    
    logger.debug("=== LOOP INFO ===");
    logger.debug("Loop length: %lu ticks", loopLength);
    logger.debug("Loop length in bars: %lu", loopLength / Config::TICKS_PER_BAR);
    
    if (loopLength == 0) {
        logger.debug("Loop length is 0, cannot move notes");
        return;
    }
    
    // If we don't have an active moving note, set it up
    if (!manager.movingNote.active) {
        logger.debug("No active moving note, cannot move");
        return;
    }
    
    // Update movement direction
    if (delta > 0) {
        manager.movingNote.movementDirection = 1; // Moving left (positive delta)
    } else if (delta < 0) {
        manager.movingNote.movementDirection = -1; // Moving right (negative delta)
    }
    
    // Use the moving note identity instead of reconstructing from selected index
    // This ensures we always track the same note even after MIDI events change
    uint8_t movingNotePitch = manager.movingNote.note;
    uint32_t currentStart = manager.movingNote.lastStart;
    uint32_t currentEnd = manager.movingNote.lastEnd;
    
    logger.debug("Moving note: pitch=%d, start=%lu, end=%lu", 
                 movingNotePitch, currentStart, currentEnd);
    
    // Calculate note length and new positions with wrap-around
    // displayEnd accounts for currentEnd possibly > loopLength
    uint32_t displayCurrentEnd = (currentEnd >= loopLength) ? (currentEnd % loopLength) : currentEnd;
    uint32_t noteLen = calculateNoteLength(currentStart, displayCurrentEnd, loopLength);
    int32_t rawNewStart = (int32_t)currentStart + delta;
    uint32_t newStart = wrapPosition(rawNewStart, loopLength);
    // actual MIDI new end (no modulo) and display end for UI/overlap logic
    uint32_t newEnd = newStart + noteLen;
    uint32_t displayNewEnd = newEnd % loopLength; // only for UI/debug; storage uses raw newEnd
    
    logger.debug("Movement: start %lu->%lu, end actual %lu (display %lu), length=%lu", 
                 currentStart, newStart, newEnd, displayNewEnd, noteLen);
    
    // Calculate how many more steps are needed to reach loop boundary
    uint32_t distanceToWrap = 0;
    if (newEnd < loopLength) {
        distanceToWrap = loopLength - newEnd;
        logger.debug("Distance to loop boundary: %lu ticks (%lu steps)", distanceToWrap, distanceToWrap);
    } else {
        logger.debug("Note WILL WRAP - newEnd=%lu >= loopLength=%lu", newEnd, loopLength);
    }
    
    // Reconstruct notes for overlap detection and selection
    std::vector<DisplayNote> currentNotes = NoteUtils::reconstructNotes(midiEvents, loopLength);
    
    // Store notes to delete and restore
    std::vector<DisplayNote> notesToDelete;
    std::vector<EditManager::MovingNoteIdentity::DeletedNote> notesToRestore;
    
    // Check if we should restore any previously deleted/shortened notes
    {
        for (const auto& deletedNote : manager.movingNote.deletedNotes) {
            // Only consider restoring notes of the same pitch as the note being moved
            if (deletedNote.note == movingNotePitch) {
                // Check if the deleted note overlaps with the new position
                // Use display coordinates consistently for overlap detection
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
                    logger.debug("Will restore note: pitch=%d, start=%lu, end=%lu (no overlap with %lu-%lu, moving away)", 
                                 deletedNote.note, deletedNote.startTick, deletedNote.endTick, newStart, newEnd);
                } else {
                    logger.debug("Cannot restore note: pitch=%d, start=%lu, end=%lu (overlap=%s, movingAway=%s)", 
                                 deletedNote.note, deletedNote.startTick, deletedNote.endTick, 
                                 hasOverlap ? "yes" : "no", movingAway ? "yes" : "no");
                }
            }
        }
    }
    
    logger.debug("Found %zu notes to restore, %zu total deleted notes", 
                 notesToRestore.size(), manager.movingNote.deletedNotes.size());
    
    // Store notes to shorten
    std::vector<std::pair<DisplayNote, uint32_t>> notesToShorten; // note and new end tick
    
    // Check for overlaps with other notes and determine what to do
    {
        for (const auto& note : currentNotes) {
            // Skip if this is not the same pitch (different notes can't overlap in MIDI)
            if (note.note != movingNotePitch) {
                continue;
            }
            
            // Skip the currently moving note (identified by its original startTick before update)
            if (note.startTick == currentStart) {
                continue;
            }
            
            // Check for overlap using consistent wrapped coordinates
            bool overlaps = notesOverlap(newStart, newEnd, note.startTick, note.endTick, loopLength);
            
            logger.debug("Checking overlap: moving note %lu-%lu vs existing note %lu-%lu, overlaps=%s", 
                        newStart, newEnd, note.startTick, note.endTick, overlaps ? "YES" : "NO");
            
            if (overlaps) {
                // For right-to-left movement (negative delta), try to shorten the note instead of deleting
                if (delta < 0 && note.startTick < newStart) {
                    // Calculate what the new end would be if we shorten it
                    uint32_t newNoteEnd = newStart;
                    uint32_t shortenedLength = calculateNoteLength(note.startTick, newNoteEnd, loopLength);
                    
                    // Only shorten if the result would be at least 48 ticks (1/16th note)
                    if (shortenedLength >= Config::TICKS_PER_16TH_STEP) {
                        notesToShorten.push_back({note, newNoteEnd});
                        logger.debug("Will shorten note: pitch=%d, start=%lu, end=%lu->%lu, length=%lu", 
                                   note.note, note.startTick, note.endTick, newNoteEnd, shortenedLength);
                    } else {
                        // Too short after shortening, delete it
                        notesToDelete.push_back(note);
                        logger.debug("Will delete note (too short after shortening): pitch=%d, start=%lu, end=%lu", 
                                   note.note, note.startTick, note.endTick);
                    }
                } else {
                    // For left-to-right movement or other cases, delete the note
                    notesToDelete.push_back(note);
                    logger.debug("Will delete overlapping note: pitch=%d, start=%lu, end=%lu", 
                               note.note, note.startTick, note.endTick);
                }
            }
        }
    }
    
    logger.debug("Found %zu notes to delete", notesToDelete.size());
    
    // Move the selected note's MIDI events
    auto onIt = std::find_if(midiEvents.begin(), midiEvents.end(), [&](MidiEvent& evt) {
        return evt.type == midi::NoteOn && 
               evt.data.noteData.note == movingNotePitch && 
               evt.tick == currentStart;
    });
    
    // Find the NoteOff event - need to handle both wrapped and unwrapped coordinates
    auto offIt = std::find_if(midiEvents.begin(), midiEvents.end(), [&](MidiEvent& evt) {
        if ((evt.type == midi::NoteOff || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) && 
            evt.data.noteData.note == movingNotePitch) {
            // Try matching with both the current end and the original end coordinates
            return (evt.tick == currentEnd) || (evt.tick == manager.movingNote.origEnd);
        }
        return false;
    });
    
    if (onIt == midiEvents.end() || offIt == midiEvents.end()) {
        logger.debug("Could not find MIDI events for selected note - searching alternatives");
        
        // Debug: show all MIDI events for this pitch to understand what's available
        for (const auto& evt : midiEvents) {
            if (evt.data.noteData.note == movingNotePitch) {
                logger.debug("Available MIDI event: type=%s, tick=%lu", 
                           (evt.type == midi::NoteOn) ? "NoteOn" : "NoteOff", evt.tick);
            }
        }
        return;
    }
    
    // Update the MIDI events with raw end to preserve full note length
    onIt->tick = newStart;
    offIt->tick = newEnd;  // Store raw end (no chopping)
    
    // Update tracking coordinates - preserve raw start/end
    manager.movingNote.lastStart = newStart;
    manager.movingNote.lastEnd = newEnd;
    
    logger.debug("Updated MIDI events: NoteOn at %lu, NoteOff at %lu", onIt->tick, offIt->tick);
    
    // Debug: Count existing events for this pitch at newStart
    int noteOnCount = 0;
    for (const auto& evt : midiEvents) {
        if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 && 
            evt.data.noteData.note == movingNotePitch && evt.tick == newStart) {
            noteOnCount++;
        }
    }
    logger.debug("Found %d NoteOn events for pitch %d at tick %lu", noteOnCount, movingNotePitch, newStart);
    
    // Debug: Show all MIDI events before processing
    logger.debug("=== ALL MIDI EVENTS BEFORE PROCESSING ===");
    for (const auto& evt : midiEvents) {
        if (evt.data.noteData.note == 51) { // Focus on pitch 51 where the problem occurs
            logger.debug("MIDI Event: type=%s, pitch=%d, tick=%lu, velocity=%d", 
                       (evt.type == midi::NoteOn) ? "NoteOn" : "NoteOff",
                       evt.data.noteData.note, evt.tick, evt.data.noteData.velocity);
        }
    }
    
    // Apply shortening and deletion to notes that overlap
    for (const auto& noteToShorten : notesToShorten) {
        // Store the original note for potential restoration
        EditManager::MovingNoteIdentity::DeletedNote originalNote;
        originalNote.note = noteToShorten.first.note;
        originalNote.velocity = noteToShorten.first.velocity;
        originalNote.startTick = noteToShorten.first.startTick;
        originalNote.endTick = noteToShorten.first.endTick;
        // Store the original length for consistent restoration
        originalNote.originalLength = calculateNoteLength(noteToShorten.first.startTick, noteToShorten.first.endTick, loopLength);
        manager.movingNote.deletedNotes.push_back(originalNote);
        
        logger.debug("Stored original note before shortening: pitch=%d, start=%lu, end=%lu, length=%lu", 
                     originalNote.note, originalNote.startTick, originalNote.endTick, originalNote.originalLength);

        // Find and shorten the overlapping note
        for (auto& evt : midiEvents) {
            if ((evt.type == midi::NoteOff || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) &&
                evt.data.noteData.note == noteToShorten.first.note && 
                evt.tick == noteToShorten.first.endTick) {
                evt.tick = noteToShorten.second;  // Shorten to the new end tick
                break;
            }
        }
    }
    
    for (const auto& noteToDelete : notesToDelete) {
        // Delete the overlapping note completely
        auto it = midiEvents.begin();
        int deletedCount = 0;
        while (it != midiEvents.end()) {
            if ((it->type == midi::NoteOn && it->data.noteData.velocity > 0 &&
                 it->data.noteData.note == noteToDelete.note && it->tick == noteToDelete.startTick) ||
                ((it->type == midi::NoteOff || (it->type == midi::NoteOn && it->data.noteData.velocity == 0)) &&
                 it->data.noteData.note == noteToDelete.note && it->tick == noteToDelete.endTick)) {
                logger.debug("Deleting MIDI event: type=%s, pitch=%d, tick=%lu", 
                           (it->type == midi::NoteOn) ? "NoteOn" : "NoteOff", 
                           it->data.noteData.note, it->tick);
                it = midiEvents.erase(it);
                deletedCount++;
            } else {
                ++it;
            }
        }
        
        if (deletedCount != 2) {
            logger.debug("Warning: Expected to delete 2 events for note, but deleted %d events", deletedCount);
        }
        
        // Store deleted note for potential restoration
        EditManager::MovingNoteIdentity::DeletedNote deletedNote;
        deletedNote.note = noteToDelete.note;
        deletedNote.velocity = noteToDelete.velocity;
        deletedNote.startTick = noteToDelete.startTick;
        deletedNote.endTick = noteToDelete.endTick;
        // Store the original length for consistent restoration
        deletedNote.originalLength = calculateNoteLength(noteToDelete.startTick, noteToDelete.endTick, loopLength);
        manager.movingNote.deletedNotes.push_back(deletedNote);
        
        logger.debug("Stored deleted note: pitch=%d, start=%lu, end=%lu, length=%lu", 
                     deletedNote.note, deletedNote.startTick, deletedNote.endTick, deletedNote.originalLength);
    }
    
    // Sort events by tick BEFORE restoration to ensure clean state
    std::sort(midiEvents.begin(), midiEvents.end(),
              [](auto const &a, auto const &b){ return a.tick < b.tick; });
    
    // Debug: Show all existing notes before restoration
    logger.debug("=== EXISTING NOTES BEFORE RESTORATION ===");
    std::map<uint8_t, std::vector<std::pair<uint32_t, uint32_t>>> existingNotesByPitch;
    for (const auto& evt : midiEvents) {
        if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0) {
            // Find matching NoteOff
            for (const auto& endEvt : midiEvents) {
                if ((endEvt.type == midi::NoteOff || (endEvt.type == midi::NoteOn && endEvt.data.noteData.velocity == 0)) &&
                    endEvt.data.noteData.note == evt.data.noteData.note && endEvt.tick > evt.tick) {
                    uint32_t length = calculateNoteLength(evt.tick, endEvt.tick, loopLength);
                    logger.debug("Existing note: pitch=%d, start=%lu, end=%lu, length=%lu", 
                               evt.data.noteData.note, evt.tick, endEvt.tick, length);
                    existingNotesByPitch[evt.data.noteData.note].push_back({evt.tick, endEvt.tick});
                    break;
                }
            }
        }
    }
    
    // Restore notes that should be restored based on movement
    std::vector<EditManager::MovingNoteIdentity::DeletedNote> notesToRemoveFromDeleted;
    
    logger.debug("=== RESTORATION ANALYSIS ===");
    logger.debug("Total deleted notes: %zu", manager.movingNote.deletedNotes.size());
    for (const auto& dn : manager.movingNote.deletedNotes) {
        logger.debug("Deleted note: pitch=%d, start=%lu, end=%lu, length=%lu", 
                   dn.note, dn.startTick, dn.endTick, dn.originalLength);
    }
    
    for (const auto& noteToRestore : notesToRestore) {
        logger.debug("Attempting to restore note: pitch=%d, start=%lu, stored_end=%lu, length=%lu", 
                     noteToRestore.note, noteToRestore.startTick, noteToRestore.endTick, noteToRestore.originalLength);
        
        // Calculate the target end tick using stored original length
        uint32_t targetEndTick = noteToRestore.startTick + noteToRestore.originalLength;
        
        // Check for and remove ultra-short conflicting notes that are likely corrupted
        bool removedCorruptedNote = false;
            auto it = midiEvents.begin();
            while (it != midiEvents.end()) {
            if (it->type == midi::NoteOn && it->data.noteData.velocity > 0 && 
                it->data.noteData.note == noteToRestore.note) {
                
                // Find the end of this existing note
                uint32_t existingEnd = it->tick;
                auto endIt = midiEvents.end();
                for (auto endSearch = midiEvents.begin(); endSearch != midiEvents.end(); ++endSearch) {
                    if ((endSearch->type == midi::NoteOff || (endSearch->type == midi::NoteOn && endSearch->data.noteData.velocity == 0)) &&
                        endSearch->data.noteData.note == noteToRestore.note && endSearch->tick > it->tick) {
                        existingEnd = endSearch->tick;
                        endIt = endSearch;
                        break;
                    }
                }
                
                // Check if this is an ultra-short note (1-2 ticks) that conflicts with restoration
                uint32_t noteLength = existingEnd - it->tick;
                bool isUltraShort = (noteLength <= 2);
                bool conflictsWithRestore = notesOverlap(noteToRestore.startTick, targetEndTick, it->tick, existingEnd, loopLength);
                
                if (isUltraShort && conflictsWithRestore) {
                    logger.debug("Removing corrupted ultra-short note that blocks restoration: pitch=%d, start=%lu, end=%lu, length=%lu", 
                               it->data.noteData.note, it->tick, existingEnd, noteLength);
                    
                    // Remove both NoteOn and NoteOff events
                    if (endIt != midiEvents.end()) {
                        // Remove NoteOff first (higher index)
                        midiEvents.erase(endIt);
                    }
                    // Remove NoteOn
                    it = midiEvents.erase(it);
                    removedCorruptedNote = true;
                    continue; // Don't increment it since we erased
                }
            }
                    ++it;
        }
        
        if (removedCorruptedNote) {
            logger.debug("Removed corrupted notes blocking restoration - retrying conflict detection");
        }
        
        // Check if restoring this note would conflict with any remaining existing notes
        bool hasConflict = false;
        for (const auto& evt : midiEvents) {
            if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 && 
                evt.data.noteData.note == noteToRestore.note) {
                
                // Find the end of this existing note
                uint32_t existingEnd = evt.tick; // Default if no end found
                for (const auto& endEvt : midiEvents) {
                    if ((endEvt.type == midi::NoteOff || (endEvt.type == midi::NoteOn && endEvt.data.noteData.velocity == 0)) &&
                        endEvt.data.noteData.note == noteToRestore.note && endEvt.tick > evt.tick) {
                        existingEnd = endEvt.tick;
                        break;
                    }
                }
                
                // Check for overlap with what we want to restore
                if (notesOverlap(noteToRestore.startTick, targetEndTick, evt.tick, existingEnd, loopLength)) {
                    hasConflict = true;
                    logger.debug("Cannot restore note: conflicts with existing note at %lu-%lu", evt.tick, existingEnd);
                    break;
                }
            }
        }
        
        if (!hasConflict) {
            // Check if this is a shortened note (has a NoteOn event but different end)
            auto onIt = std::find_if(midiEvents.begin(), midiEvents.end(), [&](const MidiEvent& evt) {
                return evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 &&
                   evt.data.noteData.note == noteToRestore.note && 
                   evt.tick == noteToRestore.startTick;
            });
            
            if (onIt != midiEvents.end()) {
                // This is a shortened note - find its current end event and extend it
                auto endIt = midiEvents.end();
                uint32_t closestEndTick = UINT32_MAX;
                
                for (auto it = midiEvents.begin(); it != midiEvents.end(); ++it) {
                    if ((it->type == midi::NoteOff || (it->type == midi::NoteOn && it->data.noteData.velocity == 0)) && 
                        it->data.noteData.note == noteToRestore.note && 
                        it->tick > noteToRestore.startTick && 
                        it->tick < closestEndTick) {
                        endIt = it;
                        closestEndTick = it->tick;
                    }
                }
                
                if (endIt != midiEvents.end() && endIt->tick < targetEndTick) {
                    endIt->tick = targetEndTick;
                    logger.debug("Extended shortened note: pitch=%d, start=%lu, end=%lu->%lu (length=%lu)", 
                               noteToRestore.note, noteToRestore.startTick, closestEndTick, targetEndTick, noteToRestore.originalLength);
                }
            } else {
                // This is a completely deleted note - recreate it
                MidiEvent onEvent;
                onEvent.type = midi::NoteOn;
                onEvent.tick = noteToRestore.startTick;
                onEvent.data.noteData.note = noteToRestore.note;
                onEvent.data.noteData.velocity = noteToRestore.velocity;
                midiEvents.push_back(onEvent);
                
                MidiEvent offEvent;
                offEvent.type = midi::NoteOff;
                offEvent.tick = targetEndTick;
                offEvent.data.noteData.note = noteToRestore.note;
                offEvent.data.noteData.velocity = 0;
                midiEvents.push_back(offEvent);
                
                logger.debug("Restored deleted note: pitch=%d, start=%lu, end=%lu (length=%lu)", 
                           noteToRestore.note, noteToRestore.startTick, targetEndTick, noteToRestore.originalLength);
            }
            
            // Mark for removal from deleted notes list
            notesToRemoveFromDeleted.push_back(noteToRestore);
        }
    }
    
    // Remove restored notes from deleted notes list (do this after restoration to avoid iterator issues)
    for (const auto& noteToRemove : notesToRemoveFromDeleted) {
        auto it = std::find_if(manager.movingNote.deletedNotes.begin(), 
                             manager.movingNote.deletedNotes.end(),
                             [&](const auto& dn) {
            return dn.note == noteToRemove.note && 
                   dn.startTick == noteToRemove.startTick && 
                   dn.endTick == noteToRemove.endTick;
        });
        if (it != manager.movingNote.deletedNotes.end()) {
            manager.movingNote.deletedNotes.erase(it);
            logger.debug("Removed restored note from deleted list: pitch=%d, start=%lu, end=%lu", 
                       noteToRemove.note, noteToRemove.startTick, noteToRemove.endTick);
        }
    }
    
    // Clean up only truly corrupted entries from the deleted list
    auto it = manager.movingNote.deletedNotes.begin();
    while (it != manager.movingNote.deletedNotes.end()) {
        bool isCorrupted = false;
        
        // Only remove notes that are clearly corrupted (impossible characteristics)
        if (it->note != movingNotePitch) {
            isCorrupted = true;
            logger.debug("Removing deleted note from different pitch: pitch=%d (current pitch=%d)", 
                       it->note, movingNotePitch);
        }
        // Check for impossible length (longer than the entire loop)
        else if (it->originalLength > loopLength) {
            isCorrupted = true;
            logger.debug("Removing corrupted deleted note with impossible length: length=%lu > loopLength=%lu", 
                       it->originalLength, loopLength);
        }
        // Check for impossible start position
        else if (it->startTick >= loopLength) {
            isCorrupted = true;
            logger.debug("Removing corrupted deleted note with impossible start: start=%lu >= loopLength=%lu", 
                       it->startTick, loopLength);
        }
        
        if (isCorrupted) {
            it = manager.movingNote.deletedNotes.erase(it);
        } else {
            ++it;
        }
    }

    // Sort events by tick
    std::sort(midiEvents.begin(), midiEvents.end(),
              [](auto const &a, auto const &b){ return a.tick < b.tick; });

    // Set bracket to new position (already updated tracking above)
    manager.setBracketTick(newStart);
    
    // Reconstruct notes to find the new selected index
    auto finalNotes = NoteUtils::reconstructNotes(midiEvents, loopLength);
    int newSelectedIdx = -1;
    // Try exact match on pitch, start, and end
    for (int i = 0; i < (int)finalNotes.size(); ++i) {
        if (finalNotes[i].note == movingNotePitch &&
            finalNotes[i].startTick == newStart &&
            finalNotes[i].endTick == newEnd) {
            newSelectedIdx = i;
            break;
        }
    }
    // Fallback: match only pitch and start
    if (newSelectedIdx < 0) {
        for (int i = 0; i < (int)finalNotes.size(); ++i) {
            if (finalNotes[i].note == movingNotePitch &&
                finalNotes[i].startTick == newStart) {
                newSelectedIdx = i;
                break;
            }
        }
    }
    if (newSelectedIdx >= 0) {
        manager.setSelectedNoteIdx(newSelectedIdx);
        logger.debug("Updated selectedNoteIdx to %d for moved note", newSelectedIdx);
    } else {
        logger.debug("Warning: Could not find moved note in final note list");
    }
    
    // Debug: Show all MIDI events after processing
    logger.debug("=== ALL MIDI EVENTS AFTER PROCESSING ===");
    for (const auto& evt : midiEvents) {
        if (evt.data.noteData.note == 51) { // Focus on pitch 51 where the problem occurs
            logger.debug("MIDI Event: type=%s, pitch=%d, tick=%lu, velocity=%d", 
                       (evt.type == midi::NoteOn) ? "NoteOn" : "NoteOff",
                       evt.data.noteData.note, evt.tick, evt.data.noteData.velocity);
        }
    }
    
    logger.debug("Updated selectedNoteIdx to %d for moved note", newSelectedIdx);
    
    logger.debug("EditStartNoteState::onEncoderTurn completed successfully");
}

// 4. onButtonPress(): exit move mode and return to NoteState.
void EditStartNoteState::onButtonPress(EditManager& manager, Track& track) {
    // Switch back to note state
    manager.setState(manager.getNoteState(), track, manager.getBracketTick());
} 