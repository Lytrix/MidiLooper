//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

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
#include <unordered_map>

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

// Helper to detect and categorize overlapping notes
void EditStartNoteState::findOverlaps(const std::vector<DisplayNote>& currentNotes,
                                      uint8_t movingNotePitch,
                                      uint32_t currentStart,
                                      uint32_t newStart,
                                      uint32_t newEnd,
                                      int delta,
                                      uint32_t loopLength,
                                      std::vector<std::pair<DisplayNote, uint32_t>>& notesToShorten,
                                      std::vector<DisplayNote>& notesToDelete) {
    for (const auto& note : currentNotes) {
        if (note.note != movingNotePitch) continue;
        if (note.startTick == currentStart) continue;
        bool overlaps = notesOverlap(newStart, newEnd, note.startTick, note.endTick, loopLength);
        if (!overlaps) continue;
        if (delta < 0 && note.startTick < newStart) {
            uint32_t newNoteEnd = newStart;
            uint32_t shortenedLength = calculateNoteLength(note.startTick, newNoteEnd, loopLength);
            if (shortenedLength >= Config::TICKS_PER_16TH_STEP) {
                notesToShorten.push_back({note, newNoteEnd});
                logger.debug("Will shorten note: pitch=%d, start=%lu, end=%lu->%lu, length=%lu", 
                             note.note, note.startTick, note.endTick, newNoteEnd, shortenedLength);
            } else {
                notesToDelete.push_back(note);
                logger.debug("Will delete note (too short after shortening): pitch=%d, start=%lu, end=%lu", 
                             note.note, note.startTick, note.endTick);
            }
        } else {
            notesToDelete.push_back(note);
            logger.debug("Will delete overlapping note: pitch=%d, start=%lu, end=%lu", 
                         note.note, note.startTick, note.endTick);
        }
    }
    logger.debug("Found %zu notes to shorten and %zu notes to delete", 
                 notesToShorten.size(), notesToDelete.size());
}

// Apply shorten/delete decisions on the raw MIDI event list, reusing prebuilt indexes
void EditStartNoteState::applyShortenOrDelete(std::vector<MidiEvent>& midiEvents,
                                              const std::vector<std::pair<DisplayNote, uint32_t>>& notesToShorten,
                                              const std::vector<DisplayNote>& notesToDelete,
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
        manager.movingNote.deletedNotes.push_back(original);
        logger.debug("Stored original note before shortening: pitch=%d, start=%lu, end=%lu, length=%lu",
                     original.note, original.startTick, original.endTick, original.originalLength);
        // Adjust its NoteOff event via index
        auto offKey = (NoteUtils::Key(dn.note) << 32) | dn.endTick;
        auto itOff = offIndex.find(offKey);
        if (itOff != offIndex.end()) {
            size_t idx = itOff->second;
            midiEvents[idx].tick = newEnd;
            // Update index for new tick
            offIndex.erase(itOff);
            auto newKey = (NoteUtils::Key(dn.note) << 32) | newEnd;
            offIndex[newKey] = idx;
        }
    }
    // Delete overlapping notes entirely (could similarly use onIndex/offIndex)
    for (const auto& dn : notesToDelete) {
        int deletedCount = 0;
        auto it = midiEvents.begin();
        while (it != midiEvents.end()) {
            bool matchOn  = (it->type == midi::NoteOn && it->data.noteData.velocity > 0 && it->data.noteData.note == dn.note && it->tick == dn.startTick);
            bool matchOff = ((it->type == midi::NoteOff || (it->type == midi::NoteOn && it->data.noteData.velocity == 0)) && it->data.noteData.note == dn.note && it->tick == dn.endTick);
            if (matchOn || matchOff) {
                logger.debug("Deleting MIDI event: type=%s, pitch=%d, tick=%lu",
                             (matchOn ? "NoteOn" : "NoteOff"), dn.note, (matchOn ? dn.startTick : dn.endTick));
                it = midiEvents.erase(it);
                deletedCount++;
            } else {
                ++it;
            }
        }
        if (deletedCount != 2) {
            logger.debug("Warning: deleted %d events for pitch %d (expected 2)", deletedCount, dn.note);
        }
        // Save deleted note for undo
        EditManager::MovingNoteIdentity::DeletedNote deleted;
        deleted.note = dn.note;
        deleted.velocity = dn.velocity;
        deleted.startTick = dn.startTick;
        deleted.endTick = dn.endTick;
        deleted.originalLength = calculateNoteLength(dn.startTick, dn.endTick, loopLength);
        manager.movingNote.deletedNotes.push_back(deleted);
        logger.debug("Stored deleted note: pitch=%d, start=%lu, end=%lu, length=%lu",
                     deleted.note, deleted.startTick, deleted.endTick, deleted.originalLength);
    }
}

// Helper to restore deleted or shortened notes after movement, reusing prebuilt indexes
static void restoreNotes(std::vector<MidiEvent>& midiEvents,
                         const std::vector<EditManager::MovingNoteIdentity::DeletedNote>& notesToRestore,
                         EditManager& manager,
                         uint32_t loopLength,
                         NoteUtils::EventIndexMap& onIndex,
                         NoteUtils::EventIndexMap& offIndex) {
    // Debug existing notes before restoration
    logger.log(CAT_MOVE_NOTES, LOG_DEBUG, "=== EXISTING NOTES BEFORE RESTORATION ===");
    #ifdef DEBUG_MOVE_NOTES
    {
        auto existingNotes = NoteUtils::reconstructNotes(midiEvents, loopLength);
        for (const auto& dn : existingNotes) {
            uint32_t length = calculateNoteLength(dn.startTick, dn.endTick, loopLength);
            logger.log(CAT_MOVE_NOTES, LOG_DEBUG,
                       "Existing note: pitch=%d, start=%lu, end=%lu, length=%lu",
                       dn.note, dn.startTick, dn.endTick, length);
        }
    }
    #endif
    // Restoration analysis
    logger.debug("=== RESTORATION ANALYSIS ===");
    logger.debug("Total deleted notes: %zu", manager.movingNote.deletedNotes.size());
    for (const auto& dn : manager.movingNote.deletedNotes) {
        logger.debug("Deleted note: pitch=%d, start=%lu, end=%lu, length=%lu", dn.note, dn.startTick, dn.endTick, dn.originalLength);
    }
    std::vector<EditManager::MovingNoteIdentity::DeletedNote> restored;
    for (const auto& nr : notesToRestore) {
        uint32_t targetEnd = nr.startTick + nr.originalLength;
        bool didRestore = false;
        // Try to extend an existing shortened note via index
        auto onKey = (NoteUtils::Key(nr.note) << 32) | nr.startTick;
        auto itOn = onIndex.find(onKey);
        if (itOn != onIndex.end()) {
            // Find its NoteOff event by original endTick
            auto offKey = (NoteUtils::Key(nr.note) << 32) | nr.endTick;
            auto itOff = offIndex.find(offKey);
            if (itOff != offIndex.end() && midiEvents[itOff->second].tick < targetEnd) {
                midiEvents[itOff->second].tick = targetEnd;
                logger.debug("Extended note: pitch=%d, start=%lu, new end=%lu", nr.note, nr.startTick, targetEnd);
            }
            didRestore = true;
        } else {
            // Recreate deleted note
            MidiEvent onEvt;
            onEvt.tick = nr.startTick;
            onEvt.type = midi::NoteOn;
            onEvt.data.noteData.note = nr.note;
            onEvt.data.noteData.velocity = nr.velocity;
            midiEvents.push_back(onEvt);
            MidiEvent offEvt;
            offEvt.tick = targetEnd;
            offEvt.type = midi::NoteOff;
            offEvt.data.noteData.note = nr.note;
            offEvt.data.noteData.velocity = 0;
            midiEvents.push_back(offEvt);
            logger.debug("Restored deleted note: pitch=%d, start=%lu, end=%lu", nr.note, nr.startTick, targetEnd);
            didRestore = true;
        }
        if (didRestore) restored.push_back(nr);
    }
    // Remove restored from deleted list
    for (const auto& r : restored) {
        manager.movingNote.deletedNotes.erase(
            std::remove_if(manager.movingNote.deletedNotes.begin(), manager.movingNote.deletedNotes.end(),
                [&](const auto& dn){ return dn.note==r.note && dn.startTick==r.startTick && dn.endTick==r.endTick; }),
            manager.movingNote.deletedNotes.end());
    }
    // Cleanup impossible entries
    for (auto it = manager.movingNote.deletedNotes.begin(); it != manager.movingNote.deletedNotes.end(); ) {
        if (it->note != manager.movingNote.note || it->originalLength > loopLength || it->startTick >= loopLength) {
            it = manager.movingNote.deletedNotes.erase(it);
        } else {
            ++it;
        }
    }
}

// Helper to finalize reconstruction and selection after movement
static void finalReconstructAndSelect(
    std::vector<MidiEvent>& midiEvents,
    EditManager& manager,
    uint8_t movingNotePitch,
    uint32_t newStart,
    uint32_t newEnd,
    uint32_t loopLength) {
    // Sort events by tick
    std::sort(midiEvents.begin(), midiEvents.end(),
              [](const MidiEvent &a, const MidiEvent &b){ return a.tick < b.tick; });
    // Update bracket to moved note start
    manager.setBracketTick(newStart);
    // Reconstruct final notes and select moved note
    auto finalNotes = NoteUtils::reconstructNotes(midiEvents, loopLength);
    int newSelectedIdx = -1;
    // Exact match on pitch/start/end
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
    // MIDI-event dump (visible when CAT_MOVE_NOTES is enabled)
    logger.log(CAT_MOVE_NOTES, LOG_DEBUG, "=== ALL MIDI EVENTS AFTER PROCESSING ===");
    for (const auto& evt : midiEvents) {
        logger.log(CAT_MOVE_NOTES, LOG_DEBUG, "MIDI Event: type=%s, pitch=%d, tick=%lu, velocity=%d",
                   (evt.type == midi::NoteOn) ? "NoteOn" : "NoteOff",
                   evt.data.noteData.note, evt.tick, evt.data.noteData.velocity);
    }
    // Completion log
    logger.debug("EditStartNoteState::onEncoderTurn completed successfully");
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
        uint32_t loopLength = track.getLoopLength();
        
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
            manager.movingNote.active = true;
            manager.movingNote.movementDirection = 0;
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
    manager.movingNote.deletedNotes.clear();
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
    // IDEA: if you want to restore the original length of the note when starting a new move, then remove this line
    manager.movingNote.deletedNotes.clear();
}

// 2. onEncoderTurn(): move a note's start/end based on encoder spinning.
void EditStartNoteState::onEncoderTurn(EditManager& manager, Track& track, int delta) {
    logger.debug("EditStartNoteState::onEncoderTurn called with delta=%d", delta);
    
    auto& midiEvents = track.getMidiEvents();
    uint32_t loopLength = track.getLoopLength();
    
    logger.debug("=== LOOP INFO ===");
    logger.debug("Loop length: %lu ticks", loopLength);
    logger.debug("Loop length in bars: %lu", loopLength / Config::TICKS_PER_BAR);
    logger.debug("MIDI events count: %zu", midiEvents.size());
    logger.debug("Selected note index: %d", manager.getSelectedNoteIdx());
    
    if (loopLength == 0) {
        logger.debug("EditStartNoteState: Loop length is 0, cannot move notes");
        return;
    }
    
    // If we don't have an active moving note, set it up
    if (!manager.movingNote.active) {
        logger.debug("EditStartNoteState: No active moving note, movingNote.active = false");
        logger.debug("EditStartNoteState: Attempting to activate moving note...");
        
        // Try to activate based on selected note
        if (manager.getSelectedNoteIdx() >= 0) {
            const auto& notes = track.getCachedNotes();
            if (manager.getSelectedNoteIdx() < (int)notes.size()) {
                auto& note = notes[manager.getSelectedNoteIdx()];
                manager.movingNote.note = note.note;
                manager.movingNote.origStart = note.startTick;
                manager.movingNote.origEnd = note.endTick;
                manager.movingNote.lastStart = note.startTick;
                manager.movingNote.lastEnd = note.endTick;
                manager.movingNote.active = true;
                manager.movingNote.movementDirection = 0;
                logger.debug("EditStartNoteState: Activated moving note: pitch=%d, start=%lu, end=%lu", 
                             note.note, note.startTick, note.endTick);
            } else {
                logger.debug("EditStartNoteState: Selected note index %d out of range (notes size: %zu)", 
                             manager.getSelectedNoteIdx(), notes.size());
            }
        } else {
            logger.debug("EditStartNoteState: No note selected (selectedNoteIdx = %d)", manager.getSelectedNoteIdx());
        }
        
        if (!manager.movingNote.active) {
            logger.debug("EditStartNoteState: Still no active moving note, cannot move");
            return;
        }
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
    
    // Build event index once and reuse
    auto [onIndex, offIndex] = NoteUtils::buildEventIndex(midiEvents);
    // Detect and categorize overlaps in one shared helper
    std::vector<std::pair<DisplayNote, uint32_t>> notesToShorten;
    findOverlaps(currentNotes, movingNotePitch, currentStart, newStart, newEnd, delta, loopLength,
                 notesToShorten, notesToDelete);
    
    // Move the selected note's NoteOn/NoteOff events before adjusting overlaps
    {
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
            logger.debug("Moved note events: pitch=%u start->%lu end->%lu", movingNotePitch, newStart, newEnd);
        } else {
            logger.debug("Warning: could not find MIDI events for moving note pitch=%u", movingNotePitch);
        }
    }
    
    // Apply shorten/delete using shared index
    applyShortenOrDelete(midiEvents,
                         notesToShorten,
                         notesToDelete,
                         manager,
                         loopLength,
                         onIndex,
                         offIndex);
    
    // Restore notes that should be restored based on movement, reusing index
    restoreNotes(midiEvents,
                 notesToRestore,
                 manager,
                 loopLength,
                 onIndex,
                 offIndex);
    
    // Helper to finalize reconstruction and selection after movement
    finalReconstructAndSelect(midiEvents, manager, movingNotePitch, newStart, newEnd, loopLength);
}

// 4. onButtonPress(): exit move mode and return to NoteState.
void EditStartNoteState::onButtonPress(EditManager& manager, Track& track) {
    // Switch back to note state
    manager.setState(manager.getNoteState(), track, manager.getBracketTick());
} 