//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0


#include "Logger.h"
#include "Globals.h"
#include "NoteEditFocus.h"
#include "Utils/MidiEventUtils.h"
#include "Utils/NoteMovementUtils.h"
#include "Utils/DebugSessionCapture.h"
#include <algorithm>
#include <map>
#include <set>

namespace NoteMovementUtils {

namespace {

using DeletedNote = EditManager::MovingNoteIdentity::DeletedNote;

NoteEditFocus& editFocus(EditManager& manager) {
    return manager.getNoteEditSession().focus;
}

const NoteEditFocus& editFocus(const EditManager& manager) {
    return manager.getNoteEditSession().focus;
}

bool isAlreadyShortenedOverlap(const EditManager& manager,
                               const NoteUtils::DisplayNote& note) {
    const NoteEditFocus& focus = editFocus(manager);
    for (const auto& [ref, entry] : focus.overlapNotes) {
        (void)ref;
        if (entry.state == OverlapNoteStoreState::Shortened &&
            entry.baseline.pitch == note.note &&
            entry.baseline.startTick == note.startTick &&
            entry.shortenedEndTick == note.endTick) {
            return true;
        }
    }
    return false;
}

bool hasShortenedOverlapEntry(const EditManager& manager, uint8_t pitch,
                             uint32_t startTick) {
    const NoteEditFocus& focus = editFocus(manager);
    for (const auto& [ref, entry] : focus.overlapNotes) {
        (void)ref;
        if (entry.state == OverlapNoteStoreState::Shortened &&
            entry.baseline.pitch == pitch && entry.baseline.startTick == startTick) {
            return true;
        }
    }
    return false;
}

OverlapNote& upsertOverlapNote(EditManager& manager, uint8_t channel,
                               const NoteUtils::DisplayNote& dn) {
    NoteEditFocus& focus = editFocus(manager);
    const NoteRef ref =
        findBaselineRefForNote(focus, channel, dn.note, dn.startTick, dn.endTick);
    OverlapNote& entry = focus.overlapNotes[ref];
    entry.ref = ref;
    entry.baseline = baselineForDisplayNote(focus, channel, dn);
    return entry;
}

void markOverlapHidden(EditManager& manager, uint8_t channel,
                       const NoteUtils::DisplayNote& dn) {
    OverlapNote& entry = upsertOverlapNote(manager, channel, dn);
    entry.state = OverlapNoteStoreState::Hidden;
    entry.shortenedEndTick = 0;
    entry.innerUnderFootprint = false;
}

void markOverlapShortened(EditManager& manager, uint8_t channel,
                          const NoteUtils::DisplayNote& dn, uint32_t shortenedEnd) {
    OverlapNote& entry = upsertOverlapNote(manager, channel, dn);
    entry.state = OverlapNoteStoreState::Shortened;
    entry.shortenedEndTick = shortenedEnd;
    entry.innerUnderFootprint = false;
}

void removeOverlapEntry(EditManager& manager, const NoteRef& ref) {
    editFocus(manager).overlapNotes.erase(ref);
}

DeletedNote overlapNoteToDeletedNote(const OverlapNote& entry) {
    DeletedNote dn;
    dn.note = entry.baseline.pitch;
    dn.velocity = entry.baseline.velocity;
    dn.startTick = entry.baseline.startTick;
    dn.endTick = entry.baseline.endTick;
    dn.originalLength = entry.baseline.endTick >= entry.baseline.startTick
                            ? entry.baseline.endTick - entry.baseline.startTick
                            : 0;
    dn.wasShortened = entry.state == OverlapNoteStoreState::Shortened;
    dn.shortenedToTick = entry.shortenedEndTick;
    return dn;
}

void appendUniqueRestoreCandidate(std::vector<DeletedNote>& notesToRestore,
                                  const DeletedNote& candidate) {
    for (const auto& queued : notesToRestore) {
        if (queued.note == candidate.note && queued.startTick == candidate.startTick) {
            return;
        }
    }
    notesToRestore.push_back(candidate);
}

uint32_t deletedNoteEffectiveEnd(const DeletedNote& deletedNote) {
    return deletedNote.wasShortened ? deletedNote.shortenedToTick : deletedNote.endTick;
}

bool isInnerNoteUnderFootprint(const EditManager& manager, uint8_t notePitch,
                               uint32_t noteStart, uint32_t noteEnd,
                               uint32_t loopLength) {
    return isInnerUnderOverlapFootprint(editFocus(manager), notePitch, noteStart, noteEnd,
                                        loopLength);
}

void restoreOverlapNotesNoLongerOverlapping(MidiEventVec& midiEvents, EditManager& manager,
                                            uint8_t channel, uint32_t moverStart,
                                            uint32_t moverDisplayEnd, uint8_t movingPitch,
                                            uint32_t originalStart, uint32_t loopLength) {
    std::vector<DeletedNote> notesToRestore;
    NoteEditFocus& focus = editFocus(manager);

    for (auto& [ref, entry] : focus.overlapNotes) {
        if (entry.state == OverlapNoteStoreState::Visible) {
            continue;
        }
        const DeletedNote dn = overlapNoteToDeletedNote(entry);

        if (dn.note != movingPitch && dn.wasShortened) {
            const uint32_t truncatedStart = dn.shortenedToTick + 1;
            const bool overlapsTruncatedZone = notesOverlap(
                moverStart, moverDisplayEnd, truncatedStart, dn.endTick, loopLength);
            if (!overlapsTruncatedZone) {
                appendUniqueRestoreCandidate(notesToRestore, dn);
            }
            continue;
        }

        if (dn.note != movingPitch) {
            continue;
        }

        if (dn.startTick == originalStart) {
            continue;
        }

        const uint32_t deletedNoteLength =
            calculateNoteLength(dn.startTick, dn.endTick, loopLength);
        if (deletedNoteLength == 0 || deletedNoteLength >= loopLength) {
            continue;
        }

        const uint32_t victimEnd = deletedNoteEffectiveEnd(dn);
        const bool hasOverlap =
            notesOverlap(moverStart, moverDisplayEnd, dn.startTick, victimEnd, loopLength);
        if (!hasOverlap) {
            appendUniqueRestoreCandidate(notesToRestore, dn);
        } else if (isInnerNoteUnderFootprint(manager, dn.note, dn.startTick, victimEnd,
                                             loopLength)) {
            appendUniqueRestoreCandidate(notesToRestore, dn);
            entry.state = OverlapNoteStoreState::Visible;
            entry.innerUnderFootprint = true;
        }
    }

    if (notesToRestore.empty()) {
        return;
    }

    auto [onIndex, offIndex] = NoteUtils::buildEventIndex(midiEvents);
    restoreNotes(midiEvents, notesToRestore, manager, loopLength, channel, onIndex, offIndex);
    logger.log(CAT_MIDI, LOG_DEBUG, "Restored %zu overlap notes no longer overlapping mover",
              notesToRestore.size());
}

void restoreOverlapNotesForPitchLaneClear(MidiEventVec& midiEvents, EditManager& manager,
                                          uint8_t channel, uint8_t clearedPitch,
                                          uint32_t loopLength) {
    std::vector<DeletedNote> notesToRestore;
    for (const auto& [ref, entry] : editFocus(manager).overlapNotes) {
        (void)ref;
        if (entry.state == OverlapNoteStoreState::Visible) {
            continue;
        }
        if (entry.baseline.pitch != clearedPitch) {
            continue;
        }
        appendUniqueRestoreCandidate(notesToRestore, overlapNoteToDeletedNote(entry));
    }
    if (notesToRestore.empty()) {
        return;
    }
    auto [onIndex, offIndex] = NoteUtils::buildEventIndex(midiEvents);
    restoreNotes(midiEvents, notesToRestore, manager, loopLength, channel, onIndex, offIndex);
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
                 std::vector<NoteUtils::DisplayNote>& notesToDelete,
                 bool allowSharedEndCoexistence) {
    
    // Calculate display end position for the moving note (handle wrapping)
    uint32_t displayNewEnd = newEnd % loopLength;
    
    for (const auto& note : currentNotes) {
        // Note: currentNotes is already filtered to only contain notes of the same pitch
        // that are NOT the moving note, so we can safely process all notes in this list

        // A note we already shortened in this edit session should not be deleted again when
        // the moving note slides back over it (fully-contained check would remove it).
        if (isAlreadyShortenedOverlap(manager, note) ||
            hasShortenedOverlapEntry(manager, note.note, note.startTick)) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Skipping overlap on tracked shortened victim: pitch=%d, start=%lu, end=%lu",
                      note.note, note.startTick, note.endTick);
            continue;
        }
        
        bool overlaps = notesOverlap(newStart, displayNewEnd, note.startTick, note.endTick, loopLength);
        if (!overlaps) continue;

        if (isInnerNoteUnderFootprint(manager, note.note, note.startTick, note.endTick,
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
            if (allowSharedEndCoexistence && note.startTick > newStart &&
                note.endTick == displayNewEnd) {
                logger.log(CAT_MIDI, LOG_DEBUG,
                          "Skipping delete for same-pitch shared release: pitch=%d, start=%lu, "
                          "end=%lu (mover %lu-%lu)",
                          note.note, note.startTick, note.endTick, newStart, displayNewEnd);
                continue;
            }
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
                if (hasShortenedOverlapEntry(manager, note.note, note.startTick)) {
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

MidiEvent* findCorrespondingNoteOff(MidiEventVec& midiEvents, MidiEvent* noteOnEvent,
                                    uint8_t pitch, std::uint32_t startTick,
                                    std::uint32_t endTick) {
    std::vector<MidiEvent*> activeNoteOnStack;

    for (auto& evt : midiEvents) {
        bool isNoteOn = (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 &&
                         evt.data.noteData.note == pitch);
        bool isNoteOff = ((evt.type == midi::NoteOff ||
                           (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) &&
                          evt.data.noteData.note == pitch);

        if (isNoteOn) {
            activeNoteOnStack.push_back(&evt);
        } else if (isNoteOff) {
            if (!activeNoteOnStack.empty()) {
                MidiEvent* correspondingNoteOn = activeNoteOnStack.back();
                activeNoteOnStack.pop_back();

                if (correspondingNoteOn == noteOnEvent) {
                    logger.log(CAT_MIDI, LOG_DEBUG,
                              "Found corresponding note-off: pitch=%d, noteOn@%lu -> noteOff@%lu",
                              pitch, correspondingNoteOn->tick, evt.tick);
                    return &evt;
                }
            }
        }
    }

    logger.log(CAT_MIDI, LOG_DEBUG, "No corresponding note-off found for pitch=%d, start=%lu",
              pitch, startTick);
    return nullptr;
}

MidiEvent* findNoteOffPairedAt(MidiEventVec& midiEvents, uint8_t pitch, uint32_t startTick,
                               uint32_t endTick) {
    std::vector<MidiEvent*> activeNoteOnStack;
    for (auto& evt : midiEvents) {
        const bool isNoteOn = (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 &&
                             evt.data.noteData.note == pitch);
        const bool isNoteOff =
            ((evt.type == midi::NoteOff ||
              (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) &&
             evt.data.noteData.note == pitch);

        if (isNoteOn) {
            activeNoteOnStack.push_back(&evt);
        } else if (isNoteOff) {
            if (activeNoteOnStack.empty()) {
                continue;
            }
            MidiEvent* correspondingNoteOn = activeNoteOnStack.back();
            activeNoteOnStack.pop_back();
            if (correspondingNoteOn->tick == startTick && evt.tick == endTick) {
                logger.log(CAT_MIDI, LOG_DEBUG,
                          "Found paired note-off: pitch=%d, start=%lu, end=%lu",
                          pitch, startTick, endTick);
                return &evt;
            }
        }
    }

    logger.log(CAT_MIDI, LOG_DEBUG,
              "No paired note-off for pitch=%d, start=%lu, end=%lu",
              pitch, startTick, endTick);
    return nullptr;
}

void applyShortenOrDelete(MidiEventVec& midiEvents,
                         const std::vector<std::pair<NoteUtils::DisplayNote, uint32_t>>& notesToShorten,
                         const std::vector<NoteUtils::DisplayNote>& notesToDelete,
                         EditManager& manager,
                         uint32_t loopLength,
                         NoteUtils::EventIndexMap& onIndex,
                         NoteUtils::EventIndexMap& offIndex) {
    const uint8_t channel = editFocus(manager).moving.channel;

    for (const auto& [dn, newEnd] : notesToShorten) {
        uint32_t currentOffTick = dn.endTick;
        const NoteRef ref =
            findBaselineRefForNote(editFocus(manager), channel, dn.note, dn.startTick, dn.endTick);
        OverlapNote* existing = findOverlapNoteEntry(editFocus(manager), ref);
        if (existing != nullptr && existing->state == OverlapNoteStoreState::Shortened) {
            currentOffTick = existing->shortenedEndTick;
            existing->shortenedEndTick = newEnd;
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Updating shortened overlap note: pitch=%d, start=%lu, old_end=%lu, new_end=%lu",
                      dn.note, dn.startTick, currentOffTick, newEnd);
        } else {
            markOverlapShortened(manager, channel, dn, newEnd);
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Stored shortened overlap note: pitch=%d, start=%lu, original_end=%lu, "
                      "shortened_to=%lu",
                      dn.note, dn.startTick, dn.endTick, newEnd);
        }

        auto offKey = (NoteUtils::Key(dn.note) << 32) | currentOffTick;
        auto itOff = offIndex.find(offKey);
        if (itOff != offIndex.end()) {
            size_t idx = itOff->second;
            midiEvents[idx].tick = newEnd;
            offIndex.erase(itOff);
            auto newKey = (NoteUtils::Key(dn.note) << 32) | newEnd;
            offIndex[newKey] = idx;
        } else {
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Warning: Could not find note-off event for shortening: pitch=%d, tick=%lu",
                      dn.note, currentOffTick);
        }
    }

    for (const auto& dn : notesToDelete) {
        MidiEvent* noteOnToDelete = nullptr;
        for (auto& evt : midiEvents) {
            if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 &&
                evt.data.noteData.note == dn.note && evt.tick == dn.startTick) {
                noteOnToDelete = &evt;
                break;
            }
        }

        MidiEvent* noteOffToDelete = nullptr;
        if (noteOnToDelete) {
            noteOffToDelete = findCorrespondingNoteOff(
                midiEvents, noteOnToDelete, dn.note, dn.startTick, dn.endTick);
            if (noteOffToDelete) {
                const uint32_t pairedLength = calculateNoteLength(
                    noteOnToDelete->tick, noteOffToDelete->tick, loopLength);
                const uint32_t expectedLength = calculateNoteLength(
                    dn.startTick, dn.endTick, loopLength);
                if (pairedLength != expectedLength) {
                    logger.log(CAT_MIDI, LOG_DEBUG,
                              "LIFO pair length mismatch for delete: pitch=%d, start=%lu, "
                              "paired_end=%lu, expected_end=%lu",
                              dn.note, dn.startTick, noteOffToDelete->tick, dn.endTick);
                    noteOffToDelete = nullptr;
                }
            }
        }

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

            markOverlapHidden(manager, channel, dn);
            logger.log(CAT_MIDI, LOG_DEBUG, "Stored hidden overlap note: pitch=%d, start=%lu, end=%lu",
                      dn.note, dn.startTick, dn.endTick);
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
        const uint8_t channel = editFocus(manager).moving.channel;
        const NoteRef ref =
            findBaselineRefForNote(editFocus(manager), channel, nr.note, nr.startTick, nr.endTick);

        if (nr.wasShortened) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Restoring shortened overlap note: pitch=%d, start=%lu, was shortened to %lu, "
                      "restoring to %lu",
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
    
    for (const auto& r : restored) {
        const uint8_t channel = editFocus(manager).moving.channel;
        const NoteRef ref = findBaselineRefForNote(editFocus(manager), channel, r.note,
                                                   r.startTick, r.endTick);
        removeOverlapEntry(manager, ref);
    }

    logger.log(CAT_MIDI, LOG_DEBUG, "Restored %zu overlap notes, %zu still tracked",
              restored.size(), editFocus(manager).overlapNotes.size());
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

    // Use the note-edit session store (when active) so live pitch edits share the same
    // source-of-truth buffer as move/length edits; getMidiEvents() re-materializes from
    // takes+edits and would diverge from the live session flat.
    auto& midiEvents = track.editAwareMidiEvents();
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return false;
    }

    const uint32_t pitchNoteOnTick = noteStart;

    restoreOverlapNotesForPitchLaneClear(midiEvents, manager, track.getMidiChannel(),
                                         currentNoteValue, loopLength);
    track.invalidateCaches();

    auto notes = track.getCachedNotes();
    const NoteEditFocus& focus = editFocus(manager);
    const bool preserveInnerNotes = manager.movingNote.active && focus.active;
    const uint32_t footprintStart = focus.overlapFootprint.start;
    const uint32_t footprintEnd = overlapFootprintDisplayEnd(focus, loopLength);

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
                isNoteWithinMovingSpan(note.startTick, note.endTick, footprintStart,
                                       footprintEnd, loopLength)) {
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
            isNoteWithinMovingSpan(note.startTick, note.endTick, footprintStart,
                                   footprintEnd, loopLength)) {
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
        findOverlaps(otherNotesOfTargetPitch, newNoteValue, noteStart, noteStart, noteEnd, 1,
                     loopLength, manager, notesToShorten, notesToDelete);

        if (!notesToShorten.empty() || !notesToDelete.empty()) {
            auto [onIndex, offIndex] = NoteUtils::buildEventIndex(midiEvents);
            applyShortenOrDelete(midiEvents, notesToShorten, notesToDelete, manager,
                                 loopLength, onIndex, offIndex);
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Applied pitch change overlaps: %zu shortened, %zu deleted",
                      notesToShorten.size(), notesToDelete.size());
        }
    }

    // Resolve the note-on, then its STRUCTURALLY PAIRED note-off (not an independent
    // tick scan): two same-pitch notes can share a start or end tick when overlapping,
    // and an independent scan would repitch one event of a neighbor and orphan a note.
    bool noteOnUpdated = false;
    bool noteOffUpdated = false;
    MidiEvent* noteOnEvent = nullptr;
    for (auto& event : midiEvents) {
        if (event.type == midi::NoteOn &&
            event.data.noteData.note == currentNoteValue &&
            event.tick == noteStart &&
            event.data.noteData.velocity > 0) {
            noteOnEvent = &event;
            break;
        }
    }
    if (noteOnEvent) {
        MidiEvent* noteOffEvent = findCorrespondingNoteOff(
            midiEvents, noteOnEvent, currentNoteValue, noteStart, noteEnd);
        if (noteOffEvent) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Updating note-on: pitch %d -> %d at tick %lu",
                      currentNoteValue, newNoteValue, noteOnEvent->tick);
            noteOnEvent->data.noteData.note = newNoteValue;
            noteOnUpdated = true;
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Updating note-off: pitch %d -> %d at tick %lu",
                      currentNoteValue, newNoteValue, noteOffEvent->tick);
            noteOffEvent->data.noteData.note = newNoteValue;
            noteOffUpdated = true;
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
    noteEditFocusApplyPitch(manager.getNoteEditSession().focus, newNoteValue, noteStart, noteEnd,
                            loopLength);
    track.getActiveLoop().markEditFlatDirty();
    track.invalidateCaches();
    return true;
}

void moveNoteWithOverlapHandling(Track& track, EditManager& manager, 
                                const NoteUtils::DisplayNote& currentNote, 
                                uint32_t targetTick, int delta) {
    // Session store when a note-edit session is active (matches move/length live paths).
    auto& midiEvents = track.editAwareMidiEvents();
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
    
    // STEP 2: Restore overlap notes no longer covered by the target mover span.
    restoreOverlapNotesNoLongerOverlapping(midiEvents, manager, track.getMidiChannel(), newStart,
                                          displayNewEnd, movingNotePitch, originalStart,
                                          loopLength);
    track.invalidateCaches();
    allNotes = NoteUtils::reconstructNotes(midiEvents, loopLength);
    otherNotesOfSamePitch.clear();
    for (const auto& note : allNotes) {
        if (note.note == movingNotePitch &&
            !(note.startTick == currentStart && note.endTick == displayCurrentEnd)) {
            otherNotesOfSamePitch.push_back(note);
        }
    }

    // STEP 3: Detect and categorize overlaps using the filtered list.
    std::vector<NoteUtils::DisplayNote> notesToDelete;
    std::vector<std::pair<NoteUtils::DisplayNote, uint32_t>> notesToShorten;
    findOverlaps(otherNotesOfSamePitch, movingNotePitch, currentStart, newStart, newEnd, delta,
                loopLength, manager, notesToShorten, notesToDelete);

    auto [onIndex, offIndex] = NoteUtils::buildEventIndex(midiEvents);

    // STEP 4: Apply new shorten/delete impacts while mover is still at current position.
    applyShortenOrDelete(midiEvents, notesToShorten, notesToDelete, manager, loopLength, onIndex,
                         offIndex);

    // STEP 5: Move the mover using LIFO pairing (not first note-off at currentEnd tick).
    MidiEvent* noteOnEvent = nullptr;
    for (auto& evt : midiEvents) {
        if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 &&
            evt.data.noteData.note == movingNotePitch && evt.tick == currentStart) {
            noteOnEvent = &evt;
            break;
        }
    }
    MidiEvent* noteOffEvent = nullptr;
    if (noteOnEvent) {
        noteOffEvent = findCorrespondingNoteOff(
            midiEvents, noteOnEvent, movingNotePitch, currentStart, displayCurrentEnd);
    }

    if (!noteOnEvent || !noteOffEvent) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Moving note MIDI events not found - checking if it was accidentally deleted");

        for (auto it = manager.movingNote.deletedNotes.begin(); it != manager.movingNote.deletedNotes.end(); ++it) {
            if (it->note == movingNotePitch && it->startTick == currentStart &&
                (it->endTick == currentEnd || (!it->wasShortened && it->endTick == currentEnd))) {

                logger.log(CAT_MIDI, LOG_DEBUG, "Found accidentally deleted moving note - restoring it: pitch=%d, start=%lu, end=%lu", 
                          it->note, it->startTick, it->endTick);

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

                manager.movingNote.deletedNotes.erase(it);

                for (auto& evt : midiEvents) {
                    if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 &&
                        evt.data.noteData.note == movingNotePitch && evt.tick == currentStart) {
                        noteOnEvent = &evt;
                        break;
                    }
                }
                if (noteOnEvent) {
                    noteOffEvent = findCorrespondingNoteOff(
                        midiEvents, noteOnEvent, movingNotePitch, currentStart, displayCurrentEnd);
                }
                break;
            }
        }
    }

    if (noteOnEvent && noteOffEvent) {
        uint8_t actualCurrentPitch = noteOnEvent->data.noteData.note;
        if (actualCurrentPitch != manager.movingNote.note) {
            logger.log(CAT_MIDI, LOG_DEBUG, "Note pitch changed during move: %d -> %d, updating moving note identity", 
                      manager.movingNote.note, actualCurrentPitch);

            uint8_t oldPitch = manager.movingNote.note;
            manager.movingNote.note = actualCurrentPitch;
            movingNotePitch = actualCurrentPitch;

            for (auto& deletedNote : manager.movingNote.deletedNotes) {
                if (deletedNote.note == oldPitch) {
                    logger.log(CAT_MIDI, LOG_DEBUG, "Reindexing deleted note pitch: %d -> %d at start=%lu", 
                              deletedNote.note, actualCurrentPitch, deletedNote.startTick);
                    deletedNote.note = actualCurrentPitch;
                }
            }
        }

        noteOnEvent->tick = newStart;
        noteOffEvent->tick = newEnd;
        manager.movingNote.lastStart = newStart;
        manager.movingNote.lastEnd = newEnd;
        noteEditFocusApplyMoveEnd(manager.getNoteEditSession().focus, newStart, newEnd);
        logger.log(CAT_MIDI, LOG_DEBUG, "Moved note events: pitch=%u start->%lu end->%lu", movingNotePitch, newStart, newEnd);
        SC_DNTE(movingNotePitch, newStart, newStart,
                newEnd >= newStart ? newEnd - newStart : 0u,
                manager.getSelectedNoteIdx());
    } else {
        logger.log(CAT_MIDI, LOG_DEBUG, "Warning: could not find MIDI events for moving note pitch=%u at start=%lu end=%lu", 
                  movingNotePitch, currentStart, currentEnd);
    }

    finalReconstructAndSelect(midiEvents, manager, movingNotePitch, newStart, newEnd, loopLength);

    Loop& loop = track.getActiveLoop();
    loop.markEditFlatDirty();
    track.invalidateCaches();
}

void changeLengthWithOverlapHandling(Track& track, EditManager& manager,
                                     const NoteUtils::DisplayNote& currentNote,
                                     uint32_t targetEndTick) {
    auto& midiEvents = track.editAwareMidiEvents();
    uint32_t loopLength = track.getLoopLength();

    if (loopLength == 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop length is 0, cannot change note length");
        return;
    }

    uint8_t notePitch = currentNote.note;
    uint32_t noteStart = currentNote.startTick;
    uint32_t currentEnd = currentNote.endTick;
    uint32_t displayCurrentEnd =
        (currentEnd >= loopLength) ? (currentEnd % loopLength) : currentEnd;

    if (targetEndTick == currentEnd) {
        manager.setBracketTick(targetEndTick % loopLength);
        return;
    }

    const int delta = (targetEndTick > currentEnd) ? 1 : -1;

    if (!manager.movingNote.active) {
        const NoteEditFocus& focus = manager.getNoteEditSession().focus;
        if (focus.active) {
            manager.movingNote.note = focus.commitBaseline.pitch;
            manager.movingNote.origPitch = focus.commitBaseline.pitch;
            manager.movingNote.origStart = focus.commitBaseline.startTick;
            manager.movingNote.origEnd = focus.commitBaseline.endTick;
            manager.movingNote.lastStart = focus.last.startTick;
            manager.movingNote.lastEnd = focus.last.endTick;
        } else {
            manager.movingNote.note = currentNote.note;
            manager.movingNote.origPitch = currentNote.note;
            manager.movingNote.origStart = currentNote.startTick;
            manager.movingNote.origEnd = currentNote.endTick;
            manager.movingNote.lastStart = currentNote.startTick;
            manager.movingNote.lastEnd = currentNote.endTick;
        }
        manager.movingNote.active = true;
        manager.movingNote.movementDirection = 0;
        manager.movingNote.deletedNotes.clear();
        logger.log(CAT_MIDI, LOG_DEBUG,
                  "Initialized moving note for length edit: pitch=%d, start=%lu, end=%lu",
                  manager.movingNote.note, manager.movingNote.lastStart,
                  manager.movingNote.lastEnd);
    } else {
        notePitch = manager.movingNote.note;
        noteStart = manager.movingNote.lastStart;
        currentEnd = manager.movingNote.lastEnd;
        displayCurrentEnd =
            (currentEnd >= loopLength) ? (currentEnd % loopLength) : currentEnd;
    }

    const uint32_t newStart = noteStart;
    const uint32_t newEnd = targetEndTick;
    const uint32_t displayNewEnd = newEnd % loopLength;

    logger.log(CAT_MIDI, LOG_DEBUG,
              "Length change with overlap: pitch=%d, start=%lu, end %lu->%lu",
              notePitch, noteStart, currentEnd, targetEndTick);

    std::vector<NoteUtils::DisplayNote> allNotes =
        NoteUtils::reconstructNotes(midiEvents, loopLength);
    std::vector<NoteUtils::DisplayNote> otherNotesOfSamePitch;
    for (const auto& note : allNotes) {
        if (note.note == notePitch &&
            !(note.startTick == noteStart && note.endTick == displayCurrentEnd)) {
            otherNotesOfSamePitch.push_back(note);
        }
    }

    std::vector<std::pair<NoteUtils::DisplayNote, uint32_t>> notesToShorten;
    std::vector<NoteUtils::DisplayNote> notesToDelete;
    restoreOverlapNotesNoLongerOverlapping(midiEvents, manager, track.getMidiChannel(), newStart,
                                          displayNewEnd, notePitch, manager.movingNote.origStart,
                                          loopLength);
    track.invalidateCaches();
    allNotes = NoteUtils::reconstructNotes(midiEvents, loopLength);
    otherNotesOfSamePitch.clear();
    for (const auto& note : allNotes) {
        if (note.note == notePitch &&
            !(note.startTick == noteStart && note.endTick == displayCurrentEnd)) {
            otherNotesOfSamePitch.push_back(note);
        }
    }

    notesToShorten.clear();
    notesToDelete.clear();
    findOverlaps(otherNotesOfSamePitch, notePitch, noteStart, newStart, newEnd, delta,
                 loopLength, manager, notesToShorten, notesToDelete,
                 /*allowSharedEndCoexistence=*/true);

  // When lengthening would place the release on a neighbor's attack (before swallowing its
  // tail), temporarily delete that neighbor using the same ledger as move overlap.
    for (const auto& note : otherNotesOfSamePitch) {
        if (note.startTick != displayNewEnd || note.startTick <= newStart ||
            note.endTick <= displayNewEnd) {
            continue;
        }
        if (isAlreadyShortenedOverlap(manager, note) ||
            hasShortenedOverlapEntry(manager, note.note, note.startTick)) {
            continue;
        }
        const bool alreadyDeleted = std::any_of(
            notesToDelete.begin(), notesToDelete.end(), [&](const auto& victim) {
                return victim.startTick == note.startTick && victim.note == note.note;
            });
        if (alreadyDeleted) {
            continue;
        }
        notesToDelete.push_back(note);
        logger.log(CAT_MIDI, LOG_DEBUG,
                  "Will delete note (release on attack before swallow): pitch=%d, start=%lu",
                  note.note, note.startTick);
    }

    auto [onIndex, offIndex] = NoteUtils::buildEventIndex(midiEvents);
    applyShortenOrDelete(midiEvents, notesToShorten, notesToDelete, manager, loopLength,
                         onIndex, offIndex);

    MidiEvent* noteOffEvent =
        findNoteOffPairedAt(midiEvents, notePitch, noteStart, displayCurrentEnd);
    if (!noteOffEvent) {
        for (auto& evt : midiEvents) {
            if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 &&
                evt.data.noteData.note == notePitch && evt.tick == noteStart) {
                noteOffEvent = findCorrespondingNoteOff(midiEvents, &evt, notePitch, noteStart,
                                                        displayCurrentEnd);
                break;
            }
        }
    }

    if (noteOffEvent) {
        noteOffEvent->tick = newEnd;
        manager.movingNote.lastEnd = newEnd;
        noteEditFocusApplyLengthEnd(manager.getNoteEditSession().focus, newEnd);
        // origEnd / commitBaseline.end stay at length-session baseline so
        // commitPendingLengthAction can detect pending ChangeLength on reselect.
        manager.setBracketTick(newEnd % loopLength);
        logger.log(CAT_MIDI, LOG_DEBUG,
                  "Updated note-off after length overlap: pitch=%d, start=%lu, end=%lu",
                  notePitch, noteStart, newEnd);
    } else {
        logger.log(CAT_MIDI, LOG_DEBUG,
                  "Warning: could not find note-off for length edit pitch=%d start=%lu end=%lu",
                  notePitch, noteStart, displayCurrentEnd);
    }

    finalReconstructAndSelect(midiEvents, manager, notePitch, newStart, newEnd, loopLength);

    NoteUtils::orderSamePitchNoteOffsForLifo(midiEvents, track.getMidiChannel(), notePitch);

    Loop& loop = track.getActiveLoop();
    loop.markEditFlatDirty();
    track.invalidateCaches();
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
            for (auto& [ref, entry] : manager.getNoteEditSession().focus.overlapNotes) {
                if (entry.baseline.pitch == noteToExtend.note &&
                    entry.baseline.startTick == noteToExtend.startTick &&
                    entry.state == OverlapNoteStoreState::Shortened) {
                    entry.shortenedEndTick = newEndTick;
                    logger.log(CAT_MIDI, LOG_DEBUG, "Updated overlap note shortened end to %lu",
                              newEndTick);
                    break;
                }
                (void)ref;
            }
            
            logger.log(CAT_MIDI, LOG_DEBUG, "Extended note-off event: pitch=%d, from tick=%lu to tick=%lu", 
                      noteToExtend.note, oldTick, newEndTick);
        } else {
            logger.log(CAT_MIDI, LOG_DEBUG, "Warning: Could not find note-off event to extend: pitch=%d, start=%lu", 
                      noteToExtend.note, noteToExtend.startTick);
        }
    }
}

bool applyNoteEditChange(Track& track, EditManager& manager, NoteEditChangeKind kind,
                         const NoteUtils::DisplayNote& currentNote, uint32_t targetTick,
                         int delta, uint32_t targetEndTick, uint8_t currentPitch,
                         uint8_t newPitch, uint32_t& inOutStart, uint32_t& inOutEnd) {
    switch (kind) {
        case NoteEditChangeKind::Move:
            moveNoteWithOverlapHandling(track, manager, currentNote, targetTick, delta);
            return true;
        case NoteEditChangeKind::Length:
            changeLengthWithOverlapHandling(track, manager, currentNote, targetEndTick);
            return true;
        case NoteEditChangeKind::Pitch:
            inOutStart = currentNote.startTick;
            inOutEnd = currentNote.endTick;
            return applyPitchChange(track, manager, currentPitch, newPitch, inOutStart,
                                    inOutEnd);
    }
    return false;
}

} // namespace NoteMovementUtils 