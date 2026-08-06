//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteEditFocus.h"
#include "NoteEditFocusInternal.h"

#include <vector>

#include "Utils/IntervalProjection.h"
#include "Utils/NoteEditMem.h"
#include "Utils/NoteMovementWrap.h"
#include "Utils/NoteUtils.h"

NOTE_EDIT_MEM uint32_t noteEditDisplayCacheFingerprint(const NoteEditFocus& focus) {
  uint32_t fp = static_cast<uint32_t>(focus.changedOverlapNoteIds.size());
  fp ^= focus.last.startTick + (focus.last.endTick << 1);
  fp ^= static_cast<uint32_t>(focus.last.pitch) << 16;
  fp ^= static_cast<uint32_t>(focus.overlapNotes.size()) << 8;
  for (NoteId noteId : focus.changedOverlapNoteIds) {
    fp ^= static_cast<uint32_t>(noteId) * 0x9E3779B9u;
  }
  return fp;
}

NOTE_EDIT_MEM uint32_t movingNoteRangeDisplayEnd(const NoteEditFocus& focus, uint32_t loopLength) {
  if (!focus.active || loopLength == 0) {
    return focus.movingNoteRange.end;
  }
  return IntervalProjection::tickPhaseInLoop(focus.movingNoteRange.end, 0, loopLength);
}

NOTE_EDIT_MEM bool isInnerOverlapNoteInMovingNoteRange(const NoteEditFocus& focus, uint8_t pitch,
                                         uint32_t noteStart, uint32_t noteEnd,
                                         uint32_t loopLength) {
  if (!focus.active || loopLength == 0) {
    return false;
  }
  if (pitch == focus.commitBaseline.pitch) {
    return false;
  }
  return NoteMovementUtils::isNoteWithinMovingNoteRange(
      noteStart, noteEnd, focus.movingNoteRange.start,
      movingNoteRangeDisplayEnd(focus, loopLength), loopLength);
}

template <typename Alloc>
NOTE_EDIT_MEM void rebuildNoteEditFocusFromStore(NoteEditFocus& focus,
                                   const std::vector<MidiEvent, Alloc>& loopMidiEvents,
                                   uint8_t channel, uint32_t loopLength,
                                   int selectedNoteIdx) {
  (void)channel;
  focus.clear();
  if (loopLength == 0) {
    return;
  }

  const NoteUtils::DisplayNoteVec notes =
      NoteUtils::reconstructDisplayNotes(loopMidiEvents, loopLength, false);

  if (selectedNoteIdx < 0 || selectedNoteIdx >= static_cast<int>(notes.size())) {
    return;
  }

  const NoteUtils::DisplayNote& selected = notes[static_cast<size_t>(selectedNoteIdx)];
  focus.movingNoteId = selected.noteId;
  std::vector<MidiEvent, Alloc> mutableEvents = loopMidiEvents;
  NoteBaseline linearBaseline;
  if (findLinearNoteSpanForNoteId(mutableEvents, selected.noteId, channel, linearBaseline,
                                  UINT32_MAX, loopLength)) {
    focus.commitBaseline = linearBaseline;
  } else {
    focus.commitBaseline = baselineFromDisplayNote(selected);
  }
  focus.baselineMap[focus.movingNoteId] = focus.commitBaseline;
  focus.movingNoteRange.start = focus.commitBaseline.startTick;
  focus.movingNoteRange.end = focus.commitBaseline.endTick;
  focus.last = focus.commitBaseline;
  focus.active = true;
}

template void rebuildNoteEditFocusFromStore<InternalHeapFirstAllocator<MidiEvent>>(
    NoteEditFocus&, const MidiEventVec&, uint8_t, uint32_t, int);
template void rebuildNoteEditFocusFromStore<ExternalMemoryFirstAllocator<MidiEvent>>(
    NoteEditFocus&, const SessionMidiEventVec&, uint8_t, uint32_t, int);

NOTE_EDIT_MEM void noteEditFocusApplyLengthEnd(NoteEditFocus& focus, uint32_t newEndTick) {
  if (!focus.active) {
    return;
  }
  focus.last.endTick = newEndTick;
  focus.movingNoteRange.end = newEndTick;
}

NOTE_EDIT_MEM void noteEditFocusApplyMoveEnd(NoteEditFocus& focus, uint32_t newStart, uint32_t newEnd) {
  if (!focus.active) {
    return;
  }
  focus.last.startTick = newStart;
  focus.last.endTick = newEnd;
  focus.movingNoteRange.start = newStart;
  focus.movingNoteRange.end = newEnd;
}

NOTE_EDIT_MEM void noteEditFocusApplyPitch(NoteEditFocus& focus, uint8_t newPitch, uint32_t start,
                             uint32_t end, uint32_t loopLength) {
  if (!focus.active || loopLength == 0) {
    return;
  }
  focus.last.pitch = newPitch;
  focus.last.startTick = start;
  focus.last.endTick = end;
  if (end >= focus.movingNoteRange.end || end > loopLength) {
    focus.movingNoteRange.end = end;
  }
}

NOTE_EDIT_MEM bool noteEditFocusHasPendingLengthChange(const NoteEditFocus& focus) {
  return focus.active && focus.last.endTick != focus.commitBaseline.endTick;
}

template <typename Alloc>
NOTE_EDIT_MEM bool noteEditFocusHasPendingBaselineMapDiff(
    const NoteEditFocus& focus, const std::vector<MidiEvent, Alloc>& sessionEvents,
    uint8_t channel, uint32_t loopLength) {
  if (!focus.active || loopLength == 0) {
    return false;
  }
  for (const auto& [noteId, baseline] : focus.baselineMap) {
    if (noteId == kInvalidNoteId || noteId == focus.movingNoteId) {
      continue;
    }
    NoteBaseline live{};
    const bool hasLive =
        readLiveBaselineForOverlapDiff(sessionEvents, noteId, baseline, channel, loopLength,
                                       focus.movingNoteId, live);
    if (!hasLive) {
      // Same rule as the pre-commit diff — an unresolved entry emits no row, so it must not
      // mark the session dirty either.
      if (hasChangedOverlapNote(focus, noteId)) {
        return true;
      }
      continue;
    }
    if (live.pitch != baseline.pitch) {
      continue;
    }
    if (!hasChangedOverlapNote(focus, noteId)) {
      continue;
    }
    if (live.startTick != baseline.startTick || live.endTick != baseline.endTick) {
      return true;
    }
  }
  return false;
}

template bool noteEditFocusHasPendingBaselineMapDiff<InternalHeapFirstAllocator<MidiEvent>>(
    const NoteEditFocus&, const MidiEventVec&, uint8_t, uint32_t);
template bool noteEditFocusHasPendingBaselineMapDiff<ExternalMemoryFirstAllocator<MidiEvent>>(
    const NoteEditFocus&, const SessionMidiEventVec&, uint8_t, uint32_t);

NOTE_EDIT_MEM bool noteEditFocusHasPendingCommit(const NoteEditFocus& focus) {
  if (!focus.active) {
    return false;
  }
  // Overlap hide/shorten pending is detected via noteEditFocusHasPendingBaselineMapDiff
  // (baselineMap vs live store) — not overlapNotes scratch (OpenSpec 4.5).
  if (focus.last.startTick != focus.commitBaseline.startTick) {
    return true;
  }
  if (focus.last.endTick != focus.commitBaseline.endTick) {
    return true;
  }
  return focus.last.pitch != focus.commitBaseline.pitch;
}
