//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0


#include "Logger.h"
#include "Globals.h"
#include "Utils/MidiEventUtils.h"
#include "Utils/NoteMovementUtils.h"
#include "Utils/DebugSessionCapture.h"
#include <algorithm>
#include <set>

namespace NoteMovementUtils {

namespace {

bool isAlreadyShortenedVictim(const EditManager& manager,
                              const NoteUtils::DisplayNote& note) {
    auto matchesShortened = [&](const EditManager::MovingNoteIdentity::DeletedNote& deletedNote) {
        return deletedNote.wasShortened &&
               deletedNote.note == note.note &&
               deletedNote.startTick == note.startTick &&
               deletedNote.shortenedToTick == note.endTick;
    };
    for (const auto& deletedNote : manager.movingNote.deletedNotes) {
        if (matchesShortened(deletedNote)) {
            return true;
        }
    }
    for (const auto& deletedNote : manager.sessionShortenedVictims) {
        if (matchesShortened(deletedNote)) {
            return true;
        }
    }
    return false;
}

bool hasShortenedVictimEntry(const EditManager& manager,
                             uint8_t pitch,
                             uint32_t startTick) {
    for (const auto& deletedNote : manager.movingNote.deletedNotes) {
        if (deletedNote.wasShortened &&
            deletedNote.note == pitch &&
            deletedNote.startTick == startTick) {
            return true;
        }
    }
    for (const auto& victim : manager.sessionShortenedVictims) {
        if (victim.note == pitch && victim.startTick == startTick) {
            return true;
        }
    }
    return false;
}

void upsertSessionShortenedVictim(
    EditManager& manager,
    const EditManager::MovingNoteIdentity::DeletedNote& victim) {
    if (!victim.wasShortened) {
        return;
    }
    for (auto& entry : manager.sessionShortenedVictims) {
        if (entry.note == victim.note && entry.startTick == victim.startTick) {
            entry = victim;
            return;
        }
    }
    manager.sessionShortenedVictims.push_back(victim);
}

void removeSessionShortenedVictim(EditManager& manager, uint8_t pitch, uint32_t startTick) {
    manager.sessionShortenedVictims.erase(
        std::remove_if(manager.sessionShortenedVictims.begin(),
                       manager.sessionShortenedVictims.end(),
                       [&](const auto& entry) {
                           return entry.note == pitch && entry.startTick == startTick;
                       }),
        manager.sessionShortenedVictims.end());
}

void upsertSessionDeletedNote(
    EditManager& manager,
    const EditManager::MovingNoteIdentity::DeletedNote& victim) {
    if (victim.wasShortened) {
        return;
    }
    for (auto& entry : manager.sessionDeletedNotes) {
        if (entry.note == victim.note && entry.startTick == victim.startTick &&
            entry.endTick == victim.endTick) {
            entry = victim;
            return;
        }
    }
    manager.sessionDeletedNotes.push_back(victim);
}

void removeSessionDeletedNote(EditManager& manager, uint8_t pitch, uint32_t startTick) {
    manager.sessionDeletedNotes.erase(
        std::remove_if(manager.sessionDeletedNotes.begin(),
                       manager.sessionDeletedNotes.end(),
                       [&](const auto& entry) {
                           return entry.note == pitch && entry.startTick == startTick;
                       }),
        manager.sessionDeletedNotes.end());
}

void appendUniqueRestoreCandidate(
    std::vector<EditManager::MovingNoteIdentity::DeletedNote>& notesToRestore,
    const EditManager::MovingNoteIdentity::DeletedNote& candidate) {
    for (const auto& queued : notesToRestore) {
        if (queued.note == candidate.note && queued.startTick == candidate.startTick) {
            return;
        }
    }
    notesToRestore.push_back(candidate);
}

uint32_t deletedNoteEffectiveEnd(
    const EditManager::MovingNoteIdentity::DeletedNote& deletedNote) {
    return deletedNote.wasShortened ? deletedNote.shortenedToTick : deletedNote.endTick;
}

uint32_t movingNoteSessionSpanEnd(const EditManager& manager, uint32_t loopLength) {
    uint32_t sessionSpanEnd = manager.movingNote.origEnd;
    if (!manager.movingNote.active || loopLength == 0) {
        return sessionSpanEnd;
    }
    const uint32_t displayLastEnd =
        (manager.movingNote.lastEnd >= loopLength)
            ? (manager.movingNote.lastEnd % loopLength)
            : manager.movingNote.lastEnd;
    if (displayLastEnd > sessionSpanEnd) {
        sessionSpanEnd = displayLastEnd;
    }
    return sessionSpanEnd;
}

bool isInnerNoteUnderSessionSpan(const EditManager& manager, uint8_t notePitch,
                                 uint32_t noteStart, uint32_t noteEnd,
                                 uint32_t loopLength) {
    if (!manager.movingNote.active || loopLength == 0) {
        return false;
    }
    if (notePitch == manager.movingNote.origPitch) {
        return false;
    }
    return isNoteWithinMovingSpan(
        noteStart, noteEnd, manager.movingNote.origStart,
        movingNoteSessionSpanEnd(manager, loopLength), loopLength);
}

} // namespace

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

bool isNoteWithinMovingSpan(uint32_t noteStart, uint32_t noteEnd,
                            uint32_t spanStart, uint32_t spanEnd,
                            uint32_t loopLength) {
    if (loopLength == 0) {
        return false;
    }
    const uint32_t displaySpanEnd =
        (spanEnd >= loopLength) ? (spanEnd % loopLength) : spanEnd;
    if (noteEnd < noteStart) {
        return false;
    }
    if (displaySpanEnd >= spanStart) {
        return noteStart >= spanStart && noteEnd <= displaySpanEnd;
    }
    return noteStart >= spanStart || noteEnd <= displaySpanEnd;
}

bool notesTouchOrOverlap(uint32_t start1, uint32_t end1, uint32_t start2, uint32_t end2,
                        uint32_t loopLength) {
    if (notesOverlap(start1, end1, start2, end2, loopLength)) {
        return true;
    }
    const bool wrapped1 = (end1 < start1);
    const bool wrapped2 = (end2 < start2);
    if (!wrapped1 && !wrapped2) {
        return end1 == start2 || end2 == start1;
    }
    if (wrapped1 && !wrapped2) {
        return end1 == start2 || end2 == start1 || start2 < end1;
    }
    if (!wrapped1 && wrapped2) {
        return end1 == start2 || end2 == start1 || start1 < end2;
    }
    return end1 == start2 || end2 == start1;
}

void findOverlaps(const std::vector<NoteUtils::DisplayNote>& currentNotes,
                 uint8_t movingNotePitch,
                 uint32_t currentStart,
                 uint32_t newStart,
                 uint32_t newEnd,
                 int delta,
                 uint32_t loopLength,
                 const EditManager& manager,
                 std::vector<std::pair<NoteUtils::DisplayNote, uint32_t>>& notesToShorten,
                 std::vector<NoteUtils::DisplayNote>& notesToDelete) {
    
    // Calculate display end position for the moving note (handle wrapping)
    uint32_t displayNewEnd = newEnd % loopLength;
    
    for (const auto& note : currentNotes) {
        // Note: currentNotes is already filtered to only contain notes of the same pitch
        // that are NOT the moving note, so we can safely process all notes in this list

        // A note we already shortened in this edit session should not be deleted again when
        // the moving note slides back over it (fully-contained check would remove it).
        if (isAlreadyShortenedVictim(manager, note) ||
            hasShortenedVictimEntry(manager, note.note, note.startTick)) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Skipping overlap on tracked shortened victim: pitch=%d, start=%lu, end=%lu",
                      note.note, note.startTick, note.endTick);
            continue;
        }
        
        bool overlaps = notesOverlap(newStart, displayNewEnd, note.startTick, note.endTick, loopLength);
        if (!overlaps) continue;

        if (isInnerNoteUnderSessionSpan(manager, note.note, note.startTick, note.endTick,
                                        loopLength)) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Skipping overlap on inner note under session span: pitch=%d, start=%lu, "
                      "end=%lu (mover %lu-%lu)",
                      note.note, note.startTick, note.endTick, newStart, displayNewEnd);
            continue;
        }
        
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
                if (hasShortenedVictimEntry(manager, note.note, note.startTick)) {
                    logger.log(CAT_MIDI, LOG_DEBUG,
                              "Keeping shortened victim head (too-short trim skipped): pitch=%d, start=%lu, end=%lu",
                              note.note, note.startTick, note.endTick);
                } else {
                    notesToDelete.push_back(note);
                    logger.log(CAT_MIDI, LOG_DEBUG, "Will delete note (too short after shortening): pitch=%d, start=%lu, end=%lu->%lu, length=%lu < 49", 
                              note.note, note.startTick, note.endTick, newNoteEnd, shortenedLength);
                }
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

void applyShortenOrDelete(MidiEventVec& midiEvents,
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
        auto sessionEntry = std::find_if(
            manager.sessionShortenedVictims.begin(), manager.sessionShortenedVictims.end(),
            [&](const auto& victim) {
                return victim.note == dn.note && victim.startTick == dn.startTick;
            });
        
        if (existingEntry != manager.movingNote.deletedNotes.end()) {
            // Update the existing entry with the new shortened position
            currentOffTick = existingEntry->shortenedToTick;  // Use current position before updating
            logger.log(CAT_MIDI, LOG_DEBUG, "Updating existing shortened note entry: pitch=%d, start=%lu, old_shortened_to=%lu, new_shortened_to=%lu",
                      dn.note, dn.startTick, existingEntry->shortenedToTick, newEnd);
            logger.log(CAT_MIDI, LOG_DEBUG, "Note was already shortened, looking for note-off at current position: %lu instead of original %lu", 
                      currentOffTick, dn.endTick);
            existingEntry->shortenedToTick = newEnd;
            upsertSessionShortenedVictim(manager, *existingEntry);
        } else if (sessionEntry != manager.sessionShortenedVictims.end()) {
            currentOffTick = sessionEntry->shortenedToTick;
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Updating session shortened victim: pitch=%d, start=%lu, old_shortened_to=%lu, "
                      "new_shortened_to=%lu",
                      dn.note, dn.startTick, sessionEntry->shortenedToTick, newEnd);
            sessionEntry->shortenedToTick = newEnd;
            manager.movingNote.deletedNotes.push_back(*sessionEntry);
            upsertSessionShortenedVictim(manager, *sessionEntry);
        } else {
            // Record original for undo - currentOffTick remains as dn.endTick (original position)
            EditManager::MovingNoteIdentity::DeletedNote original = MidiEventUtils::createDeletedNote(
                dn, loopLength, true, newEnd);
            
            manager.movingNote.deletedNotes.push_back(original);
            upsertSessionShortenedVictim(manager, original);
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
                // Drop stale full-delete shadow when a shortened-victim entry still tracks this note.
                manager.movingNote.deletedNotes.erase(
                    std::remove_if(manager.movingNote.deletedNotes.begin(),
                                   manager.movingNote.deletedNotes.end(),
                                   [&](const auto& entry) {
                                       return !entry.wasShortened &&
                                              entry.note == dn.note &&
                                              entry.startTick == dn.startTick;
                                   }),
                    manager.movingNote.deletedNotes.end());

                EditManager::MovingNoteIdentity::DeletedNote deleted = MidiEventUtils::createDeletedNote(
                    dn, loopLength, false, 0);

                manager.movingNote.deletedNotes.push_back(deleted);
                upsertSessionDeletedNote(manager, deleted);
                logger.log(CAT_MIDI, LOG_DEBUG, "Stored deleted note: pitch=%d, start=%lu, end=%lu, length=%lu",
                          deleted.note, deleted.startTick, deleted.endTick, deleted.originalLength);
            } else {
                logger.log(CAT_MIDI, LOG_DEBUG, "Skipping duplicate deleted note entry: pitch=%d, start=%lu, end=%lu",
                          dn.note, dn.startTick, dn.endTick);
            }
        } else {
            logger.log(CAT_MIDI, LOG_DEBUG, "Warning: could not find specific MIDI event pair for note pitch=%d, start=%lu, end=%lu", 
                      dn.note, dn.startTick, dn.endTick);
        }
    }
}

void restoreNotes(MidiEventVec& midiEvents,
                 const std::vector<EditManager::MovingNoteIdentity::DeletedNote>& notesToRestore,
                 EditManager& manager,
                 uint32_t loopLength,
                 uint8_t channel,
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
                logger.log(CAT_MIDI, LOG_DEBUG,
                          "Recreating shortened victim (note-on missing): pitch=%d, start=%lu, end=%lu",
                          nr.note, nr.startTick, nr.endTick);
                MidiEvent onEvt;
                onEvt.tick = nr.startTick;
                onEvt.type = midi::NoteOn;
                onEvt.channel = channel;
                onEvt.data.noteData.note = nr.note;
                onEvt.data.noteData.velocity = nr.velocity;
                midiEvents.push_back(onEvt);

                MidiEvent offEvt;
                offEvt.tick = nr.endTick;
                offEvt.type = midi::NoteOff;
                offEvt.channel = channel;
                offEvt.data.noteData.note = nr.note;
                offEvt.data.noteData.velocity = 0;
                midiEvents.push_back(offEvt);
                didRestore = true;
            }
        } else {
            // Recreate a completely deleted note
            logger.log(CAT_MIDI, LOG_DEBUG, "Restoring deleted note: pitch=%d, start=%lu, end=%lu", 
                      nr.note, nr.startTick, nr.endTick);
            
            MidiEvent onEvt;
            onEvt.tick = nr.startTick;
            onEvt.type = midi::NoteOn;
            onEvt.channel = channel;
            onEvt.data.noteData.note = nr.note;
            onEvt.data.noteData.velocity = nr.velocity;
            midiEvents.push_back(onEvt);
            
            MidiEvent offEvt;
            offEvt.tick = nr.endTick;
            offEvt.type = midi::NoteOff;
            offEvt.channel = channel;
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
        if (r.wasShortened) {
            removeSessionShortenedVictim(manager, r.note, r.startTick);
        } else {
            removeSessionDeletedNote(manager, r.note, r.startTick);
        }
    }
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Restored %zu notes, %zu notes still in deleted list", 
              restored.size(), manager.movingNote.deletedNotes.size());
}

void finalReconstructAndSelect(MidiEventVec& midiEvents,
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

bool applyPitchChange(Track& track, EditManager& manager,
                      uint8_t currentNoteValue, uint8_t newNoteValue,
                      uint32_t& noteStart, uint32_t& noteEnd) {
    if (currentNoteValue == newNoteValue) {
        return true;
    }

    auto& midiEvents = track.getMidiEvents();
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return false;
    }

    const uint32_t pitchNoteOnTick = noteStart;

    // STEP 1: restore temporarily deleted notes of the current pitch.
    std::vector<EditManager::MovingNoteIdentity::DeletedNote> notesToRestore;
    for (const auto& deletedNote : manager.movingNote.deletedNotes) {
        if (deletedNote.note == currentNoteValue) {
            notesToRestore.push_back(deletedNote);
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Will restore note after pitch change: pitch=%d, start=%lu, end=%lu "
                      "(no longer conflicts with new pitch %d)",
                      deletedNote.note, deletedNote.startTick, deletedNote.endTick,
                      newNoteValue);
        }
    }

    if (!notesToRestore.empty()) {
        auto [onIndex, offIndex] = NoteUtils::buildEventIndex(midiEvents);
        restoreNotes(midiEvents, notesToRestore, manager, loopLength,
                     track.getMidiChannel(), onIndex, offIndex);
        logger.log(CAT_MIDI, LOG_DEBUG, "Restored %zu notes after pitch change",
                  notesToRestore.size());
    }

    auto notes = track.getCachedNotes();
    const bool preserveInnerNotes = manager.movingNote.active;
    const uint32_t sessionSpanStart = manager.movingNote.origStart;
    uint32_t sessionSpanEnd = manager.movingNote.origEnd;
    if (preserveInnerNotes) {
        const uint32_t displayLastEnd =
            (manager.movingNote.lastEnd >= loopLength)
                ? (manager.movingNote.lastEnd % loopLength)
                : manager.movingNote.lastEnd;
        if (displayLastEnd > sessionSpanEnd) {
            sessionSpanEnd = displayLastEnd;
        }
    }

    // Merge adjacent same-target-pitch neighbors into the edited span before overlap
    // resolution. Skip inner notes under the session span so they can reappear later.
    std::vector<NoteUtils::DisplayNote> adjacentToDelete;
    bool mergedAdjacent = true;
    while (mergedAdjacent) {
        mergedAdjacent = false;
        notes = track.getCachedNotes();
        for (const auto& note : notes) {
            if (note.note != newNoteValue) {
                continue;
            }
            if (note.startTick == noteStart && note.endTick == noteEnd) {
                continue;
            }
            if (preserveInnerNotes &&
                isNoteWithinMovingSpan(
                    note.startTick, note.endTick, sessionSpanStart, sessionSpanEnd,
                    loopLength)) {
                logger.log(CAT_MIDI, LOG_DEBUG,
                           "Skipping adjacent merge for inner note under original span: "
                           "pitch=%d, start=%lu, end=%lu",
                           note.note, note.startTick, note.endTick);
                continue;
            }
            if (note.endTick == noteStart) {
                noteStart = note.startTick;
                adjacentToDelete.push_back(note);
                mergedAdjacent = true;
                break;
            }
            if (note.startTick == noteEnd) {
                noteEnd = note.endTick;
                adjacentToDelete.push_back(note);
                mergedAdjacent = true;
                break;
            }
        }
    }
    if (!adjacentToDelete.empty()) {
        auto [onIndex, offIndex] = NoteUtils::buildEventIndex(midiEvents);
        applyShortenOrDelete(midiEvents, {}, adjacentToDelete, manager, loopLength,
                             onIndex, offIndex);
        for (auto& event : midiEvents) {
            if (event.type == midi::NoteOn && event.data.noteData.note == currentNoteValue &&
                event.tick == pitchNoteOnTick && event.data.noteData.velocity > 0) {
                event.tick = noteStart;
                break;
            }
        }
        track.getActiveLoop().markEditFlatDirty();
        track.invalidateCaches();
        notes = track.getCachedNotes();
        logger.log(CAT_MIDI, LOG_DEBUG,
                   "Merged %zu adjacent same-pitch notes into span %lu-%lu before pitch change",
                   adjacentToDelete.size(), noteStart, noteEnd);
    }

    std::set<std::pair<uint32_t, uint32_t>> restoredNotePositions;
    for (const auto& restored : notesToRestore) {
        restoredNotePositions.insert({restored.startTick, restored.endTick});
    }

    std::vector<NoteUtils::DisplayNote> otherNotesOfTargetPitch;
    for (const auto& note : notes) {
        if (note.note != newNoteValue ||
            (note.startTick == noteStart && note.endTick == noteEnd)) {
            continue;
        }
        const bool wasJustRestored =
            restoredNotePositions.count({note.startTick, note.endTick}) > 0;
        if (wasJustRestored) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Skipping recently restored note from overlap processing: "
                      "pitch=%d, start=%lu, end=%lu",
                      note.note, note.startTick, note.endTick);
            continue;
        }
        if (preserveInnerNotes &&
            isNoteWithinMovingSpan(
                note.startTick, note.endTick, sessionSpanStart, sessionSpanEnd,
                loopLength)) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Skipping pitch overlap on inner note under original span: "
                       "pitch=%d, start=%lu, end=%lu",
                       note.note, note.startTick, note.endTick);
            continue;
        }
        otherNotesOfTargetPitch.push_back(note);
    }

    if (!otherNotesOfTargetPitch.empty()) {
        logger.log(CAT_MIDI, LOG_DEBUG,
                  "Found %zu notes of target pitch %d, checking for overlaps",
                  otherNotesOfTargetPitch.size(), newNoteValue);

        std::vector<std::pair<NoteUtils::DisplayNote, uint32_t>> notesToShorten;
        std::vector<NoteUtils::DisplayNote> notesToDelete;
        for (const auto& note : otherNotesOfTargetPitch) {
            const bool overlaps = notesTouchOrOverlap(
                noteStart, noteEnd, note.startTick, note.endTick, loopLength);
            if (!overlaps) {
                continue;
            }

            bool noteCompletelyContained = false;
            if (noteEnd >= noteStart) {
                noteCompletelyContained =
                    (note.startTick >= noteStart && note.endTick <= noteEnd);
            } else {
                noteCompletelyContained =
                    (note.startTick >= noteStart || note.endTick <= noteEnd);
            }

            if (noteCompletelyContained) {
                notesToDelete.push_back(note);
                logger.log(CAT_MIDI, LOG_DEBUG,
                          "Will delete completely contained note: pitch=%d, start=%lu, end=%lu "
                          "(within current note %lu-%lu)",
                          note.note, note.startTick, note.endTick, noteStart, noteEnd);
                continue;
            }

            uint32_t newNoteEndTick;
            if (note.startTick < noteStart) {
                newNoteEndTick = (noteStart == 0) ? (loopLength - 1) : (noteStart - 1);
            } else {
                notesToDelete.push_back(note);
                logger.log(CAT_MIDI, LOG_DEBUG,
                          "Will delete overlapping note that starts after current note: "
                          "pitch=%d, start=%lu, end=%lu",
                          note.note, note.startTick, note.endTick);
                continue;
            }

            const uint32_t shortenedLength =
                calculateNoteLength(note.startTick, newNoteEndTick, loopLength);
            if (shortenedLength < 49) {
                notesToDelete.push_back(note);
                logger.log(CAT_MIDI, LOG_DEBUG,
                          "Will delete note (too short after shortening): pitch=%d, start=%lu, "
                          "end=%lu->%lu, length=%lu < 49",
                          note.note, note.startTick, note.endTick, newNoteEndTick,
                          shortenedLength);
            } else {
                notesToShorten.push_back({note, newNoteEndTick});
                logger.log(CAT_MIDI, LOG_DEBUG,
                          "Will shorten note: pitch=%d, start=%lu, end=%lu->%lu, length=%lu",
                          note.note, note.startTick, note.endTick, newNoteEndTick,
                          shortenedLength);
            }
        }

        if (!notesToShorten.empty() || !notesToDelete.empty()) {
            auto [onIndex, offIndex] = NoteUtils::buildEventIndex(midiEvents);
            applyShortenOrDelete(midiEvents, notesToShorten, notesToDelete, manager,
                                 loopLength, onIndex, offIndex);
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Applied pitch change overlaps: %zu shortened, %zu deleted",
                      notesToShorten.size(), notesToDelete.size());
        }
    }

    bool noteOnUpdated = false;
    bool noteOffUpdated = false;
    for (auto& event : midiEvents) {
        if (event.type == midi::NoteOn &&
            event.data.noteData.note == currentNoteValue &&
            event.tick == noteStart &&
            event.data.noteData.velocity > 0) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Updating note-on: pitch %d -> %d at tick %lu",
                      currentNoteValue, newNoteValue, event.tick);
            event.data.noteData.note = newNoteValue;
            noteOnUpdated = true;
            break;
        }
    }
    for (auto& event : midiEvents) {
        if (((event.type == midi::NoteOff) ||
             (event.type == midi::NoteOn && event.data.noteData.velocity == 0)) &&
            event.data.noteData.note == currentNoteValue &&
            event.tick == noteEnd) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Updating note-off: pitch %d -> %d at tick %lu",
                      currentNoteValue, newNoteValue, event.tick);
            event.data.noteData.note = newNoteValue;
            noteOffUpdated = true;
            break;
        }
    }

    if (!noteOnUpdated || !noteOffUpdated) {
        logger.log(CAT_MIDI, LOG_DEBUG,
                  "Failed to update note value: noteOn=%s noteOff=%s",
                  noteOnUpdated ? "OK" : "FAILED",
                  noteOffUpdated ? "OK" : "FAILED");
        return false;
    }

    logger.log(CAT_MIDI, LOG_DEBUG, "Note value changed successfully: %d -> %d",
              currentNoteValue, newNoteValue);
    NoteUtils::removeDuplicateNotePairsAtSpan(midiEvents, newNoteValue, noteStart, noteEnd);
    NoteUtils::ensureNoteOffsBeforeNoteOnsAtTick(midiEvents, newNoteValue, noteStart);

    manager.movingNote.note = newNoteValue;
    manager.movingNote.lastStart = noteStart;
    manager.movingNote.lastEnd = noteEnd;
    manager.movingNote.active = true;
    if (preserveInnerNotes) {
        const uint32_t currentSessionLength = calculateNoteLength(
            manager.movingNote.origStart, manager.movingNote.origEnd, loopLength);
        const uint32_t candidateSessionLength = calculateNoteLength(
            manager.movingNote.origStart, noteEnd, loopLength);
        if (candidateSessionLength > currentSessionLength) {
            manager.movingNote.origEnd =
                (noteEnd >= loopLength) ? (noteEnd % loopLength) : noteEnd;
        }
    }
    track.getActiveLoop().markEditFlatDirty();
    track.invalidateCaches();
    return true;
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
    
    // Store original position for phantom note detection - use the stable original position
    // from when movement first began, not the current position
    uint32_t originalStart = manager.movingNote.origStart;
    
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
    findOverlaps(otherNotesOfSamePitch, movingNotePitch, currentStart, newStart, newEnd, delta, loopLength,
                manager, notesToShorten, notesToDelete);

    // Cross-pitch overlap does not shorten/delete — only same-pitch rules above apply.
    
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
        SC_DNTE(movingNotePitch, newStart, newStart,
                newEnd >= newStart ? newEnd - newStart : 0u,
                manager.getSelectedNoteIdx());
    } else {
        logger.log(CAT_MIDI, LOG_DEBUG, "Warning: could not find MIDI events for moving note pitch=%u at start=%lu end=%lu", 
                  movingNotePitch, currentStart, currentEnd);
    }
    
    // CRITICAL: Rebuild event index AFTER moving the note but BEFORE applying shortening
    // This ensures the index reflects the current state and doesn't accidentally modify the wrong events
    auto [onIndex, offIndex] = NoteUtils::buildEventIndex(midiEvents);
    
    // STEP 4: Check if we should restore any previously deleted/shortened notes
    std::vector<EditManager::MovingNoteIdentity::DeletedNote> overlapVictims;
    overlapVictims.reserve(manager.movingNote.deletedNotes.size() +
                           manager.sessionDeletedNotes.size());
    for (const auto& deletedNote : manager.movingNote.deletedNotes) {
        appendUniqueRestoreCandidate(overlapVictims, deletedNote);
    }
    for (const auto& deletedNote : manager.sessionDeletedNotes) {
        appendUniqueRestoreCandidate(overlapVictims, deletedNote);
    }

    for (const auto& deletedNote : overlapVictims) {
        if (deletedNote.note != movingNotePitch && deletedNote.wasShortened) {
            const uint32_t truncatedStart = deletedNote.shortenedToTick + 1;
            const bool overlapsTruncatedZone = notesOverlap(
                newStart, displayNewEnd, truncatedStart, deletedNote.endTick, loopLength);
            if (!overlapsTruncatedZone) {
                appendUniqueRestoreCandidate(notesToRestore, deletedNote);
                logger.log(CAT_MIDI, LOG_DEBUG,
                          "Will restore cross-pitch shortened note: pitch=%d, start=%lu, end=%lu "
                          "(mover %lu-%lu clear of truncated %lu-%lu)",
                          deletedNote.note, deletedNote.startTick, deletedNote.endTick,
                          newStart, displayNewEnd, truncatedStart, deletedNote.endTick);
            } else {
                logger.log(CAT_MIDI, LOG_DEBUG,
                          "Cannot restore cross-pitch shortened note: pitch=%d, start=%lu "
                          "(mover still in truncated zone %lu-%lu)",
                          deletedNote.note, deletedNote.startTick, truncatedStart,
                          deletedNote.endTick);
            }
            continue;
        }

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
            
            // Check if the deleted note overlaps with the new position (use current shortened span)
            const uint32_t victimEnd = deletedNoteEffectiveEnd(deletedNote);
            const bool hasOverlap = notesOverlap(newStart, displayNewEnd,
                                                  deletedNote.startTick, victimEnd, loopLength);
            
            if (!hasOverlap) {
                appendUniqueRestoreCandidate(notesToRestore, deletedNote);
                logger.log(CAT_MIDI, LOG_DEBUG, "Will restore note: pitch=%d, start=%lu, end=%lu (victimEnd=%lu, no overlap with %lu-%lu)", 
                          deletedNote.note, deletedNote.startTick, deletedNote.endTick, victimEnd, newStart, displayNewEnd);
            } else if (isInnerNoteUnderSessionSpan(
                           manager, deletedNote.note, deletedNote.startTick, victimEnd,
                           loopLength)) {
                appendUniqueRestoreCandidate(notesToRestore, deletedNote);
                logger.log(CAT_MIDI, LOG_DEBUG,
                          "Will restore inner note under session span: pitch=%d, start=%lu, end=%lu "
                          "(coexists inside mover %lu-%lu)",
                          deletedNote.note, deletedNote.startTick, deletedNote.endTick,
                          newStart, displayNewEnd);
            } else {
                logger.log(CAT_MIDI, LOG_DEBUG, "Cannot restore note: pitch=%d, start=%lu, end=%lu (victimEnd=%lu still overlaps %lu-%lu)", 
                          deletedNote.note, deletedNote.startTick, deletedNote.endTick, victimEnd, newStart, displayNewEnd);
            }
        }
    }
    
    logger.log(CAT_MIDI, LOG_DEBUG,
              "Found %zu notes to restore, %zu active deleted notes, %zu session deleted, "
              "%zu session shortened",
              notesToRestore.size(), manager.movingNote.deletedNotes.size(),
              manager.sessionDeletedNotes.size(), manager.sessionShortenedVictims.size());
    
    // Apply shorten/delete using shared index
    applyShortenOrDelete(midiEvents, notesToShorten, notesToDelete, manager, loopLength, onIndex, offIndex);
    
    // Restore notes that should be restored based on movement, reusing index
    restoreNotes(midiEvents, notesToRestore, manager, loopLength, track.getMidiChannel(), onIndex, offIndex);
    
    // Helper to finalize reconstruction and selection after movement
    finalReconstructAndSelect(midiEvents, manager, movingNotePitch, newStart, newEnd, loopLength);

    Loop& loop = track.getActiveLoop();
    loop.markEditFlatDirty();
    track.invalidateCaches();
}

// Find the corresponding note-off event for a given note-on event using LIFO pairing logic
MidiEvent* findCorrespondingNoteOff(MidiEventVec& midiEvents, MidiEvent* noteOnEvent, uint8_t pitch, std::uint32_t startTick, std::uint32_t endTick) {
    // Use LIFO pairing logic similar to NoteUtils::reconstructNotes
    // We need to simulate the pairing process to find which note-off belongs to our note-on
    
    std::vector<MidiEvent*> activeNoteOnStack;
    
    for (auto& evt : midiEvents) {
        bool isNoteOn = (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 && evt.data.noteData.note == pitch);
        bool isNoteOff = ((evt.type == midi::NoteOff || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) && evt.data.noteData.note == pitch);
        
        if (isNoteOn) {
            activeNoteOnStack.push_back(&evt);
        } else if (isNoteOff) {
            if (!activeNoteOnStack.empty()) {
                MidiEvent* correspondingNoteOn = activeNoteOnStack.back();
                activeNoteOnStack.pop_back();
                
                // Check if this is the note-off for our target note-on
                if (correspondingNoteOn == noteOnEvent) {
                    logger.log(CAT_MIDI, LOG_DEBUG, "Found corresponding note-off: pitch=%d, noteOn@%lu -> noteOff@%lu", 
                              pitch, correspondingNoteOn->tick, evt.tick);
                    return &evt;
                }
            }
        }
    }
    
    // If we reach here, the note-on didn't have a corresponding note-off (shouldn't happen in well-formed MIDI)
    logger.log(CAT_MIDI, LOG_DEBUG, "No corresponding note-off found for pitch=%d, start=%lu", pitch, startTick);
    return nullptr;
}

// Extend shortened notes dynamically
void extendShortenedNotes(MidiEventVec& midiEvents,
                         const std::vector<std::pair<EditManager::MovingNoteIdentity::DeletedNote, std::uint32_t>>& notesToExtend,
                         EditManager& manager,
                         std::uint32_t loopLength) {
    logger.log(CAT_MIDI, LOG_DEBUG, "=== EXTENDING SHORTENED NOTES ===");
    logger.log(CAT_MIDI, LOG_DEBUG, "Total notes to extend: %zu", notesToExtend.size());
    
    for (const auto& [noteToExtend, newEndTick] : notesToExtend) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Extending shortened note: pitch=%d, start=%lu, from %lu to %lu", 
                  noteToExtend.note, noteToExtend.startTick, noteToExtend.shortenedToTick, newEndTick);
        
        // Find the note-off event at its current shortened position
        MidiEvent* noteOffEvent = nullptr;
        for (auto& event : midiEvents) {
            if ((event.type == midi::NoteOff || (event.type == midi::NoteOn && event.data.noteData.velocity == 0)) &&
                event.data.noteData.note == noteToExtend.note && 
                event.tick > noteToExtend.startTick) {
                // Take the first note-off we find for this pitch after the note-on
                noteOffEvent = &event;
                break;
            }
        }
        
        if (noteOffEvent) {
            uint32_t oldTick = noteOffEvent->tick;
            noteOffEvent->tick = newEndTick;
            
            // Update the tracking in the deleted notes list
            for (auto& deletedNote : manager.movingNote.deletedNotes) {
                if (deletedNote.note == noteToExtend.note && 
                    deletedNote.startTick == noteToExtend.startTick && 
                    deletedNote.wasShortened) {
                    deletedNote.shortenedToTick = newEndTick;
                    logger.log(CAT_MIDI, LOG_DEBUG, "Updated tracking: shortened note now ends at %lu", newEndTick);
                    break;
                }
            }
            
            logger.log(CAT_MIDI, LOG_DEBUG, "Extended note-off event: pitch=%d, from tick=%lu to tick=%lu", 
                      noteToExtend.note, oldTick, newEndTick);
        } else {
            logger.log(CAT_MIDI, LOG_DEBUG, "Warning: Could not find note-off event to extend: pitch=%d, start=%lu", 
                      noteToExtend.note, noteToExtend.startTick);
        }
    }
}

} // namespace NoteMovementUtils 