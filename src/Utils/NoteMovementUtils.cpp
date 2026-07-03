//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0


#include "Logger.h"
#include "Globals.h"
#include "DisplayManager.h"
#include "NoteEditFocus.h"
#include "Utils/MidiEventUtils.h"
#include "Utils/NoteMovementUtils.h"
#include "Globals.h"
#include "Utils/DebugSessionCapture.h"
#include <algorithm>
#include <map>
#include <set>

namespace NoteMovementUtils {

MidiEvent* findNoteOnForOverlapTarget(MidiEventVec& midiEvents, uint8_t channel,
                                      const NoteUtils::DisplayNote& dn, NoteId noteId);

MidiEvent* findNoteOffForOverlapShorten(MidiEventVec& midiEvents, MidiEvent* noteOn,
                                        const NoteUtils::DisplayNote& dn,
                                        uint32_t expectedOffTick, uint32_t loopLength);

namespace {

NoteEditFocus& editFocus(EditManager& manager) {
    return manager.getEditSession().focus;
}

MidiEvent* findNoteOnAtStart(MidiEventVec& midiEvents, uint8_t channel, uint8_t pitch,
                             uint32_t startTick);

const NoteEditFocus& editFocus(const EditManager& manager) {
    return manager.getEditSession().focus;
}

OverlapNoteRestore overlapNoteToRestorePayload(const OverlapNote& entry) {
    OverlapNoteRestore restore;
    restore.noteId = entry.noteId;
    restore.pitch = entry.baseline.pitch;
    restore.velocity = entry.baseline.velocity;
    restore.startTick = entry.baseline.startTick;
    restore.endTick = entry.baseline.endTick;
    restore.originalLength = entry.baseline.endTick >= entry.baseline.startTick
                                 ? entry.baseline.endTick - entry.baseline.startTick
                                 : 0;
    restore.wasShortened = entry.state == OverlapNoteStoreState::Shortened;
    restore.shortenedToTick = entry.shortenedEndTick;
    return restore;
}

void appendUniqueRestoreCandidate(std::vector<OverlapNoteRestore>& notesToRestore,
                                  const OverlapNoteRestore& candidate) {
    for (const auto& queued : notesToRestore) {
        if (queued.pitch == candidate.pitch && queued.startTick == candidate.startTick) {
            return;
        }
    }
    notesToRestore.push_back(candidate);
}

uint32_t overlapNoteRestoreEffectiveEnd(const OverlapNoteRestore& restore) {
    return restore.wasShortened ? restore.shortenedToTick : restore.endTick;
}

bool isAlreadyShortenedOverlap(const EditManager& manager,
                               const NoteUtils::DisplayNote& note) {
    const NoteEditFocus& focus = editFocus(manager);
    for (const auto& [noteId, entry] : focus.overlapNotes) {
        (void)noteId;
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
    for (const auto& [noteId, entry] : focus.overlapNotes) {
        (void)noteId;
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
  const NoteId noteId = findBaselineNoteIdForDisplay(focus, dn);
  OverlapNote& entry = focus.overlapNotes[noteId];
  entry.noteId = noteId;
  NoteBaseline linear;
  MidiEventVec& events = manager.sessionMidiEvents();
  if (noteId != kInvalidNoteId &&
      findLinearNoteSpanForNoteId(events, noteId, channel, linear)) {
    entry.baseline = linear;
  } else {
    entry.baseline = baselineForDisplayNote(focus, dn);
  }
  return entry;
}

void markOverlapHidden(EditManager& manager, uint8_t channel,
                       const NoteUtils::DisplayNote& dn) {
    OverlapNote& entry = upsertOverlapNote(manager, channel, dn);
    entry.state = OverlapNoteStoreState::Hidden;
    entry.shortenedEndTick = 0;
    entry.innerUnderMovingNote = false;
}

void markOverlapHiddenFromPair(EditManager& manager, NoteId noteId, const MidiEvent& noteOn,
                               const MidiEvent& noteOff) {
    NoteEditFocus& focus = editFocus(manager);
    OverlapNote& entry = focus.overlapNotes[noteId];
    entry.noteId = noteId;
    entry.baseline.pitch = noteOn.data.noteData.note;
    entry.baseline.velocity = noteOn.data.noteData.velocity;
    entry.baseline.startTick = noteOn.tick;
    entry.baseline.endTick = noteOff.tick;
    entry.state = OverlapNoteStoreState::Hidden;
    entry.shortenedEndTick = 0;
    entry.innerUnderMovingNote = false;
}

void markOverlapShortened(EditManager& manager, uint8_t channel,
                          const NoteUtils::DisplayNote& dn, uint32_t shortenedEnd) {
    OverlapNote& entry = upsertOverlapNote(manager, channel, dn);
    entry.state = OverlapNoteStoreState::Shortened;
    entry.shortenedEndTick = shortenedEnd;
    entry.innerUnderMovingNote = false;
}

void removeOverlapEntry(EditManager& manager, NoteId noteId) {
    editFocus(manager).overlapNotes.erase(noteId);
}

bool isInnerOverlapNoteInMovingNoteRange(const EditManager& manager, uint8_t notePitch,
                                         uint32_t noteStart, uint32_t noteEnd,
                                         uint32_t loopLength) {
    return isInnerOverlapNoteInMovingNoteRange(editFocus(manager), notePitch, noteStart, noteEnd,
                                               loopLength);
}

size_t restoreNotes(MidiEventVec& midiEvents, const std::vector<OverlapNoteRestore>& notesToRestore,
                    EditManager& manager, uint32_t loopLength, uint8_t channel,
                    NoteUtils::EventIndexMap& onIndex, NoteUtils::EventIndexMap& offIndex,
                    bool keepOverlapTrackingForPitchRestore) {
    (void)onIndex;
    (void)offIndex;
    logger.log(CAT_MIDI, LOG_DEBUG, "=== RESTORING TEMPORARY NOTES ===");
    logger.log(CAT_MIDI, LOG_DEBUG, "Total notes to restore: %zu", notesToRestore.size());

    std::vector<OverlapNoteRestore> restored;

    for (const auto& nr : notesToRestore) {
        bool didRestore = false;

        if (nr.wasShortened) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Restoring shortened overlap note: pitch=%d, start=%lu, was shortened to %lu, "
                      "restoring to %lu",
                      nr.pitch, nr.startTick, nr.shortenedToTick, nr.endTick);

            const NoteUtils::DisplayNote dn{nr.noteId, nr.pitch, nr.velocity, nr.startTick,
                                            nr.endTick};
            MidiEvent* noteOn =
                findNoteOnForOverlapTarget(midiEvents, channel, dn, nr.noteId);
            if (noteOn != nullptr) {
                MidiEvent* noteOff = findNoteOffForOverlapShorten(
                    midiEvents, noteOn, dn, nr.shortenedToTick, loopLength);
                if (noteOff != nullptr) {
                    if (noteOff->tick != nr.endTick) {
                        logger.log(CAT_MIDI, LOG_DEBUG,
                                  "Extending note-off event: pitch=%d, from tick=%lu to tick=%lu",
                                  nr.pitch, noteOff->tick, nr.endTick);
                        noteOff->tick = nr.endTick;
                    }
                    didRestore = true;
                } else {
                    logger.log(CAT_MIDI, LOG_DEBUG,
                              "Failed to find note-off for shortened note: pitch=%d, start=%lu",
                              nr.pitch, nr.startTick);
                }
            } else {
                logger.log(CAT_MIDI, LOG_DEBUG,
                          "Recreating shortened overlap note (note-on missing): pitch=%d, start=%lu, end=%lu",
                          nr.pitch, nr.startTick, nr.endTick);
                MidiEvent onEvt;
                onEvt.tick = nr.startTick;
                onEvt.type = midi::NoteOn;
                onEvt.channel = channel;
                onEvt.data.noteData.note = nr.pitch;
                onEvt.data.noteData.velocity = nr.velocity;
                if (nr.noteId != kInvalidNoteId) {
                    onEvt.noteId = nr.noteId;
                }
                midiEvents.push_back(onEvt);

                MidiEvent offEvt;
                offEvt.tick = nr.endTick;
                offEvt.type = midi::NoteOff;
                offEvt.channel = channel;
                offEvt.data.noteData.note = nr.pitch;
                offEvt.data.noteData.velocity = 0;
                if (nr.noteId != kInvalidNoteId) {
                    offEvt.noteId = nr.noteId;
                }
                midiEvents.push_back(offEvt);
                didRestore = true;
            }
        } else {
            if (nr.noteId != kInvalidNoteId) {
                NoteBaseline linear;
                if (findLinearNoteSpanForNoteId(midiEvents, nr.noteId, channel, linear)) {
                    continue;
                }
            }
            if (findNoteOnAtStart(midiEvents, channel, nr.pitch, nr.startTick) != nullptr) {
                continue;
            }

            logger.log(CAT_MIDI, LOG_DEBUG, "Restoring hidden overlap note: pitch=%d, start=%lu, end=%lu",
                      nr.pitch, nr.startTick, nr.endTick);

            MidiEvent onEvt;
            onEvt.tick = nr.startTick;
            onEvt.type = midi::NoteOn;
            onEvt.channel = channel;
            onEvt.data.noteData.note = nr.pitch;
            onEvt.data.noteData.velocity = nr.velocity;
            midiEvents.push_back(onEvt);

            MidiEvent offEvt;
            offEvt.tick = nr.endTick;
            offEvt.type = midi::NoteOff;
            offEvt.channel = channel;
            offEvt.data.noteData.note = nr.pitch;
            offEvt.data.noteData.velocity = 0;
            midiEvents.push_back(offEvt);

            didRestore = true;
        }

        if (didRestore) {
            restored.push_back(nr);
        }
    }

    for (const auto& r : restored) {
        NoteId noteId = r.noteId;
        if (noteId == kInvalidNoteId) {
            NoteUtils::DisplayNote lookup{r.noteId, r.pitch, r.velocity, r.startTick, r.endTick};
            noteId = findBaselineNoteIdForDisplay(editFocus(manager), lookup);
        }
        if (keepOverlapTrackingForPitchRestore) {
            OverlapNote* entry = findOverlapNoteEntry(editFocus(manager), noteId);
            if (entry != nullptr) {
                entry->state = OverlapNoteStoreState::Visible;
                entry->innerUnderMovingNote = true;
            }
        } else {
            removeOverlapEntry(manager, noteId);
        }
    }

    logger.log(CAT_MIDI, LOG_DEBUG, "Restored %zu overlap notes, %zu still tracked",
              restored.size(), editFocus(manager).overlapNotes.size());
    return restored.size();
}

void restoreOverlapNotesNoLongerOverlapping(MidiEventVec& midiEvents, EditManager& manager,
                                            uint8_t channel, uint32_t moverStart,
                                            uint32_t moverDisplayEnd, uint8_t movingPitch,
                                            uint32_t originalStart, uint32_t loopLength) {
    std::vector<OverlapNoteRestore> notesToRestore;
    NoteEditFocus& focus = editFocus(manager);

    for (auto& [noteId, entry] : focus.overlapNotes) {
        (void)noteId;
        if (entry.state == OverlapNoteStoreState::Visible) {
            continue;
        }
        const OverlapNoteRestore restore = overlapNoteToRestorePayload(entry);

        if (restore.pitch != movingPitch && restore.wasShortened) {
            const uint32_t truncatedStart = restore.shortenedToTick + 1;
            const bool overlapsTruncatedZone = notesOverlap(
                moverStart, moverDisplayEnd, truncatedStart, restore.endTick, loopLength);
            if (!overlapsTruncatedZone) {
                appendUniqueRestoreCandidate(notesToRestore, restore);
            }
            continue;
        }

        if (restore.pitch != movingPitch) {
            continue;
        }

        if (restore.startTick == originalStart) {
            continue;
        }

        const uint32_t overlapNoteLength =
            calculateNoteLength(restore.startTick, restore.endTick, loopLength);
        if (overlapNoteLength == 0 || overlapNoteLength >= loopLength) {
            continue;
        }

        const uint32_t overlapNoteEnd = overlapNoteRestoreEffectiveEnd(restore);
        const bool hasOverlap =
            notesOverlap(moverStart, moverDisplayEnd, restore.startTick, overlapNoteEnd, loopLength);
        if (!hasOverlap) {
            appendUniqueRestoreCandidate(notesToRestore, restore);
        } else if (isInnerOverlapNoteInMovingNoteRange(manager, restore.pitch, restore.startTick,
                                                        overlapNoteEnd, loopLength)) {
            appendUniqueRestoreCandidate(notesToRestore, restore);
            entry.state = OverlapNoteStoreState::Visible;
            entry.innerUnderMovingNote = true;
        }
    }

    if (notesToRestore.empty()) {
        return;
    }

    auto [onIndex, offIndex] = NoteUtils::buildEventIndex(midiEvents);
    const size_t restoredCount =
        restoreNotes(midiEvents, notesToRestore, manager, loopLength, channel, onIndex, offIndex,
                     false);
    logger.log(CAT_MIDI, LOG_DEBUG, "Restored %zu overlap notes no longer overlapping mover",
              restoredCount);
}

void restoreOverlapNotesForPitchLaneClear(MidiEventVec& midiEvents, EditManager& manager,
                                          uint8_t channel, uint8_t clearedPitch,
                                          uint32_t loopLength) {
    std::vector<OverlapNoteRestore> notesToRestore;
    const NoteEditFocus& focus = editFocus(manager);
    for (const auto& [noteId, entry] : focus.overlapNotes) {
        if (entry.state == OverlapNoteStoreState::Visible) {
            continue;
        }
        if (entry.baseline.pitch != clearedPitch) {
            continue;
        }
        if (isMovingNoteOverlapScratchEntry(focus, noteId, entry.baseline)) {
            continue;
        }
        logger.log(CAT_MIDI, LOG_DEBUG,
                    "Will restore note after pitch change: pitch=%d, start=%lu, end=%lu "
                    "(no longer conflicts with new pitch lane)",
                    entry.baseline.pitch, entry.baseline.startTick, entry.baseline.endTick);
        appendUniqueRestoreCandidate(notesToRestore, overlapNoteToRestorePayload(entry));
    }
    if (notesToRestore.empty()) {
        return;
    }
    auto [onIndex, offIndex] = NoteUtils::buildEventIndex(midiEvents);
    const size_t restoredCount =
        restoreNotes(midiEvents, notesToRestore, manager, loopLength, channel, onIndex, offIndex,
                     false);
    logger.log(CAT_MIDI, LOG_DEBUG, "Restored %zu notes after pitch lane clear (of %zu queued)",
              restoredCount, notesToRestore.size());
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
                      "Skipping overlap on tracked shortened overlap note: pitch=%d, start=%lu, end=%lu",
                      note.note, note.startTick, note.endTick);
            continue;
        }

        // notesOverlap treats touching intervals as non-overlapping; a same-pitch note that ends
        // exactly where the mover starts must still be removed without pulling mover start backward.
        if (note.endTick == newStart && note.startTick < newStart) {
            notesToDelete.push_back(note);
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Will delete prefix note ending at mover start: pitch=%d, start=%lu, end=%lu "
                      "(mover starts %lu)",
                      note.note, note.startTick, note.endTick, newStart);
            continue;
        }
        
        bool overlaps = notesOverlap(newStart, displayNewEnd, note.startTick, note.endTick, loopLength);
        if (!overlaps) continue;

        if (isInnerOverlapNoteInMovingNoteRange(manager, note.note, note.startTick, note.endTick,
                                        loopLength)) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Skipping overlap on inner overlap note inside moving note range: pitch=%d, start=%lu, "
                      "end=%lu (mover %lu-%lu)",
                      note.note, note.startTick, note.endTick, newStart, displayNewEnd);
            continue;
        }
        
        // Check if the overlapping note is completely contained within the moving note's new position
        bool noteCompletelyContained = false;

        if (note.endTick < note.startTick && loopLength > 0) {
            // Display-wrapped overlap segment — contained when its on-tick lies in the linear
            // mover span (e.g. tail on@1490 inside mover 1482-1577). Display end<=displayNewEnd
            // would wrongly mark loop-start tails; isNoteWithinMovingNoteRange returns false here.
            noteCompletelyContained = (note.startTick >= newStart && note.startTick < newEnd);
        } else if (displayNewEnd >= newStart) {
            noteCompletelyContained = (note.startTick >= newStart && note.endTick <= displayNewEnd);
        } else {
            // Mover wraps in display coordinates — use linear moving-note range, not
            // (start >= newStart || end <= displayNewEnd) which marks unrelated loop-start notes.
            noteCompletelyContained = isNoteWithinMovingNoteRange(
                note.startTick, note.endTick, newStart, newEnd, loopLength);
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
                              "Keeping shortened overlap note head (too-short trim skipped): pitch=%d, start=%lu, end=%lu",
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
    (void)startTick;
    (void)endTick;
    if (noteOnEvent == nullptr) {
        return nullptr;
    }
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
            for (int stackIndex = static_cast<int>(activeNoteOnStack.size()) - 1; stackIndex >= 0;
                 --stackIndex) {
                MidiEvent* candidateOn = activeNoteOnStack[static_cast<size_t>(stackIndex)];
                if (evt.tick <= candidateOn->tick) {
                    continue;
                }
                MidiEvent* correspondingNoteOn = candidateOn;
                activeNoteOnStack.erase(activeNoteOnStack.begin() + stackIndex);
                if (correspondingNoteOn == noteOnEvent) {
                    logger.log(CAT_MIDI, LOG_DEBUG,
                              "Found corresponding note-off: pitch=%d, noteOn@%lu -> noteOff@%lu",
                              pitch, correspondingNoteOn->tick, evt.tick);
                    return &evt;
                }
                break;
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

MidiEvent* findNoteOffForNoteOnAtStart(MidiEventVec& midiEvents, uint8_t pitch,
                                       uint32_t startTick) {
    for (auto& evt : midiEvents) {
        if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 &&
            evt.data.noteData.note == pitch && evt.tick == startTick) {
            return findCorrespondingNoteOff(midiEvents, &evt, pitch, startTick, 0);
        }
    }
    return nullptr;
}

MidiEvent* resolveNoteOffForEditSpan(MidiEventVec& midiEvents, MidiEvent* noteOnEvent,
                                   uint8_t channel, uint8_t pitch, uint32_t startTick,
                                   uint32_t displayEndTick, uint32_t loopLength) {
    if (!noteOnEvent) {
        return nullptr;
    }

    if (MidiEvent* noteOffEvent = findCorrespondingNoteOff(
            midiEvents, noteOnEvent, pitch, startTick, displayEndTick)) {
        return noteOffEvent;
    }
    if (MidiEvent* noteOffEvent =
            findNoteOffPairedAt(midiEvents, pitch, startTick, displayEndTick)) {
        return noteOffEvent;
    }

    for (auto& evt : midiEvents) {
        if (!evt.isNoteOff() || evt.channel != channel || evt.data.noteData.note != pitch) {
            continue;
        }
        const uint32_t headOffTick =
            (evt.tick >= loopLength) ? (evt.tick % loopLength) : evt.tick;
        if (NoteUtils::isPreferredWrapTailForHeadOff(startTick, headOffTick, midiEvents, pitch,
                                                     channel, loopLength)) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Found wrap-head note-off: pitch=%d, tailOn@%lu -> headOff@%lu",
                       pitch, startTick, headOffTick);
            return &evt;
        }
    }
    return nullptr;
}

bool isOpenTailNoteAtLoopEnd(const MidiEventVec& midiEvents, uint8_t channel, uint8_t pitch,
                             uint32_t startTick, uint32_t displayEndTick, uint32_t loopLength) {
    if (loopLength == 0 || displayEndTick != loopLength - 1) {
        return false;
    }

    const MidiEvent* noteOnEvent = nullptr;
    for (const auto& evt : midiEvents) {
        if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 &&
            evt.data.noteData.note == pitch && evt.channel == channel &&
            evt.tick == startTick) {
            noteOnEvent = &evt;
            break;
        }
    }
    if (!noteOnEvent) {
        return false;
    }

    MidiEventVec& mutableEvents = const_cast<MidiEventVec&>(midiEvents);
    return resolveNoteOffForEditSpan(mutableEvents, const_cast<MidiEvent*>(noteOnEvent), channel,
                                     pitch, startTick, displayEndTick,
                                     loopLength) == nullptr;
}

namespace {

MidiEvent* findNoteOnAtStart(MidiEventVec& midiEvents, uint8_t channel, uint8_t pitch,
                             uint32_t startTick) {
    for (auto& evt : midiEvents) {
        if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 &&
            evt.data.noteData.note == pitch && evt.channel == channel &&
            evt.tick == startTick) {
            return &evt;
        }
    }
    return nullptr;
}

MidiEvent& appendNoteOffForOpenTail(MidiEventVec& midiEvents, uint8_t channel, uint8_t pitch,
                                    uint32_t offTick, NoteId noteId) {
    MidiEvent offEvent = MidiEvent::NoteOff(offTick, channel, pitch, 0);
    offEvent.noteId = noteId;
    midiEvents.push_back(offEvent);
    NoteUtils::orderSamePitchNoteOffsForLifo(midiEvents, channel, pitch);
    return midiEvents.back();
}

uint32_t storageOffTickForSpanEnd(uint32_t startTick, uint32_t noteLen, uint32_t loopLength) {
    (void)loopLength;
    return NoteMovementUtils::linearStorageOffTickForSpanEnd(startTick, noteLen);
}

uint32_t displayFocusEndTickForMove(uint32_t startTick, uint32_t noteLen, uint32_t loopLength) {
    if (loopLength == 0) {
        return startTick + noteLen;
    }
    const uint32_t rawEnd = startTick + noteLen;
    if (rawEnd >= loopLength) {
        return loopLength - 1;
    }
    return rawEnd;
}

void scrubStaleWrapHeadOffsForMovedNote(MidiEventVec& midiEvents, uint8_t channel, uint8_t pitch,
                                        uint32_t tailOnTick, uint32_t linearOffTick,
                                        uint32_t loopLength, NoteId movingNoteId) {
    if (loopLength == 0 || linearOffTick >= loopLength || movingNoteId == kInvalidNoteId) {
        return;
    }
    MidiEvent* moverOn = findNoteOnAtStart(midiEvents, channel, pitch, tailOnTick);
    if (moverOn == nullptr || moverOn->noteId != movingNoteId) {
        return;
    }
    const auto isStaleWrapHead = [&](const MidiEvent& evt) {
        if (!evt.isNoteOff() || evt.channel != channel || evt.data.noteData.note != pitch) {
            return false;
        }
        if (evt.tick == linearOffTick) {
            return false;
        }
        uint32_t headOffTick = evt.tick;
        if (headOffTick >= loopLength) {
            headOffTick %= loopLength;
        }
        if (headOffTick >= loopLength - 1) {
            return false;
        }
        return NoteUtils::isPreferredWrapTailForHeadOff(tailOnTick, headOffTick, midiEvents, pitch,
                                                        channel, loopLength);
    };
    midiEvents.erase(std::remove_if(midiEvents.begin(), midiEvents.end(), isStaleWrapHead),
                     midiEvents.end());
}

bool stillOpenTailAfterMove(uint32_t newStart, uint32_t noteLen, uint32_t loopLength) {
    if (loopLength == 0) {
        return false;
    }
    return (newStart + noteLen) >= loopLength;
}

bool isWrapHeadOffForTailOn(const MidiEventVec& midiEvents, MidiEvent* noteOnEvent,
                            MidiEvent* noteOffEvent, uint8_t channel, uint8_t pitch,
                            uint32_t loopLength) {
    if (!noteOnEvent || !noteOffEvent || loopLength == 0) {
        return false;
    }
    const uint32_t headOffTick =
        (noteOffEvent->tick >= loopLength) ? (noteOffEvent->tick % loopLength)
                                           : noteOffEvent->tick;
    return NoteUtils::isPreferredWrapTailForHeadOff(noteOnEvent->tick, headOffTick, midiEvents,
                                                    pitch, channel, loopLength);
}

uint32_t resolveMovingNoteLengthTicks(MidiEventVec& midiEvents, uint8_t pitch, uint32_t startTick,
                                      uint32_t fallbackEndTick, uint32_t loopLength) {
    if (loopLength == 0) {
        return 0;
    }
    if (MidiEvent* noteOffEvent = findNoteOffForNoteOnAtStart(midiEvents, pitch, startTick)) {
        const uint32_t pairedEnd = noteOffEvent->tick;
        if (pairedEnd > startTick && pairedEnd <= startTick + loopLength) {
            return pairedEnd - startTick;
        }
        return calculateNoteLength(startTick, pairedEnd, loopLength);
    }
    const uint32_t displayFallbackEnd =
        (fallbackEndTick >= loopLength) ? (fallbackEndTick % loopLength) : fallbackEndTick;
    if (fallbackEndTick > startTick && fallbackEndTick <= startTick + loopLength) {
        return fallbackEndTick - startTick;
    }
    return calculateNoteLength(startTick, displayFallbackEnd, loopLength);
}

}  // namespace

MidiEvent* findNoteOnForOverlapTarget(MidiEventVec& midiEvents, uint8_t channel,
                                      const NoteUtils::DisplayNote& dn, NoteId noteId) {
    if (noteId != kInvalidNoteId) {
        for (auto& evt : midiEvents) {
            if (evt.channel == channel && evt.noteId == noteId && evt.isNoteOn() &&
                evt.data.noteData.velocity > 0 && evt.data.noteData.note == dn.note) {
                return &evt;
            }
        }
    }
    for (auto& evt : midiEvents) {
        if (evt.channel == channel && evt.isNoteOn() && evt.data.noteData.velocity > 0 &&
            evt.data.noteData.note == dn.note && evt.tick == dn.startTick) {
            return &evt;
        }
    }
    return nullptr;
}

MidiEvent* findNoteOffForOverlapShorten(MidiEventVec& midiEvents, MidiEvent* noteOn,
                                        const NoteUtils::DisplayNote& dn,
                                        uint32_t expectedOffTick, uint32_t loopLength) {
    if (noteOn == nullptr) {
        return nullptr;
    }
    MidiEvent* off = findCorrespondingNoteOff(midiEvents, noteOn, dn.note, noteOn->tick,
                                              dn.endTick);
    if (off == nullptr) {
        return nullptr;
    }
    if (off->tick == expectedOffTick) {
        return off;
    }
    const uint32_t displayOff =
        (loopLength > 0 && off->tick >= loopLength) ? (off->tick % loopLength) : off->tick;
    if (displayOff == expectedOffTick) {
        return off;
    }
    logger.log(CAT_MIDI, LOG_DEBUG,
               "LIFO pair off tick mismatch for shorten: pitch=%d, on@%lu, paired_off@%lu, "
               "expected_off=%lu",
               dn.note, noteOn->tick, off->tick, expectedOffTick);
    return nullptr;
}

void applyShortenOrDelete(MidiEventVec& midiEvents,
                         const std::vector<std::pair<NoteUtils::DisplayNote, uint32_t>>& notesToShorten,
                         const std::vector<NoteUtils::DisplayNote>& notesToDelete,
                         EditManager& manager,
                         uint8_t channel,
                         uint32_t loopLength,
                         NoteUtils::EventIndexMap& onIndex,
                         NoteUtils::EventIndexMap& offIndex) {
    for (const auto& [dn, newEnd] : notesToShorten) {
        uint32_t currentOffTick = dn.endTick;
        const NoteId noteId = findBaselineNoteIdForDisplay(editFocus(manager), dn);
        OverlapNote* existing = findOverlapNoteEntry(editFocus(manager), noteId);
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

        MidiEvent* noteOnToShorten =
            findNoteOnForOverlapTarget(midiEvents, channel, dn, noteId);
        MidiEvent* noteOffToShorten = findNoteOffForOverlapShorten(
            midiEvents, noteOnToShorten, dn, currentOffTick, loopLength);
        if (noteOffToShorten != nullptr) {
            noteOffToShorten->tick = newEnd;
            const auto offKey = (NoteUtils::Key(dn.note) << 32) | currentOffTick;
            const auto itOff = offIndex.find(offKey);
            if (itOff != offIndex.end() && &midiEvents[itOff->second] == noteOffToShorten) {
                const size_t idx = itOff->second;
                offIndex.erase(itOff);
                offIndex[(NoteUtils::Key(dn.note) << 32) | newEnd] = idx;
            }
            continue;
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
        const NoteId noteId = findBaselineNoteIdForDisplay(editFocus(manager), dn);
        MidiEvent* noteOnToDelete =
            findNoteOnForOverlapTarget(midiEvents, channel, dn, noteId);
        if (noteOnToDelete == nullptr) {
            for (auto& evt : midiEvents) {
                if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 &&
                    evt.data.noteData.note == dn.note && evt.tick == dn.startTick) {
                    noteOnToDelete = &evt;
                    break;
                }
            }
        }

        MidiEvent* noteOffToDelete = nullptr;
        if (noteOnToDelete != nullptr) {
            noteOffToDelete = findCorrespondingNoteOff(
                midiEvents, noteOnToDelete, dn.note, noteOnToDelete->tick, dn.endTick);
            if (noteOffToDelete != nullptr && dn.endTick >= dn.startTick) {
                const uint32_t pairedLength = calculateNoteLength(
                    noteOnToDelete->tick, noteOffToDelete->tick, loopLength);
                const uint32_t expectedLength =
                    calculateNoteLength(dn.startTick, dn.endTick, loopLength);
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
                const NoteId hiddenId =
                    noteId != kInvalidNoteId ? noteId : noteOnToDelete->noteId;
                if (hiddenId != kInvalidNoteId) {
                    markOverlapHiddenFromPair(manager, hiddenId, *noteOnToDelete, *noteOffToDelete);
                } else {
                    markOverlapHidden(manager, channel, dn);
                }
                // Remove the later iterator first to preserve indices
                if (it2 > it1) {
                    midiEvents.erase(it2);
                    midiEvents.erase(it1);
                } else {
                    midiEvents.erase(it1);
                    midiEvents.erase(it2);
                }
                logger.log(CAT_MIDI, LOG_DEBUG, "Stored hidden overlap note: pitch=%d, start=%lu, end=%lu",
                          dn.note, noteOnToDelete->tick, noteOffToDelete->tick);
            }
        } else {
            logger.log(CAT_MIDI, LOG_DEBUG, "Warning: could not find specific MIDI event pair for note pitch=%d, start=%lu, end=%lu", 
                      dn.note, dn.startTick, dn.endTick);
        }
    }
}

void finalReconstructAndSelect(Track& track,
                              MidiEventVec& midiEvents,
                              EditManager& manager,
                              uint8_t movingNotePitch,
                              uint32_t newStart,
                              uint32_t newEnd,
                              uint32_t loopLength,
                              uint32_t bracketTick) {
    std::sort(midiEvents.begin(), midiEvents.end(),
              [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });

    int newSelectedIdx = -1;
    const uint32_t displayNewEnd =
        (newEnd >= loopLength && loopLength > 0) ? (newEnd % loopLength) : newEnd;

    if (manager.isNoteEditActive()) {
        const NoteEditFocus& focus = manager.getEditSession().focus;
        const std::vector<NoteUtils::DisplayNote> filtered = filterSelectableDisplayNotes(
            midiEvents, focus, track.getMidiChannel(), loopLength);

        if (focus.active && focus.movingNoteId != kInvalidNoteId) {
            newSelectedIdx = filteredDisplayNoteIndexForMovingNote(
                filtered, focus.movingNoteId, bracketTick);
            if (newSelectedIdx < 0) {
                newSelectedIdx = filteredDisplayNoteIndexForNoteIdAndStart(
                    filtered, focus.movingNoteId, focus.commitBaseline.startTick);
            }
        }

        if (newSelectedIdx < 0) {
            uint8_t matchPitch = movingNotePitch;
            uint32_t matchStart = newStart;
            uint32_t matchEnd = displayNewEnd;
            if (focus.active) {
                matchPitch = focus.last.pitch;
                matchStart = focus.last.startTick;
                matchEnd = focus.last.endTick;
                if (loopLength > 0 && matchEnd >= loopLength) {
                    matchEnd = matchEnd % loopLength;
                }
            }

            for (int i = 0; i < static_cast<int>(filtered.size()); ++i) {
                const NoteUtils::DisplayNote& dn = filtered[static_cast<size_t>(i)];
                uint32_t dnEnd = dn.endTick;
                if (loopLength > 0 && dnEnd >= loopLength) {
                    dnEnd = dnEnd % loopLength;
                }
                if (dn.note == matchPitch && dn.startTick == matchStart && dnEnd == matchEnd) {
                    newSelectedIdx = i;
                    break;
                }
            }
        }
    } else {
        const std::vector<NoteUtils::DisplayNote> finalNotes =
            NoteUtils::reconstructNotes(midiEvents, loopLength);
        for (int i = 0; i < static_cast<int>(finalNotes.size()); ++i) {
            if (finalNotes[static_cast<size_t>(i)].note == movingNotePitch &&
                finalNotes[static_cast<size_t>(i)].startTick == newStart &&
                finalNotes[static_cast<size_t>(i)].endTick == displayNewEnd) {
                newSelectedIdx = i;
                break;
            }
        }
    }

    if (newSelectedIdx >= 0) {
        const int oldSelectedIdx = manager.getSelectedNoteIdx();
        manager.setSelectedNoteIdx(newSelectedIdx);
        logger.log(CAT_MIDI, LOG_DEBUG, "Updated selectedNoteIdx: %d -> %d (note at new position)",
                   oldSelectedIdx, newSelectedIdx);
        manager.setBracketTick(bracketTick);
    } else {
        logger.log(CAT_MIDI, LOG_DEBUG, "Warning: Could not find moved note in filtered list");
        manager.setBracketTick(bracketTick);
    }

    manager.syncSelectedNoteIdxToFilteredInventory(track);
    track.invalidateCaches();
    displayManager.requestNoteInfoRefresh(track);
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

    NoteEditFocus& focus = editFocus(manager);
    if (focus.active) {
        manager.syncNoteEditFocusLastFromSessionStore(track);
        noteStart = focus.last.startTick;
        noteEnd = focus.last.endTick;
    }

    restoreOverlapNotesForPitchLaneClear(midiEvents, manager, track.getMidiChannel(),
                                         currentNoteValue, loopLength);
    track.invalidateCaches();

    const uint8_t channel = track.getMidiChannel();
    auto notes = track.getCachedNotes();
    const NoteEditFocus& focusConst = editFocus(manager);
    const bool preserveInnerNotes = focusConst.active;
    const uint32_t movingNoteStart = focusConst.movingNoteRange.start;
    const uint32_t movingNoteEnd = movingNoteRangeDisplayEnd(focusConst, loopLength);

    // Merge suffix-adjacent same-target-pitch notes into the moving note end before overlap
    // resolution. Prefix notes ending at mover start are deleted via findOverlaps without
    // extending mover start. Skip inner overlap notes inside the moving note range.
    // Compare linear storage ticks only — display start/end must not be mixed with linear noteEnd.
    std::vector<NoteUtils::DisplayNote> adjacentToDelete;
    bool mergedAdjacent = true;
    while (mergedAdjacent) {
        mergedAdjacent = false;
        notes = track.getCachedNotes();
        for (const auto& note : notes) {
            if (note.note != newNoteValue) {
                continue;
            }
            if (focusConst.movingNoteId != kInvalidNoteId &&
                note.noteId == focusConst.movingNoteId) {
                continue;
            }
            if (preserveInnerNotes &&
                isNoteWithinMovingNoteRange(note.startTick, note.endTick, movingNoteStart,
                                             movingNoteEnd, loopLength)) {
                logger.log(CAT_MIDI, LOG_DEBUG,
                           "Skipping adjacent merge for inner overlap note inside moving note range: "
                           "pitch=%d, start=%lu, end=%lu",
                           note.note, note.startTick, note.endTick);
                continue;
            }
            if (note.noteId == kInvalidNoteId) {
                continue;
            }
            NoteBaseline adjacentLinear;
            if (!findLinearNoteSpanForNoteId(midiEvents, note.noteId, channel, adjacentLinear,
                                             note.startTick)) {
                continue;
            }
            if (adjacentLinear.startTick != noteEnd) {
                continue;
            }
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Merging suffix-adjacent note into mover end: pitch=%d, linear %lu-%lu",
                       note.note, adjacentLinear.startTick, adjacentLinear.endTick);
            noteEnd = adjacentLinear.endTick;
            adjacentToDelete.push_back(note);
            mergedAdjacent = true;
            break;
        }
    }
    if (!adjacentToDelete.empty()) {
        auto [onIndex, offIndex] = NoteUtils::buildEventIndex(midiEvents);
        applyShortenOrDelete(midiEvents, {}, adjacentToDelete, manager, track.getMidiChannel(),
                             loopLength, onIndex, offIndex);
        track.invalidateCaches();
        notes = track.getCachedNotes();
        logger.log(CAT_MIDI, LOG_DEBUG,
                   "Merged %zu suffix-adjacent same-pitch notes into moving note range %lu-%lu "
                   "before pitch change",
                   adjacentToDelete.size(), noteStart, noteEnd);
    }

    std::set<std::pair<uint32_t, uint32_t>> restoredNotePositions;

    std::vector<NoteUtils::DisplayNote> otherNotesOfTargetPitch;
    for (const auto& note : notes) {
        if (note.note != newNoteValue) {
            continue;
        }
        if (focusConst.movingNoteId != kInvalidNoteId &&
            note.noteId == focusConst.movingNoteId) {
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
            isNoteWithinMovingNoteRange(note.startTick, note.endTick, movingNoteStart,
                                         movingNoteEnd, loopLength)) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Skipping pitch overlap on inner overlap note inside moving note range: "
                       "pitch=%d, start=%lu, end=%lu",
                       note.note, note.startTick, note.endTick);
            continue;
        }
        if (preserveInnerNotes && focusConst.movingNoteId != kInvalidNoteId &&
            note.noteId == focusConst.movingNoteId &&
            note.startTick != focusConst.last.startTick) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Skipping mover wrap projection segment for pitch overlap: "
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
                                 track.getMidiChannel(), loopLength, onIndex, offIndex);
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Applied pitch change overlaps: %zu shortened, %zu deleted",
                      notesToShorten.size(), notesToDelete.size());
        }
    }

    const uint32_t displayEndForResolve =
        (noteEnd >= loopLength) ? (noteEnd % loopLength) : noteEnd;

    // Resolve the note-on, then its STRUCTURALLY PAIRED note-off (not an independent
    // tick scan): two same-pitch notes can share a start or end tick when overlapping,
    // and an independent scan would repitch one event of an overlap note and orphan a note.
    bool noteOnUpdated = false;
    bool noteOffUpdated = false;
    MidiEvent* noteOnEvent = findNoteOnAtStart(midiEvents, channel, currentNoteValue, noteStart);
    MidiEvent* noteOffEvent = nullptr;
    if (noteOnEvent) {
        noteOffEvent = resolveNoteOffForEditSpan(midiEvents, noteOnEvent, channel, currentNoteValue,
                                                 noteStart, displayEndForResolve, loopLength);
        const bool openTailOnly =
            !noteOffEvent &&
            isOpenTailNoteAtLoopEnd(midiEvents, channel, currentNoteValue, noteStart,
                                    displayEndForResolve, loopLength);
        if (noteOffEvent || openTailOnly) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Updating note-on: pitch %d -> %d at tick %lu",
                      currentNoteValue, newNoteValue, noteOnEvent->tick);
            noteOnEvent->data.noteData.note = newNoteValue;
            noteOnUpdated = true;
            if (noteOffEvent) {
                logger.log(CAT_MIDI, LOG_DEBUG,
                          "Updating note-off: pitch %d -> %d at tick %lu",
                          currentNoteValue, newNoteValue, noteOffEvent->tick);
                noteOffEvent->data.noteData.note = newNoteValue;
            } else {
                logger.log(CAT_MIDI, LOG_DEBUG,
                          "Pitch edit on open-tail note: pitch %d -> %d (note-on only)",
                          currentNoteValue, newNoteValue);
            }
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

    const uint32_t linearEnd = noteOffEvent != nullptr ? noteOffEvent->tick : noteEnd;
    noteEditFocusApplyPitch(focus, newNoteValue, noteStart, linearEnd, loopLength);
    syncNoteEditFocusLinearFromSessionStore(focus, midiEvents, channel);
    focus.movingNoteRange.start = focus.last.startTick;
    focus.movingNoteRange.end = focus.last.endTick;
    if (focus.movingNoteId != kInvalidNoteId) {
        focus.overlapNotes.erase(focus.movingNoteId);
    }
    if (focus.movingNoteId != kInvalidNoteId) {
        manager.applySelectionFromGeometryEdit(track, focus.last.startTick, focus.movingNoteId);
    }
    finalReconstructAndSelect(track, midiEvents, manager, newNoteValue, noteStart,
                              focus.last.endTick, loopLength, focus.last.startTick);
    return true;
}

void moveNoteWithOverlapHandling(Track& track, EditManager& manager, 
                                const NoteUtils::DisplayNote& currentNote, 
                                uint32_t targetTick, int delta) {
    // Session store when a note-edit session is active (matches move/length live paths).
    auto& midiEvents = track.editAwareMidiEvents();
    uint32_t loopLength = track.getLoopLength();

    logger.log(CAT_MIDI, LOG_DEBUG, "NoteMovementUtils::moveNoteWithOverlapHandling called: targetTick=%lu, delta=%d", targetTick, delta);

    manager.ensureNoteEditFocusForLiveEdit(track, currentNote);
    const uint8_t channel = track.getMidiChannel();
    const NoteEditFocus& focus = editFocus(manager);
    
    if (loopLength == 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop length is 0, cannot move notes");
        return;
    }

    if (focus.active) {
        manager.syncNoteEditFocusLastFromSessionStore(track);
    }
    
    // If there's no actual movement, just update the bracket position and return
    if (delta == 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "No movement (delta=0), just updating bracket position to %lu", targetTick);
        manager.setBracketTick(targetTick);
        return;
    }
    
    uint8_t movingNotePitch = currentNote.note;
    uint32_t currentStart = currentNote.startTick;
    uint32_t currentEnd = currentNote.endTick;
    if (focus.active) {
        movingNotePitch = focus.last.pitch;
        currentStart = focus.last.startTick;
        currentEnd = focus.last.endTick;
    }
    const uint32_t originalStart =
        focus.active ? focus.commitBaseline.startTick : currentNote.startTick;
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Moving note: pitch=%d, start=%lu, end=%lu", 
              movingNotePitch, currentStart, currentEnd);
    
    uint32_t displayCurrentEnd = (currentEnd >= loopLength) ? (currentEnd % loopLength) : currentEnd;
    if (focus.active && focus.last.pitch == movingNotePitch &&
        focus.last.startTick == currentStart &&
        focus.last.endTick > currentEnd) {
        currentEnd = focus.last.endTick;
        displayCurrentEnd =
            (currentEnd >= loopLength) ? (currentEnd % loopLength) : currentEnd;
    }
    const uint32_t noteLen = resolveMovingNoteLengthTicks(
        midiEvents, movingNotePitch, currentStart, currentEnd, loopLength);
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

    // Overlap scratch is applied live via applyShortenOrDelete below. Do not call
    // commitEditAction / resolveOverlapNotesForPreCommit on this hot path — rematerialize
    // invalidates the midiEvents reference and nested reconstructNotes risks stack overflow.

    // STEP 2: Restore overlap notes no longer covered by the target mover span.
    restoreOverlapNotesNoLongerOverlapping(midiEvents, manager, channel, newStart,
                                          displayNewEnd, movingNotePitch, originalStart,
                                          loopLength);
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
    applyShortenOrDelete(midiEvents, notesToShorten, notesToDelete, manager, channel, loopLength,
                         onIndex, offIndex);
    manager.syncSelectedNoteIdxToFilteredInventory(track);

    // STEP 5: Move the mover using resolved pairing (LIFO, paired-at, wrap-head, open-tail).
    MidiEvent* noteOnEvent =
        findNoteOnAtStart(midiEvents, channel, movingNotePitch, currentStart);
    MidiEvent* noteOffEvent = nullptr;
    if (noteOnEvent) {
        noteOffEvent = resolveNoteOffForEditSpan(midiEvents, noteOnEvent, channel, movingNotePitch,
                                                 currentStart, displayCurrentEnd, loopLength);
    }

    const uint32_t storageOffTick = storageOffTickForSpanEnd(newStart, noteLen, loopLength);
    const uint32_t displayEndForBracket =
        displayFocusEndTickForMove(newStart, noteLen, loopLength);

    bool movedNoteEvents = false;
    if (noteOnEvent && noteOffEvent) {
        const uint8_t actualCurrentPitch = noteOnEvent->data.noteData.note;
        if (actualCurrentPitch != movingNotePitch) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Note pitch changed during move: %d -> %d, updating focus",
                      movingNotePitch, actualCurrentPitch);
            movingNotePitch = actualCurrentPitch;
        }

        noteOnEvent->tick = newStart;
        const uint32_t linearStorageOff =
            NoteMovementUtils::linearStorageOffTickForSpanEnd(newStart, noteLen);
        noteOffEvent->tick = linearStorageOff;
        logger.log(CAT_MIDI, LOG_DEBUG, "Moved note events: pitch=%u start->%lu end->%lu",
                   movingNotePitch, newStart, linearStorageOff);
        noteEditFocusApplyMoveEnd(manager.getEditSession().focus, newStart, linearStorageOff);
        if (linearStorageOff < loopLength) {
            scrubStaleWrapHeadOffsForMovedNote(midiEvents, channel, movingNotePitch, newStart,
                                               linearStorageOff, loopLength, noteOnEvent->noteId);
        }
        if (actualCurrentPitch != currentNote.note) {
            noteEditFocusApplyPitch(manager.getEditSession().focus, actualCurrentPitch, newStart,
                                    linearStorageOff, loopLength);
        }
        const uint32_t displayEndForTelemetry =
            (linearStorageOff >= loopLength && loopLength > 0) ? (linearStorageOff % loopLength)
                                                               : linearStorageOff;
        SC_DNTE(movingNotePitch, newStart, newStart,
                displayEndForTelemetry >= newStart ? displayEndForTelemetry - newStart : noteLen,
                manager.getSelectedNoteIdx());
        movedNoteEvents = true;
    } else if (noteOnEvent &&
               isOpenTailNoteAtLoopEnd(midiEvents, channel, movingNotePitch, currentStart,
                                       displayCurrentEnd, loopLength)) {
        noteOnEvent->tick = newStart;
        if (!stillOpenTailAfterMove(newStart, noteLen, loopLength)) {
            appendNoteOffForOpenTail(midiEvents, channel, movingNotePitch, storageOffTick,
                                     noteOnEvent->noteId);
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Moved open-tail note: pitch=%u start->%lu materialized off@%lu",
                       movingNotePitch, newStart, storageOffTick);
        } else {
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Moved open-tail note: pitch=%u start->%lu (still open to loop end)",
                       movingNotePitch, newStart);
        }
        noteEditFocusApplyMoveEnd(manager.getEditSession().focus, newStart,
                                  stillOpenTailAfterMove(newStart, noteLen, loopLength)
                                      ? displayEndForBracket
                                      : storageOffTick);
        SC_DNTE(movingNotePitch, newStart, newStart,
                displayEndForBracket >= newStart ? displayEndForBracket - newStart : noteLen,
                manager.getSelectedNoteIdx());
        movedNoteEvents = true;
    }

    if (!movedNoteEvents) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Warning: could not find MIDI events for moving note pitch=%u at start=%lu end=%lu", 
                  movingNotePitch, currentStart, currentEnd);
    }

    finalReconstructAndSelect(track, midiEvents, manager, movingNotePitch, newStart,
                              displayEndForBracket, loopLength, newStart);

    track.invalidateCaches();
}

void changeLengthWithOverlapHandling(Track& track, EditManager& manager,
                                     const NoteUtils::DisplayNote& currentNote,
                                     uint32_t targetEndTick) {
    auto& midiEvents = track.editAwareMidiEvents();
    uint32_t loopLength = track.getLoopLength();
    manager.ensureNoteEditFocusForLiveEdit(track, currentNote);
    manager.syncNoteEditFocusLastFromSessionStore(track);
    const uint8_t channel = track.getMidiChannel();
    const NoteEditFocus& focus = editFocus(manager);

    if (loopLength == 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop length is 0, cannot change note length");
        return;
    }

    uint8_t notePitch = currentNote.note;
    uint32_t noteStart = currentNote.startTick;
    uint32_t currentEnd = currentNote.endTick;
    if (focus.active) {
        notePitch = focus.last.pitch;
        noteStart = focus.last.startTick;
        currentEnd = focus.last.endTick;
    }
    if (loopLength > 0) {
        noteStart %= loopLength;
        currentEnd %= loopLength;
        targetEndTick %= loopLength;
    }
    uint32_t displayCurrentEnd = currentEnd;

    if (targetEndTick == currentEnd) {
        manager.setBracketTick(targetEndTick % loopLength);
        return;
    }

    const uint32_t minNoteDuration = Config::TICKS_PER_16TH_STEP;
    const bool nonWrapMovingNote =
        currentEnd > noteStart && (currentEnd - noteStart) < loopLength;

    uint32_t newStart = noteStart;
    uint32_t newEnd = targetEndTick;

    if (nonWrapMovingNote && newEnd < noteStart + minNoteDuration) {
        newEnd = noteStart + minNoteDuration;
    }

    const int delta = (newEnd > currentEnd) ? 1 : -1;
    const uint32_t baselineStart =
        focus.active ? focus.commitBaseline.startTick : noteStart;

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
    restoreOverlapNotesNoLongerOverlapping(midiEvents, manager, channel, newStart,
                                          displayNewEnd, notePitch, baselineStart,
                                          loopLength);
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

  // When lengthening would place the release on an overlap note's attack (before swallowing its
  // tail), temporarily hide that overlap note using the same path as move overlap.
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
            notesToDelete.begin(), notesToDelete.end(), [&](const auto& overlapNote) {
                return overlapNote.startTick == note.startTick && overlapNote.note == note.note;
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
    applyShortenOrDelete(midiEvents, notesToShorten, notesToDelete, manager, channel, loopLength,
                         onIndex, offIndex);
    manager.syncSelectedNoteIdxToFilteredInventory(track);

    MidiEvent* noteOnEvent = findNoteOnAtStart(midiEvents, channel, notePitch, noteStart);

    MidiEvent* noteOffEvent = nullptr;
    if (noteOnEvent) {
        noteOffEvent = resolveNoteOffForEditSpan(midiEvents, noteOnEvent, channel, notePitch,
                                                 noteStart, displayCurrentEnd, loopLength);
    }

    if (noteOnEvent && noteOffEvent) {
        noteOnEvent->tick = newStart;
        const uint32_t noteLen =
            NoteMovementUtils::calculateNoteLength(newStart, newEnd, loopLength);
        const uint32_t linearStorageOff =
            NoteMovementUtils::linearStorageOffTickForSpanEnd(newStart, noteLen);
        noteOffEvent->tick = linearStorageOff;
        noteEditFocusApplyLengthEnd(manager.getEditSession().focus, linearStorageOff);
        manager.setBracketTick(displayNewEnd);
        logger.log(CAT_MIDI, LOG_DEBUG,
                  "Updated note events after length overlap: pitch=%d, start=%lu, linearEnd=%lu",
                  notePitch, newStart, linearStorageOff);
    } else {
        logger.log(CAT_MIDI, LOG_DEBUG,
                  "Warning: could not find note-off for length edit pitch=%d start=%lu end=%lu",
                  notePitch, noteStart, displayCurrentEnd);
    }

    finalReconstructAndSelect(track, midiEvents, manager, notePitch, newStart, newEnd, loopLength,
                              displayNewEnd);

    NoteUtils::orderSamePitchNoteOffsForLifo(midiEvents, track.getMidiChannel(), notePitch);

    track.invalidateCaches();
}

// Extend shortened notes dynamically
void extendShortenedNotes(MidiEventVec& midiEvents,
                         const std::vector<std::pair<OverlapNoteRestore, std::uint32_t>>& notesToExtend,
                         EditManager& manager,
                         std::uint32_t loopLength) {
    logger.log(CAT_MIDI, LOG_DEBUG, "=== EXTENDING SHORTENED NOTES ===");
    logger.log(CAT_MIDI, LOG_DEBUG, "Total notes to extend: %zu", notesToExtend.size());
    
    for (const auto& [noteToExtend, newEndTick] : notesToExtend) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Extending shortened note: pitch=%d, start=%lu, from %lu to %lu", 
                  noteToExtend.pitch, noteToExtend.startTick, noteToExtend.shortenedToTick, newEndTick);
        
        // Find the note-off event at its current shortened position
        MidiEvent* noteOffEvent = nullptr;
        for (auto& event : midiEvents) {
            if ((event.type == midi::NoteOff || (event.type == midi::NoteOn && event.data.noteData.velocity == 0)) &&
                event.data.noteData.note == noteToExtend.pitch && 
                event.tick > noteToExtend.startTick) {
                // Take the first note-off we find for this pitch after the note-on
                noteOffEvent = &event;
                break;
            }
        }
        
        if (noteOffEvent) {
            uint32_t oldTick = noteOffEvent->tick;
            noteOffEvent->tick = newEndTick;
            
            // Update the tracking in overlap notes
            for (auto& [noteId, entry] : manager.getEditSession().focus.overlapNotes) {
                (void)noteId;
                if (entry.baseline.pitch == noteToExtend.pitch &&
                    entry.baseline.startTick == noteToExtend.startTick &&
                    entry.state == OverlapNoteStoreState::Shortened) {
                    entry.shortenedEndTick = newEndTick;
                    logger.log(CAT_MIDI, LOG_DEBUG, "Updated overlap note shortened end to %lu",
                              newEndTick);
                    break;
                }
            }
            
            logger.log(CAT_MIDI, LOG_DEBUG, "Extended note-off event: pitch=%d, from tick=%lu to tick=%lu", 
                      noteToExtend.pitch, oldTick, newEndTick);
        } else {
            logger.log(CAT_MIDI, LOG_DEBUG, "Warning: Could not find note-off event to extend: pitch=%d, start=%lu", 
                      noteToExtend.pitch, noteToExtend.startTick);
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