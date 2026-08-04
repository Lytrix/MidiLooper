//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0


#include "Logger.h"
#include "Globals.h"
#ifndef PIO_UNIT_TEST_NATIVE
#include "DisplayManager.h"
#endif
#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"
#include "Utils/NoteEditDisplaySnapshot.h"
#include "Utils/MidiEventUtils.h"
#include "Utils/NoteMovementUtils.h"
#include "RunEditSessionGeometryPipelineDriver.h"
#include "Utils/IntervalProjection.h"
#include "Globals.h"
#include "Utils/DebugSessionCapture.h"
#include <algorithm>
#include <map>
#include <set>
#include "Utils/NoteEditMem.h"

namespace NoteMovementUtils {

namespace {

uint32_t storageTickToDisplayPhase(uint32_t tick, uint32_t loopLength) {
    return IntervalProjection::tickPhaseInLoop(tick, 0, loopLength);
}

uint32_t bracketDisplayTickFromStorage(uint32_t storageTick, uint32_t loopStartTick,
                                       uint32_t loopLength) {
    return NoteEditDisplaySnapshot::displayStartTickFromStorage(storageTick, loopStartTick,
                                                                loopLength);
}

}  // namespace

MidiEvent* findNoteOnForOverlapTarget(MidiEventVec& midiEvents, uint8_t channel,
                                      const NoteUtils::DisplayNote& dn, NoteId noteId);

MidiEvent* findNoteOffForOverlapShorten(MidiEventVec& midiEvents, MidiEvent* noteOn,
                                        const NoteUtils::DisplayNote& dn,
                                        uint32_t expectedOffTick, uint32_t loopLength);

namespace {

NOTE_EDIT_MEM void stampPairedNoteId(MidiEvent* noteOn, MidiEvent* noteOff) {
    if (noteOn != nullptr && noteOff != nullptr && noteOn->noteId != kInvalidNoteId &&
        noteOff->noteId == kInvalidNoteId) {
        noteOff->noteId = noteOn->noteId;
    }
}

NOTE_EDIT_MEM NoteEditFocus& editFocus(EditManager& manager) {
    return manager.getEditSession().focus;
}

MidiEvent* findNoteOnAtStart(MidiEventVec& midiEvents, uint8_t channel, uint8_t pitch,
                             uint32_t startTick);

NOTE_EDIT_MEM const NoteEditFocus& editFocus(const EditManager& manager) {
    return manager.getEditSession().focus;
}

NOTE_EDIT_MEM OverlapNoteRestore overlapNoteToRestorePayload(const NoteEditFocus& focus, const OverlapNote& entry,
                                               MidiEventVec* sessionEvents, uint8_t channel) {
    OverlapNoteRestore restore;
    restore.noteId = entry.noteId;
    const NoteBaseline baseline =
        linearBaselineForOverlapRestore(focus, entry, sessionEvents, channel);
    restore.pitch = baseline.pitch;
    restore.velocity = baseline.velocity;
    restore.startTick = baseline.startTick;
    restore.endTick = baseline.endTick;
    restore.originalLength = baseline.endTick >= baseline.startTick
                                 ? baseline.endTick - baseline.startTick
                                 : 0;
    restore.wasShortened = entry.state == OverlapNoteStoreState::Shortened;
    restore.shortenedToTick = entry.shortenedEndTick;
    return restore;
}

NOTE_EDIT_MEM void appendUniqueRestoreCandidate(std::vector<OverlapNoteRestore>& notesToRestore,
                                  const OverlapNoteRestore& candidate) {
    for (const auto& queued : notesToRestore) {
        if (queued.pitch == candidate.pitch && queued.startTick == candidate.startTick) {
            return;
        }
    }
    notesToRestore.push_back(candidate);
}

NOTE_EDIT_MEM uint32_t overlapNoteRestoreEffectiveEnd(const OverlapNoteRestore& restore) {
    return restore.wasShortened ? restore.shortenedToTick : restore.endTick;
}

NOTE_EDIT_MEM bool isAlreadyShortenedOverlap(const EditManager& manager,
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

NOTE_EDIT_MEM bool hasShortenedOverlapEntry(const EditManager& manager, uint8_t pitch,
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

NOTE_EDIT_MEM OverlapNote& upsertOverlapNote(EditManager& manager, uint8_t channel,
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
    const NoteBaseline candidate = baselineForDisplayNote(focus, dn);
    if (candidate.endTick >= candidate.startTick) {
      entry.baseline = candidate;
    } else if (entry.baseline.endTick >= entry.baseline.startTick) {
      // Keep existing linear baseline; do not overwrite with display wrap tail end.
    } else {
      entry.baseline = linearBaselineForOverlapRestore(focus, entry, &events, channel);
    }
  }
  return entry;
}

NOTE_EDIT_MEM void markOverlapHidden(EditManager& manager, uint8_t channel,
                       const NoteUtils::DisplayNote& dn) {
    OverlapNote& entry = upsertOverlapNote(manager, channel, dn);
    entry.state = OverlapNoteStoreState::Hidden;
    entry.shortenedEndTick = 0;
    entry.innerUnderMovingNote = false;
}

NOTE_EDIT_MEM void markOverlapHiddenFromPair(EditManager& manager, NoteId noteId, const MidiEvent& noteOn,
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

NOTE_EDIT_MEM void markOverlapShortened(EditManager& manager, uint8_t channel,
                          const NoteUtils::DisplayNote& dn, uint32_t shortenedEnd) {
    OverlapNote& entry = upsertOverlapNote(manager, channel, dn);
    NoteEditFocus& focus = editFocus(manager);
    NoteBaseline linear;
    MidiEventVec& events = manager.sessionMidiEvents();
    if (resolveLinearNoteSpanForOverlap(focus, events, channel, dn, linear)) {
        entry.baseline = linear;
    }
    if (entry.noteId != kInvalidNoteId) {
        const auto mapIt = focus.baselineMap.find(entry.noteId);
        if (mapIt != focus.baselineMap.end() &&
            mapIt->second.endTick > shortenedEnd &&
            mapIt->second.startTick == entry.baseline.startTick) {
            entry.baseline.endTick = mapIt->second.endTick;
            entry.baseline.pitch = mapIt->second.pitch;
            entry.baseline.velocity = mapIt->second.velocity;
        }
    }
    entry.state = OverlapNoteStoreState::Shortened;
    entry.shortenedEndTick = shortenedEnd;
    entry.innerUnderMovingNote = false;
}

NOTE_EDIT_MEM void removeOverlapEntry(EditManager& manager, NoteId noteId) {
    editFocus(manager).overlapNotes.erase(noteId);
}

NOTE_EDIT_MEM bool isInnerOverlapNoteInMovingNoteRange(const EditManager& manager, uint8_t notePitch,
                                         uint32_t noteStart, uint32_t noteEnd,
                                         uint32_t loopLength) {
    return isInnerOverlapNoteInMovingNoteRange(editFocus(manager), notePitch, noteStart, noteEnd,
                                               loopLength);
}

NOTE_EDIT_MEM size_t restoreNotes(MidiEventVec& midiEvents, const std::vector<OverlapNoteRestore>& notesToRestore,
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

NOTE_EDIT_MEM void restoreOverlapNotesNoLongerOverlapping(MidiEventVec& midiEvents, EditManager& manager,
                                            uint8_t channel, uint32_t moverStart,
                                            uint32_t moverLinearEnd, uint8_t movingPitch,
                                            uint32_t originalStart, uint32_t loopLength) {
    std::vector<OverlapNoteRestore> notesToRestore;
    NoteEditFocus& focus = editFocus(manager);

    const auto moverOverlapsRestore = [&](uint32_t noteStart, uint32_t noteEnd) {
        if (moverLinearEnd >= moverStart && noteEnd >= noteStart) {
            return linearStorageSpansOverlap(moverStart, moverLinearEnd, noteStart, noteEnd);
        }
        return notesOverlap(moverStart, moverLinearEnd, noteStart, noteEnd, loopLength);
    };

    for (auto& [noteId, entry] : focus.overlapNotes) {
        (void)noteId;
        if (entry.state == OverlapNoteStoreState::Visible) {
            continue;
        }
        const OverlapNoteRestore restore = overlapNoteToRestorePayload(focus, entry, &midiEvents, channel);

        if (entry.state == OverlapNoteStoreState::Hidden && restore.pitch != movingPitch) {
            if (!moverOverlapsRestore(restore.startTick, restore.endTick)) {
                appendUniqueRestoreCandidate(notesToRestore, restore);
            }
            continue;
        }

        if (restore.pitch != movingPitch && restore.wasShortened) {
            const uint32_t truncatedStart = restore.shortenedToTick + 1;
            const bool overlapsTruncatedZone = moverOverlapsRestore(truncatedStart, restore.endTick);
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
            moverOverlapsRestore(restore.startTick, overlapNoteEnd);
        if (!hasOverlap) {
            appendUniqueRestoreCandidate(notesToRestore, restore);
        } else if (isInnerOverlapNoteInMovingNoteRange(manager, restore.pitch, restore.startTick,
                                                        overlapNoteEnd, loopLength)) {
            if (entry.state == OverlapNoteStoreState::Hidden) {
                entry.innerUnderMovingNote = true;
                continue;
            }
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

NOTE_EDIT_MEM void restoreOverlapNotesForPitchLaneClear(MidiEventVec& midiEvents, EditManager& manager,
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
        const OverlapNoteRestore restore =
            overlapNoteToRestorePayload(focus, entry, &midiEvents, channel);
        logger.log(CAT_MIDI, LOG_DEBUG,
                    "Will restore note after pitch change: pitch=%d, start=%lu, end=%lu "
                    "(no longer conflicts with new pitch lane)",
                    restore.pitch, restore.startTick, restore.endTick);
        appendUniqueRestoreCandidate(notesToRestore, restore);
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

NOTE_EDIT_MEM bool notesOverlap(uint32_t start1, uint32_t end1, uint32_t start2, uint32_t end2, uint32_t loopLength) {
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

NOTE_EDIT_MEM bool linearStorageSpansOverlap(uint32_t start1, uint32_t end1, uint32_t start2, uint32_t end2) {
    return start1 < end2 && start2 < end1;
}

namespace {

NOTE_EDIT_MEM bool resolveOverlapNoteLinearSpan(const NoteEditFocus& focus, MidiEventVec& events, uint8_t channel,
                                  const NoteUtils::DisplayNote& dn, uint32_t loopLength,
                                  NoteBaseline& out) {
    const NoteId noteId = dn.noteId != kInvalidNoteId ? dn.noteId
                                                      : findBaselineNoteIdForDisplay(focus, dn);
    if (noteId != kInvalidNoteId) {
        const auto mapIt = focus.baselineMap.find(noteId);
        if (mapIt != focus.baselineMap.end() &&
            (loopLength == 0 ||
             isPlausibleStorageSpan(mapIt->second.startTick, mapIt->second.endTick, loopLength))) {
            out = mapIt->second;
            return true;
        }
    }
    if (dn.endTick >= dn.startTick && (loopLength == 0 || !isInflatedDisplaySpan(dn, loopLength)) &&
        (loopLength == 0 ||
         isPlausibleStorageSpan(dn.startTick, dn.endTick, loopLength))) {
        out = baselineFromDisplayNote(dn);
        return true;
    }
    if (noteId != kInvalidNoteId &&
        findLinearNoteSpanForNoteId(events, noteId, channel, out, dn.startTick, loopLength)) {
        return true;
    }
    if (resolveLinearNoteSpanForOverlap(focus, events, channel, dn, out, loopLength)) {
        return true;
    }
    return false;
}

NOTE_EDIT_MEM bool isOtherSamePitchNote(const NoteEditFocus& focus, const NoteUtils::DisplayNote& note,
                          uint8_t pitch) {
    if (note.note != pitch) {
        return false;
    }
    if (focus.active && focus.movingNoteId != kInvalidNoteId &&
        note.noteId == focus.movingNoteId) {
        return false;
    }
    return true;
}

}  // namespace

NOTE_EDIT_MEM void findOverlaps(const std::vector<NoteUtils::DisplayNote>& currentNotes,
                 uint8_t movingNotePitch,
                 uint32_t currentStart,
                 uint32_t newStart,
                 uint32_t newEnd,
                 int delta,
                 uint32_t loopLength,
                 const EditManager& manager,
                 MidiEventVec& sessionEvents,
                 uint8_t channel,
                 NoteId movingNoteId,
                 std::vector<std::pair<NoteUtils::DisplayNote, uint32_t>>& notesToShorten,
                 std::vector<NoteUtils::DisplayNote>& notesToDelete,
                 bool allowSharedEndCoexistence) {
    (void)movingNotePitch;
    (void)currentStart;
    (void)delta;
    const NoteEditFocus& focus = editFocus(manager);

    uint32_t displayNewEnd = storageTickToDisplayPhase(newEnd, loopLength);

    for (const auto& note : currentNotes) {
        if (movingNoteId != kInvalidNoteId && note.noteId == movingNoteId) {
            continue;
        }

        NoteBaseline linear{};
        if (!resolveOverlapNoteLinearSpan(focus, sessionEvents, channel, note, loopLength,
                                          linear)) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Skipping overlap on display wrap projection without linear span: "
                      "pitch=%d, start=%lu, end=%lu",
                      note.note, note.startTick, note.endTick);
            continue;
        }
        const uint32_t noteLinearStart = linear.startTick;
        const uint32_t noteLinearEnd = linear.endTick;

        if (isAlreadyShortenedOverlap(manager, note) ||
            hasShortenedOverlapEntry(manager, note.note, note.startTick)) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Skipping overlap on tracked shortened overlap note: pitch=%d, start=%lu, end=%lu",
                      note.note, note.startTick, note.endTick);
            continue;
        }

        // Adjacent touch (noteLinearEnd == newStart) is not overlap — linearStorageSpansOverlap
        // is false there (session_20260714_011558 restore churn). Partial prefix under mover
        // start is overlap and is shortened (or deleted only when trim < 16th) below.

        bool overlaps = false;
        if (noteLinearEnd >= noteLinearStart && newEnd >= newStart) {
            overlaps = linearStorageSpansOverlap(newStart, newEnd, noteLinearStart, noteLinearEnd);
        } else {
            overlaps = notesOverlap(newStart, newEnd, noteLinearStart, noteLinearEnd, loopLength);
        }
        if (!overlaps) continue;

        if (isInnerOverlapNoteInMovingNoteRange(manager, note.note, note.startTick, note.endTick,
                                        loopLength)) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Skipping overlap on inner overlap note inside moving note range: pitch=%d, start=%lu, "
                      "end=%lu (mover %lu-%lu)",
                      note.note, note.startTick, note.endTick, newStart, displayNewEnd);
            continue;
        }

        bool noteCompletelyContained = false;

        if (note.endTick < note.startTick && loopLength > 0) {
            noteCompletelyContained = (note.startTick >= newStart && note.startTick < newEnd);
        } else if (displayNewEnd >= newStart) {
            noteCompletelyContained =
                (noteLinearStart >= newStart && noteLinearEnd <= newEnd);
        } else {
            noteCompletelyContained = isNoteWithinMovingNoteRange(
                noteLinearStart, noteLinearEnd, newStart, newEnd, loopLength);
        }

        if (noteCompletelyContained) {
            if (allowSharedEndCoexistence && noteLinearStart > newStart &&
                noteLinearEnd == newEnd) {
                logger.log(CAT_MIDI, LOG_DEBUG,
                          "Skipping delete for same-pitch shared release: pitch=%d, linear %lu-%lu "
                          "(mover %lu-%lu)",
                          note.note, noteLinearStart, noteLinearEnd, newStart, newEnd);
                continue;
            }
            notesToDelete.push_back(note);
            logger.log(CAT_MIDI, LOG_DEBUG,
                      "Will delete completely contained note: pitch=%d, linear %lu-%lu "
                      "(within moving note %lu-%lu)",
                      note.note, noteLinearStart, noteLinearEnd, newStart, newEnd);
        } else {
            uint32_t newNoteEnd;

            if (noteLinearStart < newStart) {
                if (newStart == 0) {
                    newNoteEnd = loopLength - 1;
                } else {
                    newNoteEnd = newStart - 1;
                }
            } else {
                notesToDelete.push_back(note);
                logger.log(CAT_MIDI, LOG_DEBUG,
                          "Will delete overlapping note that starts after moving note: pitch=%d, "
                          "linear %lu-%lu",
                          note.note, noteLinearStart, noteLinearEnd);
                continue;
            }

            uint32_t shortenedLength =
                calculateNoteLength(noteLinearStart, newNoteEnd, loopLength);

            if (shortenedLength < 49) {
                if (hasShortenedOverlapEntry(manager, note.note, note.startTick)) {
                    logger.log(CAT_MIDI, LOG_DEBUG,
                              "Keeping shortened overlap note head (too-short trim skipped): pitch=%d, start=%lu, end=%lu",
                              note.note, note.startTick, note.endTick);
                } else {
                    notesToDelete.push_back(note);
                    logger.log(CAT_MIDI, LOG_DEBUG,
                              "Will delete note (too short after shortening): pitch=%d, linear %lu-%lu->%lu, length=%lu < 49",
                              note.note, noteLinearStart, noteLinearEnd, newNoteEnd,
                              shortenedLength);
                }
            } else {
                notesToShorten.push_back({note, newNoteEnd});
                logger.log(CAT_MIDI, LOG_DEBUG,
                          "Will shorten note: pitch=%d, linear %lu-%lu->%lu, length=%lu",
                          note.note, noteLinearStart, noteLinearEnd, newNoteEnd,
                          shortenedLength);
            }
        }
    }

    auto dedupeDisplayNotes = [](std::vector<NoteUtils::DisplayNote>& notes) {
        std::vector<NoteUtils::DisplayNote> unique;
        unique.reserve(notes.size());
        for (const auto& note : notes) {
            bool duplicate = false;
            for (const auto& kept : unique) {
                if (note.noteId != kInvalidNoteId && kept.noteId == note.noteId) {
                    duplicate = true;
                    break;
                }
                if (kept.note == note.note && kept.startTick == note.startTick) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                unique.push_back(note);
            }
        }
        notes.swap(unique);
    };
    dedupeDisplayNotes(notesToDelete);

    logger.log(CAT_MIDI, LOG_DEBUG, "Found %zu notes to shorten and %zu notes to delete",
              notesToShorten.size(), notesToDelete.size());
}

template <typename Alloc>
NOTE_EDIT_MEM MidiEvent* findCorrespondingNoteOff(std::vector<MidiEvent, Alloc>& midiEvents,
                                                  MidiEvent* noteOnEvent, uint8_t pitch,
                                                  std::uint32_t startTick, std::uint32_t endTick) {
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

template MidiEvent* findCorrespondingNoteOff<InternalHeapFirstAllocator<MidiEvent>>(
    MidiEventVec&, MidiEvent*, uint8_t, std::uint32_t, std::uint32_t);
template MidiEvent* findCorrespondingNoteOff<ExternalMemoryFirstAllocator<MidiEvent>>(
    SessionMidiEventVec&, MidiEvent*, uint8_t, std::uint32_t, std::uint32_t);

NOTE_EDIT_MEM MidiEvent* findNoteOffPairedAt(MidiEventVec& midiEvents, uint8_t pitch, uint32_t startTick,
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

NOTE_EDIT_MEM MidiEvent* findNoteOffForNoteOnAtStart(MidiEventVec& midiEvents, uint8_t channel,
                                       uint8_t pitch, uint32_t startTick, NoteId noteId,
                                       uint32_t loopLength) {
    if (noteId != kInvalidNoteId) {
        for (auto& evt : midiEvents) {
            if (evt.noteId != noteId || evt.channel != channel || !evt.isNoteOn() ||
                evt.data.noteData.velocity == 0 || evt.data.noteData.note != pitch) {
                continue;
            }
            if (evt.tick != startTick) {
                continue;
            }
            if (MidiEvent* linearOff =
                    findLinearOffForNoteId(midiEvents, evt, noteId, loopLength)) {
                return linearOff;
            }
            return findCorrespondingNoteOff(midiEvents, &evt, pitch, evt.tick, 0);
        }
    }
    for (auto& evt : midiEvents) {
        if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 &&
            evt.data.noteData.note == pitch && evt.channel == channel &&
            evt.tick == startTick) {
            return findCorrespondingNoteOff(midiEvents, &evt, pitch, startTick, 0);
        }
    }
    return nullptr;
}

NOTE_EDIT_MEM MidiEvent* resolveNoteOffForEditSpan(MidiEventVec& midiEvents, MidiEvent* noteOnEvent,
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
        const uint32_t headOffTick = storageTickToDisplayPhase(evt.tick, loopLength);
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

NOTE_EDIT_MEM bool isOpenTailNoteAtLoopEnd(const MidiEventVec& midiEvents, uint8_t channel, uint8_t pitch,
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

NOTE_EDIT_MEM MidiEvent* resolveMovingNoteOffForEdit(MidiEventVec& midiEvents, MidiEvent* noteOnEvent,
                                       NoteId movingNoteId, uint8_t channel, uint8_t pitch,
                                       uint32_t startTick, uint32_t displayEndTick,
                                       uint32_t loopLength) {
    if (noteOnEvent == nullptr) {
        return nullptr;
    }
    MidiEvent* noteOffEvent = nullptr;
    if (movingNoteId != kInvalidNoteId && noteOnEvent->noteId == movingNoteId) {
        noteOffEvent =
            findLinearOffForNoteId(midiEvents, *noteOnEvent, movingNoteId, loopLength);
    }
    if (noteOffEvent == nullptr) {
        noteOffEvent = resolveNoteOffForEditSpan(midiEvents, noteOnEvent, channel, pitch,
                                                 startTick, displayEndTick, loopLength);
    }
    stampPairedNoteId(noteOnEvent, noteOffEvent);
    return noteOffEvent;
}

NOTE_EDIT_MEM MidiEvent* findNoteOnAtStart(MidiEventVec& midiEvents, uint8_t channel, uint8_t pitch,
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

NOTE_EDIT_MEM MidiEvent& appendNoteOffForOpenTail(MidiEventVec& midiEvents, uint8_t channel, uint8_t pitch,
                                    uint32_t offTick, NoteId noteId) {
    MidiEvent offEvent = MidiEvent::NoteOff(offTick, channel, pitch, 0);
    offEvent.noteId = noteId;
    midiEvents.push_back(offEvent);
    NoteUtils::orderSamePitchNoteOffsForLifo(midiEvents, channel, pitch);
    return midiEvents.back();
}

NOTE_EDIT_MEM uint32_t storageOffTickForSpanEnd(uint32_t startTick, uint32_t noteLen, uint32_t loopLength) {
    (void)loopLength;
    return NoteMovementUtils::linearStorageOffTickForSpanEnd(startTick, noteLen);
}

NOTE_EDIT_MEM uint32_t displayFocusEndTickForMove(uint32_t startTick, uint32_t noteLen, uint32_t loopLength) {
    if (loopLength == 0) {
        return startTick + noteLen;
    }
    const uint32_t rawEnd = startTick + noteLen;
    if (rawEnd >= loopLength) {
        return loopLength - 1;
    }
    return rawEnd;
}

NOTE_EDIT_MEM void scrubStaleWrapHeadOffsForMovedNote(MidiEventVec& midiEvents, uint8_t channel, uint8_t pitch,
                                        uint32_t tailOnTick, uint32_t linearOffTick,
                                        uint32_t loopLength, NoteId movingNoteId) {
    if (loopLength == 0 || movingNoteId == kInvalidNoteId) {
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
            if (headOffTick == linearOffTick) {
                return false;
            }
            headOffTick %= loopLength;
        }
        if (headOffTick >= loopLength - 1) {
            return false;
        }
        if (linearOffTick >= loopLength) {
            return NoteUtils::isPreferredWrapTailForHeadOff(tailOnTick, headOffTick, midiEvents,
                                                            pitch, channel, loopLength);
        }
        return NoteUtils::isPreferredWrapTailForHeadOff(tailOnTick, headOffTick, midiEvents, pitch,
                                                        channel, loopLength);
    };
    midiEvents.erase(std::remove_if(midiEvents.begin(), midiEvents.end(), isStaleWrapHead),
                     midiEvents.end());
}

NOTE_EDIT_MEM bool stillOpenTailAfterMove(uint32_t newStart, uint32_t noteLen, uint32_t loopLength) {
    if (loopLength == 0) {
        return false;
    }
    return (newStart + noteLen) >= loopLength;
}

NOTE_EDIT_MEM bool isWrapHeadOffForTailOn(const MidiEventVec& midiEvents, MidiEvent* noteOnEvent,
                            MidiEvent* noteOffEvent, uint8_t channel, uint8_t pitch,
                            uint32_t loopLength) {
    if (!noteOnEvent || !noteOffEvent || loopLength == 0) {
        return false;
    }
    const uint32_t headOffTick = storageTickToDisplayPhase(noteOffEvent->tick, loopLength);
    return NoteUtils::isPreferredWrapTailForHeadOff(noteOnEvent->tick, headOffTick, midiEvents,
                                                    pitch, channel, loopLength);
}

NOTE_EDIT_MEM uint32_t resolveMovingNoteLengthTicks(MidiEventVec& midiEvents, uint8_t channel,
                                      uint8_t pitch, uint32_t startTick,
                                      uint32_t fallbackEndTick, uint32_t loopLength,
                                      NoteId movingNoteId) {
    if (loopLength == 0) {
        return 0;
    }
    if (movingNoteId != kInvalidNoteId) {
        NoteBaseline linearSpan;
        if (findLinearNoteSpanForNoteId(midiEvents, movingNoteId, channel, linearSpan, startTick,
                                        loopLength) &&
            linearSpan.endTick > linearSpan.startTick) {
            if (linearSpan.endTick <= linearSpan.startTick + loopLength) {
                return linearSpan.endTick - linearSpan.startTick;
            }
            return calculateNoteLength(linearSpan.startTick, linearSpan.endTick, loopLength);
        }
    }
    if (MidiEvent* noteOffEvent = findNoteOffForNoteOnAtStart(
            midiEvents, channel, pitch, startTick, movingNoteId, loopLength)) {
        const uint32_t pairedEnd = noteOffEvent->tick;
        if (pairedEnd > startTick && pairedEnd <= startTick + loopLength) {
            return pairedEnd - startTick;
        }
        return calculateNoteLength(startTick, pairedEnd, loopLength);
    }
    const uint32_t displayFallbackEnd = storageTickToDisplayPhase(fallbackEndTick, loopLength);
    if (fallbackEndTick > startTick && fallbackEndTick <= startTick + loopLength) {
        return fallbackEndTick - startTick;
    }
    return calculateNoteLength(startTick, displayFallbackEnd, loopLength);
}

}  // namespace

NOTE_EDIT_MEM MidiEvent* findNoteOnForOverlapTarget(MidiEventVec& midiEvents, uint8_t channel,
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

NOTE_EDIT_MEM MidiEvent* findNoteOffForOverlapShorten(MidiEventVec& midiEvents, MidiEvent* noteOn,
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
    const uint32_t displayOff = storageTickToDisplayPhase(off->tick, loopLength);
    if (displayOff == expectedOffTick) {
        return off;
    }
    logger.log(CAT_MIDI, LOG_DEBUG,
               "LIFO pair off tick mismatch for shorten: pitch=%d, on@%lu, paired_off@%lu, "
               "expected_off=%lu",
               dn.note, noteOn->tick, off->tick, expectedOffTick);
    return nullptr;
}

NOTE_EDIT_MEM void applyShortenOrDelete(MidiEventVec& midiEvents,
                         const std::vector<std::pair<NoteUtils::DisplayNote, uint32_t>>& notesToShorten,
                         const std::vector<NoteUtils::DisplayNote>& notesToDelete,
                         EditManager& manager,
                         uint8_t channel,
                         uint32_t loopLength,
                         NoteUtils::EventIndexMap& onIndex,
                         NoteUtils::EventIndexMap& offIndex) {
    for (const auto& [dn, newEnd] : notesToShorten) {
        const NoteId noteId = findBaselineNoteIdForDisplay(editFocus(manager), dn);
        NoteBaseline linearSpan{};
        const bool hasLinearSpan =
            resolveLinearNoteSpanForOverlap(editFocus(manager), midiEvents, channel, dn,
                                            linearSpan, loopLength);
        uint32_t currentOffTick = dn.endTick;
        if (hasLinearSpan) {
            currentOffTick = linearSpan.endTick;
        }
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
                      dn.note, dn.startTick, currentOffTick, newEnd);
        }

        NoteUtils::DisplayNote shortenTarget = dn;
        if (hasLinearSpan) {
            shortenTarget.startTick = linearSpan.startTick;
            shortenTarget.endTick = currentOffTick;
        }
        MidiEvent* noteOnToShorten =
            findNoteOnForOverlapTarget(midiEvents, channel, shortenTarget, noteId);
        MidiEvent* noteOffToShorten = findNoteOffForOverlapShorten(
            midiEvents, noteOnToShorten, shortenTarget, currentOffTick, loopLength);
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
        NoteBaseline linearSpan{};
        NoteUtils::DisplayNote deleteTarget = dn;
        if (resolveLinearNoteSpanForOverlap(editFocus(manager), midiEvents, channel, dn,
                                            linearSpan, loopLength)) {
            deleteTarget.startTick = linearSpan.startTick;
            deleteTarget.endTick = linearSpan.endTick;
        }
        MidiEvent* noteOnToDelete =
            findNoteOnForOverlapTarget(midiEvents, channel, deleteTarget, noteId);
        if (noteOnToDelete == nullptr) {
            for (auto& evt : midiEvents) {
                if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0 &&
                    evt.data.noteData.note == deleteTarget.note &&
                    evt.tick == deleteTarget.startTick) {
                    noteOnToDelete = &evt;
                    break;
                }
            }
        }

        MidiEvent* noteOffToDelete = nullptr;
        if (noteOnToDelete != nullptr) {
            noteOffToDelete = findCorrespondingNoteOff(
                midiEvents, noteOnToDelete, deleteTarget.note, noteOnToDelete->tick,
                deleteTarget.endTick);
            if (noteOffToDelete != nullptr) {
                const uint32_t pairedLength = calculateNoteLength(
                    noteOnToDelete->tick, noteOffToDelete->tick, loopLength);
                const uint32_t expectedLength = calculateNoteLength(
                    deleteTarget.startTick, deleteTarget.endTick, loopLength);
                if (pairedLength != expectedLength) {
                    logger.log(CAT_MIDI, LOG_DEBUG,
                              "LIFO pair length mismatch for delete: pitch=%d, start=%lu, "
                              "paired_end=%lu, expected_end=%lu",
                              deleteTarget.note, deleteTarget.startTick,
                              noteOffToDelete->tick, deleteTarget.endTick);
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
                const uint32_t hiddenStart = noteOnToDelete->tick;
                const uint32_t hiddenEnd = noteOffToDelete->tick;
                const NoteId hiddenId =
                    noteId != kInvalidNoteId ? noteId : noteOnToDelete->noteId;
                if (hiddenId != kInvalidNoteId) {
                    markOverlapHiddenFromPair(manager, hiddenId, *noteOnToDelete, *noteOffToDelete);
                } else {
                    markOverlapHidden(manager, channel, deleteTarget);
                }
                logger.log(CAT_MIDI, LOG_DEBUG, "Stored hidden overlap note: pitch=%d, start=%lu, end=%lu",
                          dn.note, hiddenStart, hiddenEnd);
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
                      deleteTarget.note, deleteTarget.startTick, deleteTarget.endTick);
        }
    }
}

NOTE_EDIT_MEM void finalReconstructAndSelect(Track& track,
                              MidiEventVec& midiEvents,
                              EditManager& manager,
                              uint8_t movingNotePitch,
                              uint32_t newStart,
                              uint32_t newEnd,
                              uint32_t loopLength,
                              uint32_t selectedTick,
                              bool refreshPlaybackPreview) {
    NoteUtils::sortMidiEventsChronologically(midiEvents);
    int newSelectedIdx = -1;
    const uint32_t displayNewEnd = storageTickToDisplayPhase(newEnd, loopLength);

    if (manager.isNoteEditActive()) {
        const NoteEditFocus& focus = manager.getEditSession().focus;
        const EditorSelection& selection = manager.getNoteEditSessionState().selection;
        const NoteUtils::DisplayNoteVec filtered = filterSelectableDisplayNotes(
            midiEvents, focus, track.getMidiChannel(), loopLength);

        const uint32_t loopStartTick = manager.noteEditLoopStartTick(track);
        const bool lengthBracket = manager.isLengthBracketEditActive();
        const NoteEditKind sessionKind = manager.getNoteEditSessionState().kind;
        const bool geometryMutation =
            isGeometryEditKind(sessionKind) && sessionKind != NoteEditKind::Select;
        if (editorSelectionHasNote(selection)) {
            if (geometryMutation && focus.active &&
                focus.movingNoteId == selection.primaryNote) {
                const uint32_t storageBracketTick =
                    lengthBracket ? focus.last.endTick : focus.last.startTick;
                const uint32_t displayBracket = bracketDisplayTickFromStorage(
                    storageBracketTick, loopStartTick, loopLength);
                newSelectedIdx = filteredDisplayNoteIndexForNoteIdAndStart(
                    filtered, focus.movingNoteId, displayBracket, loopStartTick, loopLength);
            }
            if (newSelectedIdx < 0) {
                newSelectedIdx = NoteEditDisplaySnapshot::filteredDisplayNoteIndexForSelection(
                    selection, filtered, loopStartTick, loopLength, lengthBracket);
            }
            if (newSelectedIdx < 0 && focus.active &&
                focus.movingNoteId == selection.primaryNote) {
                EditorSelection retrySelection = selection;
                const bool lengthBracket = manager.isLengthBracketEditActive();
                const uint32_t storageBracketTick =
                    lengthBracket ? focus.last.endTick : focus.last.startTick;
                retrySelection.selectedTick = bracketDisplayTickFromStorage(
                    storageBracketTick, loopStartTick, loopLength);
                newSelectedIdx = NoteEditDisplaySnapshot::filteredDisplayNoteIndexForSelection(
                    retrySelection, filtered, loopStartTick, loopLength, lengthBracket);
                if (newSelectedIdx >= 0) {
                    manager.applySelectionFromGeometryEdit(track, retrySelection.selectedTick,
                                                           retrySelection.primaryNote);
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

    const bool noteEditActive = manager.isNoteEditActive();
    bool geometrySelectionFromFocus = false;
    if (noteEditActive) {
        const NoteEditKind sessionKind = manager.getNoteEditSessionState().kind;
        const bool geometryMutation =
            isGeometryEditKind(sessionKind) && sessionKind != NoteEditKind::Select;
        const NoteEditFocus& focus = manager.getEditSession().focus;
        const EditorSelection& selection = manager.getNoteEditSessionState().selection;
        geometrySelectionFromFocus =
            geometryMutation && focus.active && editorSelectionHasNote(selection) &&
            focus.movingNoteId == selection.primaryNote;
    }

    if (!geometrySelectionFromFocus) {
        if (newSelectedIdx >= 0) {
            const int oldSelectedIdx = manager.getSelectedNoteIdx();
            manager.setSelectedNoteIdx(newSelectedIdx);
            logger.log(CAT_MIDI, LOG_DEBUG, "Updated selectedNoteIdx: %d -> %d (note at new position)",
                       oldSelectedIdx, newSelectedIdx);
            if (noteEditActive &&
                editorSelectionHasNote(manager.getNoteEditSessionState().selection)) {
                manager.setSelectedTick(manager.getNoteEditSessionState().selection.selectedTick);
            } else {
                manager.setSelectedTick(selectedTick);
            }
        } else if (noteEditActive) {
            if (editorSelectionHasNote(manager.getNoteEditSessionState().selection)) {
                logger.log(CAT_MIDI, LOG_DEBUG,
                           "Keeping selectedNoteIdx %d (moving note not in filtered list)",
                           manager.getSelectedNoteIdx());
                manager.setSelectedTick(manager.getNoteEditSessionState().selection.selectedTick);
            } else {
                logger.log(CAT_MIDI, LOG_DEBUG, "Warning: Could not find moved note in filtered list");
                manager.setSelectedTick(selectedTick);
            }
        } else {
            logger.log(CAT_MIDI, LOG_DEBUG, "Warning: Could not find moved note in filtered list");
            manager.setSelectedTick(selectedTick);
        }
        manager.syncSelectedNoteIdxToFilteredInventory(track);
    } else {
        const NoteEditFocus& focus = manager.getEditSession().focus;
        const uint32_t loopStartTick = manager.noteEditLoopStartTick(track);
        const bool lengthBracket = manager.isLengthBracketEditActive();
        const uint32_t storageBracketTick =
            lengthBracket ? focus.last.endTick : focus.last.startTick;
        const uint32_t displayBracket = bracketDisplayTickFromStorage(
            storageBracketTick, loopStartTick, loopLength);
        manager.applySelectionFromGeometryEdit(track, displayBracket, focus.movingNoteId);
    }
    track.invalidateCaches(refreshPlaybackPreview);
#ifndef PIO_UNIT_TEST_NATIVE
    displayManager.requestNoteInfoRefresh(track);
#endif
}

namespace {

NOTE_EDIT_MEM bool applySimplePitchChange(MidiEventVec& midiEvents, EditManager& manager,
                                          Track& track, NoteEditFocus& focus, uint8_t channel,
                                          uint8_t currentNoteValue, uint8_t newNoteValue,
                                          uint32_t noteStart, uint32_t noteEnd,
                                          uint32_t loopLength) {
  if (focus.movingNoteId != kInvalidNoteId) {
    NoteBaseline moverSpan;
    if (findLinearNoteSpanForNoteId(midiEvents, focus.movingNoteId, channel, moverSpan,
                                    focus.last.startTick, loopLength)) {
      noteStart = moverSpan.startTick;
      noteEnd = moverSpan.endTick;
      focus.last.startTick = moverSpan.startTick;
      focus.last.endTick = moverSpan.endTick;
    }
  }

  const uint32_t displayEndForResolve = storageTickToDisplayPhase(noteEnd, loopLength);
  MidiEvent* noteOnEvent = findNoteOnForMovingNoteEdit(midiEvents, focus, channel, currentNoteValue,
                                                       noteStart, loopLength);
  if (noteOnEvent == nullptr) {
    return false;
  }
  MidiEvent* noteOffEvent = resolveMovingNoteOffForEdit(
      midiEvents, noteOnEvent, focus.movingNoteId, channel, currentNoteValue, noteOnEvent->tick,
      displayEndForResolve, loopLength);
  const bool openTailOnly =
      noteOffEvent == nullptr &&
      isOpenTailNoteAtLoopEnd(midiEvents, channel, currentNoteValue, noteOnEvent->tick,
                              displayEndForResolve, loopLength);
  if (!noteOffEvent && !openTailOnly) {
    return false;
  }

  noteOnEvent->data.noteData.note = newNoteValue;
  if (noteOffEvent != nullptr) {
    noteOffEvent->data.noteData.note = newNoteValue;
  }

  const uint32_t linearEnd = noteOffEvent != nullptr ? noteOffEvent->tick : noteEnd;
  noteEditFocusApplyPitch(focus, newNoteValue, noteStart, linearEnd, loopLength);
  focus.movingNoteRange.start = focus.last.startTick;
  focus.movingNoteRange.end = focus.last.endTick;
  focus.overlapNotes.erase(focus.movingNoteId);
  manager.bumpSessionPreviewRevision();
  manager.scheduleDeferredNoteEditDisplayRefresh();
  logger.log(CAT_MIDI, LOG_DEBUG, "Note value changed successfully (simple path): %d -> %d",
             currentNoteValue, newNoteValue);
  return true;
}

}  // namespace

NOTE_EDIT_MEM bool applyPitchChange(Track& track, EditManager& manager,
                      uint8_t currentNoteValue, uint8_t newNoteValue,
                      uint32_t& noteStart, uint32_t& noteEnd, bool refreshPlaybackPreview) {
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

    const uint8_t channel = track.getMidiChannel();
    if (canApplySimplePitchChange(midiEvents, focus, channel, currentNoteValue, newNoteValue,
                                  noteStart, noteEnd, loopLength) &&
        applySimplePitchChange(midiEvents, manager, track, focus, channel, currentNoteValue,
                               newNoteValue, noteStart, noteEnd, loopLength)) {
        (void)refreshPlaybackPreview;
        return true;
    }

    restoreOverlapNotesForPitchLaneClear(midiEvents, manager, track.getMidiChannel(),
                                         currentNoteValue, loopLength);
    track.invalidateCaches(refreshPlaybackPreview);

    if (focus.active && focus.movingNoteId != kInvalidNoteId) {
        NoteBaseline moverSpan;
        if (findLinearNoteSpanForNoteId(midiEvents, focus.movingNoteId, channel, moverSpan,
                                        focus.last.startTick, loopLength)) {
            noteStart = moverSpan.startTick;
            noteEnd = moverSpan.endTick;
            focus.last.startTick = moverSpan.startTick;
            focus.last.endTick = moverSpan.endTick;
        }
    }
    std::vector<NoteUtils::DisplayNote> notes;
    if (focus.active) {
        const std::vector<NoteUtils::DisplayNote> reconstructed =
            NoteUtils::reconstructNotes(midiEvents, loopLength);
        notes.assign(reconstructed.begin(), reconstructed.end());
    } else {
        const auto& cached = track.getCachedNotes();
        notes.assign(cached.begin(), cached.end());
    }
    const NoteEditFocus& focusConst = editFocus(manager);
    const bool preserveInnerNotes = focusConst.active;
    const uint32_t movingNoteStart = focusConst.movingNoteRange.start;
    const uint32_t movingNoteEnd = movingNoteRangeDisplayEnd(focusConst, loopLength);

    // Merge suffix-adjacent same-target-pitch notes into the moving note end before overlap
    // resolution. Prefix spans under mover start are shortened via findOverlaps (deleted only
    // when trim < 16th; adjacent touch is not overlap). Skip inner overlap notes inside the
    // moving note range.
    // Compare linear storage ticks only — display start/end must not be mixed with linear noteEnd.
    std::vector<NoteUtils::DisplayNote> adjacentToDelete;
    bool mergedAdjacent = true;
    while (mergedAdjacent) {
        mergedAdjacent = false;
        if (focus.active) {
            const std::vector<NoteUtils::DisplayNote> reconstructed =
                NoteUtils::reconstructNotes(midiEvents, loopLength);
            notes.assign(reconstructed.begin(), reconstructed.end());
        } else {
            const auto& cached = track.getCachedNotes();
            notes.assign(cached.begin(), cached.end());
        }
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
            if (note.endTick < note.startTick) {
                logger.log(CAT_MIDI, LOG_DEBUG,
                           "Skipping adjacent merge for display wrap projection segment: "
                           "pitch=%d, start=%lu, end=%lu",
                           note.note, note.startTick, note.endTick);
                continue;
            }
            if (note.noteId != focusConst.movingNoteId) {
                const auto overlapIt = focusConst.overlapNotes.find(note.noteId);
                if (overlapIt != focusConst.overlapNotes.end() &&
                    overlapIt->second.state != OverlapNoteStoreState::Visible) {
                    logger.log(CAT_MIDI, LOG_DEBUG,
                               "Skipping adjacent merge for overlap scratch note: "
                               "pitch=%d, start=%lu, end=%lu",
                               note.note, note.startTick, note.endTick);
                    continue;
                }
            }
            NoteBaseline adjacentLinear;
            if (!findLinearNoteSpanForNoteId(midiEvents, note.noteId, channel, adjacentLinear,
                                             note.startTick, loopLength)) {
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
        track.invalidateCaches(false);
        if (focus.active) {
            const std::vector<NoteUtils::DisplayNote> reconstructed =
                NoteUtils::reconstructNotes(midiEvents, loopLength);
            notes.assign(reconstructed.begin(), reconstructed.end());
        } else {
            const auto& cached = track.getCachedNotes();
            notes.assign(cached.begin(), cached.end());
        }
        logger.log(CAT_MIDI, LOG_DEBUG,
                   "Merged %zu suffix-adjacent same-pitch notes into moving note range %lu-%lu "
                   "before pitch change",
                   adjacentToDelete.size(), noteStart, noteEnd);
    }

    std::set<std::pair<uint32_t, uint32_t>> restoredNotePositions;

    bool overlapStructureChanged = !adjacentToDelete.empty();

    std::vector<NoteUtils::DisplayNote> otherNotesOfTargetPitch;
    for (const auto& note : notes) {
        if (note.note != newNoteValue) {
            continue;
        }
        if (focusConst.movingNoteId != kInvalidNoteId &&
            note.noteId == focusConst.movingNoteId) {
            continue;
        }
        if (note.noteId != kInvalidNoteId && note.endTick < note.startTick) {
            logger.log(CAT_MIDI, LOG_DEBUG,
                       "Skipping pitch overlap on display wrap projection segment: "
                       "pitch=%d, start=%lu, end=%lu",
                       note.note, note.startTick, note.endTick);
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
                  "Found %zu notes of target pitch %d, running geometry pipeline",
                  otherNotesOfTargetPitch.size(), newNoteValue);
    }

    const NoteBaseline priorLatch{currentNoteValue, focus.last.velocity, noteStart, noteEnd};
    const NoteBaseline editedSpan{newNoteValue, focus.last.velocity, noteStart, noteEnd};
    if (runEditSessionGeometryPipelineForCausingNote(track, manager, focus.movingNoteId, editedSpan,
                                                     priorLatch, std::nullopt, newNoteValue,
                                                     false)) {
      overlapStructureChanged = true;
      noteStart = focus.last.startTick;
      noteEnd = focus.last.endTick;
    }

    const uint32_t displayEndForResolve = storageTickToDisplayPhase(noteEnd, loopLength);

    // Resolve the note-on, then its STRUCTURALLY PAIRED note-off (not an independent
    // tick scan): two same-pitch notes can share a start or end tick when overlapping,
    // and an independent scan would repitch one event of an overlap note and orphan a note.
    bool noteOnUpdated = false;
    bool noteOffUpdated = false;
    MidiEvent* noteOnEvent = findNoteOnForMovingNoteEdit(midiEvents, focus, channel, newNoteValue,
                                                         noteStart, loopLength);
    if (noteOnEvent == nullptr) {
      noteOnEvent = findNoteOnForMovingNoteEdit(midiEvents, focus, channel, currentNoteValue,
                                                noteStart, loopLength);
    }
    MidiEvent* noteOffEvent = nullptr;
    if (noteOnEvent) {
        noteOffEvent = resolveMovingNoteOffForEdit(
            midiEvents, noteOnEvent, focus.movingNoteId, channel, newNoteValue, noteOnEvent->tick,
            displayEndForResolve, loopLength);
        const bool openTailOnly =
            !noteOffEvent &&
            isOpenTailNoteAtLoopEnd(midiEvents, channel, currentNoteValue, noteOnEvent->tick,
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
    focus.movingNoteRange.start = focus.last.startTick;
    focus.movingNoteRange.end = focus.last.endTick;
    if (focus.movingNoteId != kInvalidNoteId) {
        focus.overlapNotes.erase(focus.movingNoteId);
    }
    if (focus.movingNoteId != kInvalidNoteId) {
        const uint32_t loopStartTick = manager.noteEditLoopStartTick(track);
        const bool lengthBracket = manager.isLengthBracketEditActive();
        const uint32_t storageBracket =
            lengthBracket ? focus.last.endTick : focus.last.startTick;
        const uint32_t bracketDisplay =
            bracketDisplayTickFromStorage(storageBracket, loopStartTick, loopLength);
        manager.applySelectionFromGeometryEdit(track, bracketDisplay, focus.movingNoteId);
        if (!overlapStructureChanged) {
            NoteUtils::sortMidiEventsChronologically(midiEvents);
            track.invalidateCaches(refreshPlaybackPreview);
#ifndef PIO_UNIT_TEST_NATIVE
            displayManager.requestNoteInfoRefresh(track);
#endif
        } else {
            finalReconstructAndSelect(track, midiEvents, manager, newNoteValue, noteStart,
                                      focus.last.endTick, loopLength, bracketDisplay,
                                      refreshPlaybackPreview);
        }
        return true;
    }
    finalReconstructAndSelect(track, midiEvents, manager, newNoteValue, noteStart,
                              focus.last.endTick, loopLength, noteStart, refreshPlaybackPreview);
    return true;
}

NOTE_EDIT_MEM bool moveNoteWithOverlapHandling(Track& track, EditManager& manager,
                                const NoteUtils::DisplayNote& currentNote,
                                uint32_t targetTick, int delta) {
    // Session store when a note-edit session is active (matches move/length live paths).
    auto& midiEvents = track.editAwareMidiEvents();
    const uint32_t loopLength = manager.noteEditLoopLengthTicks(track);

    logger.log(CAT_MIDI, LOG_DEBUG, "NoteMovementUtils::moveNoteWithOverlapHandling called: targetTick=%lu, delta=%d", targetTick, delta);

    manager.ensureNoteEditFocusForLiveEdit(track, currentNote);
    const uint8_t channel = track.getMidiChannel();
    const NoteEditFocus& focus = editFocus(manager);
    
    if (loopLength == 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Loop length is 0, cannot move notes");
        return false;
    }

    // If there's no actual movement, just update the bracket position and return
    if (delta == 0) {
        logger.log(CAT_MIDI, LOG_DEBUG, "No movement (delta=0), just updating bracket position to %lu", targetTick);
        manager.setSelectedTick(targetTick);
        return false;
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
    
    uint32_t displayCurrentEnd = storageTickToDisplayPhase(currentEnd, loopLength);
    if (focus.active && focus.last.pitch == movingNotePitch &&
        focus.last.startTick == currentStart &&
        focus.last.endTick > currentEnd) {
        currentEnd = focus.last.endTick;
        displayCurrentEnd = storageTickToDisplayPhase(currentEnd, loopLength);
    }
    const NoteId movingNoteId =
        focus.active ? focus.movingNoteId : kInvalidNoteId;
    if (focus.active) {
        evictOverlapScratchForSelectedNote(editFocus(manager), movingNoteId);
    }
    // Prefer focus.last length during an active edit driver. Live-store pairing can briefly
    // mis-resolve same-pitch neighbor offs (noteId lives on note-on only) before Shorten
    // applies; session_20260804_210819 collapsed 190 → 4 at neighbor end 815.
    uint32_t noteLen = 0;
    if (focus.active && currentEnd > currentStart) {
        if (currentEnd <= currentStart + loopLength) {
            noteLen = currentEnd - currentStart;
        } else {
            noteLen = calculateNoteLength(currentStart, currentEnd, loopLength);
        }
    } else {
        noteLen = resolveMovingNoteLengthTicks(midiEvents, channel, movingNotePitch, currentStart,
                                               currentEnd, loopLength, movingNoteId);
    }
    uint32_t newStart = targetTick;
    uint32_t newEnd = newStart + noteLen;
    uint32_t displayNewEnd = storageTickToDisplayPhase(newEnd, loopLength);
    
    logger.log(CAT_MIDI, LOG_DEBUG, "Movement: start %lu->%lu, end actual %lu (display %lu), length=%lu", 
              currentStart, newStart, newEnd, displayNewEnd, noteLen);

    const uint32_t linearNewEnd =
        NoteMovementUtils::linearStorageOffTickForSpanEnd(newStart, noteLen);
    const uint32_t displayEndForBracket =
        displayFocusEndTickForMove(newStart, noteLen, loopLength);

    const NoteBaseline editedSpan{movingNotePitch, focus.last.velocity, newStart, linearNewEnd};
    const bool pipelineApplied =
        runEditSessionGeometryPipelineForCausingNote(track, manager, focus.movingNoteId, editedSpan,
                                                     focus.last, targetTick, movingNotePitch,
                                                     false);

    bool movedNoteEvents = pipelineApplied;
    if (pipelineApplied) {
      MidiEvent* noteOnEvent = findNoteOnForMovingNoteEdit(midiEvents, editFocus(manager), channel,
                                                           movingNotePitch, newStart, loopLength);
      if (noteOnEvent != nullptr) {
        scrubStaleWrapHeadOffsForMovedNote(midiEvents, channel, movingNotePitch, newStart,
                                           linearNewEnd, loopLength, noteOnEvent->noteId);
      }
      const uint32_t displayEndForTelemetry =
          storageTickToDisplayPhase(linearNewEnd, loopLength);
      SC_DNTE(movingNotePitch, newStart, newStart,
              displayEndForTelemetry >= newStart ? displayEndForTelemetry - newStart : noteLen,
              manager.getSelectedNoteIdx());
    } else {
      logger.log(CAT_MIDI, LOG_DEBUG,
                  "Warning: edit session geometry pipeline did not apply move for pitch=%u "
                  "start=%lu",
                  movingNotePitch, newStart);
    }

    const uint32_t loopStartTick = manager.noteEditLoopStartTick(track);
    const uint32_t bracketDisplay =
        bracketDisplayTickFromStorage(newStart, loopStartTick, loopLength);
    if (movedNoteEvents && focus.active && focus.movingNoteId != kInvalidNoteId) {
        manager.applySelectionFromGeometryEdit(track, bracketDisplay, focus.movingNoteId);
    }

    finalReconstructAndSelect(track, midiEvents, manager, movingNotePitch, newStart,
                              displayEndForBracket, loopLength, bracketDisplay, false);
    return movedNoteEvents;
}

NOTE_EDIT_MEM void changeLengthWithOverlapHandling(Track& track, EditManager& manager,
                                     const NoteUtils::DisplayNote& currentNote,
                                     uint32_t targetEndTick) {
    auto& midiEvents = track.editAwareMidiEvents();
    uint32_t loopLength = track.getLoopLength();
    manager.ensureNoteEditFocusForLiveEdit(track, currentNote);
    manager.syncNoteEditFocusLastFromSessionStore(track);
    const uint8_t channel = track.getMidiChannel();
    const NoteEditFocus& focus = editFocus(manager);

    if (focus.active && focus.movingNoteId != kInvalidNoteId) {
        evictOverlapScratchForSelectedNote(editFocus(manager), focus.movingNoteId);
    }

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
        const uint32_t loopStartTick = manager.noteEditLoopStartTick(track);
        manager.setSelectedTick(
            bracketDisplayTickFromStorage(targetEndTick, loopStartTick, loopLength));
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

    const uint32_t displayNewEnd = storageTickToDisplayPhase(newEnd, loopLength);

    logger.log(CAT_MIDI, LOG_DEBUG,
              "Length change with overlap: pitch=%d, start=%lu, end %lu->%lu",
              notePitch, noteStart, currentEnd, targetEndTick);

    const uint32_t noteLen =
        NoteMovementUtils::calculateNoteLength(newStart, newEnd, loopLength);
    const uint32_t linearNewEnd =
        NoteMovementUtils::linearStorageOffTickForSpanEnd(newStart, noteLen);

    const NoteBaseline editedSpan{notePitch, focus.last.velocity, newStart, linearNewEnd};
    const bool pipelineApplied =
        runEditSessionGeometryPipelineForCausingNote(track, manager, focus.movingNoteId,
                                                     editedSpan, focus.last, std::nullopt,
                                                     notePitch, false);

    if (!pipelineApplied) {
        logger.log(CAT_MIDI, LOG_DEBUG,
                  "Warning: edit session geometry pipeline did not apply length for pitch=%d "
                  "start=%lu end=%lu",
                  notePitch, noteStart, newEnd);
    }

    const uint32_t loopStartTick = manager.noteEditLoopStartTick(track);
    const uint32_t lengthBracketDisplay =
        bracketDisplayTickFromStorage(newEnd, loopStartTick, loopLength);
  if (pipelineApplied && focus.active && focus.movingNoteId != kInvalidNoteId) {
    manager.applySelectionFromGeometryEdit(track, lengthBracketDisplay, focus.movingNoteId);
  }
  manager.setSelectedTick(lengthBracketDisplay);

    finalReconstructAndSelect(track, midiEvents, manager, notePitch, newStart, newEnd, loopLength,
                              lengthBracketDisplay, false);

    NoteUtils::orderSamePitchNoteOffsForLifo(midiEvents, track.getMidiChannel(), notePitch);
}

// Extend shortened notes dynamically
NOTE_EDIT_MEM void extendShortenedNotes(MidiEventVec& midiEvents,
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

NOTE_EDIT_MEM bool applyNoteEditChange(Track& track, EditManager& manager, NoteEditChangeKind kind,
                         const NoteUtils::DisplayNote& currentNote, uint32_t targetTick,
                         int delta, uint32_t targetEndTick, uint8_t currentPitch,
                         uint8_t newPitch, uint32_t& inOutStart, uint32_t& inOutEnd,
                         bool refreshPlaybackPreview) {
    switch (kind) {
        case NoteEditChangeKind::Move:
            return moveNoteWithOverlapHandling(track, manager, currentNote, targetTick, delta);
        case NoteEditChangeKind::Length:
            changeLengthWithOverlapHandling(track, manager, currentNote, targetEndTick);
            return true;
        case NoteEditChangeKind::Pitch:
            inOutStart = currentNote.startTick;
            inOutEnd = currentNote.endTick;
            return applyPitchChange(track, manager, currentPitch, newPitch, inOutStart,
                                    inOutEnd, refreshPlaybackPreview);
    }
    return false;
}

} // namespace NoteMovementUtils 