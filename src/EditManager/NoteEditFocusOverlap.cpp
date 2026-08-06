//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteEditFocus.h"
#include "NoteEditFocusInternal.h"

#include <algorithm>
#include <vector>

#include "EditSessionLiveStoreSpan.h"
#include "Utils/NoteEditMem.h"

template <typename Alloc>
NOTE_EDIT_MEM void resolveOverlapNotesForPreCommit(std::vector<MidiEvent, Alloc>& sessionStoreEvents,
                                                   NoteEditFocus& focus, uint8_t channel,
                                                   uint32_t loopLength) {
  if (!focus.active || loopLength == 0) {
    return;
  }

  for (auto& [noteId, entry] : focus.overlapNotes) {
    (void)noteId;
    if (entry.state == OverlapNoteStoreState::Visible) {
      continue;
    }
    if (entry.state == OverlapNoteStoreState::Hidden) {
      eraseNotePairAtBaseline(sessionStoreEvents, channel, entry.baseline);
      if (entry.shortenedEndTick != 0) {
        eraseNoteEndpoint(sessionStoreEvents, channel, entry.baseline.pitch, entry.shortenedEndTick,
                          false);
      }
      continue;
    }
    if (entry.state == OverlapNoteStoreState::Shortened) {
      materializeShortenedOverlap(sessionStoreEvents, channel, entry, loopLength);
    }
  }
}

template void resolveOverlapNotesForPreCommit<InternalHeapFirstAllocator<MidiEvent>>(
    MidiEventVec&, NoteEditFocus&, uint8_t, uint32_t);
template void resolveOverlapNotesForPreCommit<ExternalMemoryFirstAllocator<MidiEvent>>(
    SessionMidiEventVec&, NoteEditFocus&, uint8_t, uint32_t);

NOTE_EDIT_MEM OverlapNote* findOverlapNoteEntry(NoteEditFocus& focus, NoteId noteId) {
  const auto it = focus.overlapNotes.find(noteId);
  return it == focus.overlapNotes.end() ? nullptr : &it->second;
}

NOTE_EDIT_MEM const OverlapNote* findOverlapNoteEntry(const NoteEditFocus& focus, NoteId noteId) {
  const auto it = focus.overlapNotes.find(noteId);
  return it == focus.overlapNotes.end() ? nullptr : &it->second;
}

NOTE_EDIT_MEM bool hasChangedOverlapNote(const NoteEditFocus& focus, NoteId noteId) {
  return std::find(focus.changedOverlapNoteIds.begin(), focus.changedOverlapNoteIds.end(),
                   noteId) != focus.changedOverlapNoteIds.end();
}

NOTE_EDIT_MEM void recordChangedOverlapNote(NoteEditFocus& focus, NoteId noteId) {
  if (noteId == kInvalidNoteId || hasChangedOverlapNote(focus, noteId)) {
    return;
  }
  focus.changedOverlapNoteIds.push_back(noteId);
}

NOTE_EDIT_MEM void forgetChangedOverlapNote(NoteEditFocus& focus, NoteId noteId) {
  const auto it = std::find(focus.changedOverlapNoteIds.begin(),
                            focus.changedOverlapNoteIds.end(), noteId);
  if (it != focus.changedOverlapNoteIds.end()) {
    focus.changedOverlapNoteIds.erase(it);
  }
}

NOTE_EDIT_MEM void applyCommittedOverlapUpdateToFocus(NoteEditFocus& focus, NoteId noteId,
                                                      const NoteBaseline& baseline) {
  if (noteId == kInvalidNoteId || noteId == focus.movingNoteId) {
    return;
  }
  focus.baselineMap[noteId] = baseline;
  focus.overlapNotes.erase(noteId);
  forgetChangedOverlapNote(focus, noteId);
}

NOTE_EDIT_MEM void clearCommittedOverlapDeleteIdsFromFocus(NoteEditFocus& focus,
                                                           const NoteIdList& noteIds) {
  for (NoteId noteId : noteIds) {
    if (noteId == kInvalidNoteId || noteId == focus.movingNoteId) {
      continue;
    }
    focus.baselineMap.erase(noteId);
    focus.overlapNotes.erase(noteId);
    forgetChangedOverlapNote(focus, noteId);
  }
}

NOTE_EDIT_MEM bool evictOverlapScratchForSelectedNote(NoteEditFocus& focus, NoteId selectedNoteId) {
  if (selectedNoteId == kInvalidNoteId) {
    return false;
  }
  return focus.overlapNotes.erase(selectedNoteId) > 0;
}

NOTE_EDIT_MEM uint32_t overlapNoteEffectiveEnd(const OverlapNote& entry) {
  if (entry.state == OverlapNoteStoreState::Shortened) {
    return entry.shortenedEndTick;
  }
  return entry.baseline.endTick;
}

NOTE_EDIT_MEM bool isMovingNoteOverlapScratchEntry(const NoteEditFocus& focus, NoteId noteId,
                                     const NoteBaseline& baseline) {
  if (!focus.active) {
    return false;
  }
  if (noteId != kInvalidNoteId && noteId == focus.movingNoteId) {
    return true;
  }
  return baseline.startTick == focus.commitBaseline.startTick &&
         baseline.pitch == focus.commitBaseline.pitch;
}

/// Phase 4 geometry pipeline hides/shortens without writing overlapNotes scratch. Leaving that
/// pitch must take the full path so RestoreNote can reinsert/extend from baselineMap.
NOTE_EDIT_FOCUS_INTERNAL_MEM bool baselineMapPitchLaneNeedsRestore(const NoteEditFocus& focus,
                                                    const MidiEventVec& liveStore, uint8_t channel,
                                                    uint8_t clearedPitch) {
  for (const auto& [noteId, baseline] : focus.baselineMap) {
    if (noteId == kInvalidNoteId || noteId == focus.movingNoteId) {
      continue;
    }
    if (baseline.pitch != clearedPitch) {
      continue;
    }
    NoteBaseline live{};
    if (!readLiveLinearSpan(liveStore, noteId, channel, live)) {
      return true;
    }
    if (live.pitch != baseline.pitch || live.startTick != baseline.startTick ||
        live.endTick != baseline.endTick) {
      return true;
    }
  }
  return false;
}

NOTE_EDIT_MEM bool canApplySimplePitchChange(MidiEventVec& sessionEvents, const NoteEditFocus& focus,
                                             uint8_t channel, uint8_t currentPitch,
                                             uint8_t targetPitch, uint32_t moverStart,
                                             uint32_t moverEnd, uint32_t loopLength) {
  if (!focus.active || focus.movingNoteId == kInvalidNoteId || loopLength == 0) {
    return false;
  }
  if (baselineMapPitchLaneNeedsRestore(focus, sessionEvents, channel, currentPitch)) {
    return false;
  }
  if (baselineMapPitchLaneNeedsRestore(focus, sessionEvents, channel, targetPitch)) {
    return false;
  }

  for (const MidiEvent& evt : sessionEvents) {
    if (!evt.isNoteOn() || evt.data.noteData.velocity == 0 ||
        evt.data.noteData.note != targetPitch || evt.noteId == kInvalidNoteId ||
        evt.noteId == focus.movingNoteId) {
      continue;
    }
    NoteBaseline linear;
    if (!findLinearNoteSpanForNoteId(sessionEvents, evt.noteId, channel, linear, evt.tick,
                                     loopLength)) {
      continue;
    }
    if (linear.startTick == moverEnd) {
      return false;
    }
    if (linearStorageSpansOverlapLocal(moverStart, moverEnd, linear.startTick, linear.endTick)) {
      return false;
    }
  }
  return true;
}

NOTE_EDIT_MEM void recordBaselinePitchLaneRestoreOverlapCandidates(NoteEditFocus& focus,
                                                                   const MidiEventVec& liveStore,
                                                                   uint8_t channel,
                                                                   uint8_t pitch) {
  for (const auto& [noteId, baseline] : focus.baselineMap) {
    if (noteId == kInvalidNoteId || noteId == focus.movingNoteId) {
      continue;
    }
    if (baseline.pitch != pitch) {
      continue;
    }
    NoteBaseline live{};
    if (!readLiveLinearSpan(liveStore, noteId, channel, live)) {
      recordChangedOverlapNote(focus, noteId);
      continue;
    }
    if (live.pitch != baseline.pitch || live.startTick != baseline.startTick ||
        live.endTick != baseline.endTick) {
      recordChangedOverlapNote(focus, noteId);
    }
  }
}

template <typename Alloc>
NOTE_EDIT_MEM void pruneOverlapNotesBeforePreCommit(NoteEditFocus& focus,
                                                    std::vector<MidiEvent, Alloc>& events,
                                                    uint8_t channel) {
  for (auto it = focus.overlapNotes.begin(); it != focus.overlapNotes.end();) {
    OverlapNote& entry = it->second;
    NoteBaseline linear;
    const bool hasLinear = entry.noteId != kInvalidNoteId &&
                           findLinearNoteSpanForNoteId(events, entry.noteId, channel, linear);
    if (hasLinear) {
      entry.baseline = linear;
    }

    bool erase = false;
    if (entry.state == OverlapNoteStoreState::Hidden) {
      if (hasLinear) {
        erase = true;
      }
    } else if (entry.state == OverlapNoteStoreState::Shortened) {
      if (entry.baseline.endTick < entry.baseline.startTick) {
        erase = true;
      } else if (hasLinear) {
        const uint32_t targetOff = entry.shortenedEndTick;
        if (targetOff == linear.endTick ||
            (targetOff + 1 >= linear.endTick && targetOff >= linear.startTick)) {
          erase = true;
        }
      }
    }

    if (erase) {
      it = focus.overlapNotes.erase(it);
    } else {
      ++it;
    }
  }
}

template void pruneOverlapNotesBeforePreCommit<InternalHeapFirstAllocator<MidiEvent>>(
    NoteEditFocus&, MidiEventVec&, uint8_t);
template void pruneOverlapNotesBeforePreCommit<ExternalMemoryFirstAllocator<MidiEvent>>(
    NoteEditFocus&, SessionMidiEventVec&, uint8_t);
