//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"
#include "NoteEditFocusInternal.h"
#include "NoteEditCurrentState.h"

#include <vector>

#include "Utils/IntervalProjection.h"
#include "Utils/NoteEditDisplaySnapshot.h"
#include "Utils/NoteEditMem.h"
#include "Utils/NoteMovementWrap.h"
#include "Utils/NoteUtils.h"

namespace {

NOTE_EDIT_MEM bool noteBaselineMatches(const NoteBaseline& left, const NoteBaseline& right) {
  return left.pitch == right.pitch && left.velocity == right.velocity &&
         left.startTick == right.startTick && left.endTick == right.endTick;
}

}  // namespace

template <typename Alloc>
NOTE_EDIT_MEM bool isLiveEditDriverValid(const EditorSelection& selection,
                                         const NoteEditFocus& focus,
                                         const std::vector<MidiEvent, Alloc>& sessionStore,
                                         uint8_t channel, uint32_t loopLength) {
  if (!focus.active || focus.movingNoteId == kInvalidNoteId) {
    return false;
  }
  if (!editorSelectionMatchesDriverNote(selection, focus.movingNoteId)) {
    return false;
  }
  NoteBaseline storeSpan{};
  std::vector<MidiEvent, Alloc>& mutableStore =
      const_cast<std::vector<MidiEvent, Alloc>&>(sessionStore);
  if (!findLinearNoteSpanForNoteId(mutableStore, focus.movingNoteId, channel, storeSpan,
                                   UINT32_MAX, loopLength)) {
    return false;
  }
  return noteBaselineMatches(storeSpan, focus.last);
}

template bool isLiveEditDriverValid<InternalHeapFirstAllocator<MidiEvent>>(
    const EditorSelection&, const NoteEditFocus&, const MidiEventVec&, uint8_t, uint32_t);
template bool isLiveEditDriverValid<ExternalMemoryFirstAllocator<MidiEvent>>(
    const EditorSelection&, const NoteEditFocus&, const SessionMidiEventVec&, uint8_t,
    uint32_t);

NOTE_EDIT_MEM bool isLiveEditDriverValidFromCurrentState(const EditorSelection& selection,
                                                         const NoteEditFocus& focus,
                                                         const NoteEditCurrentState& currentState) {
  if (!focus.active || focus.movingNoteId == kInvalidNoteId) {
    return false;
  }
  if (!editorSelectionMatchesDriverNote(selection, focus.movingNoteId)) {
    return false;
  }
  NoteBaseline current{};
  if (!currentState.readCurrentSpan(focus.movingNoteId, current)) {
    return false;
  }
  if (!currentState.rowProjectsToStore(focus.movingNoteId)) {
    return false;
  }
  return noteBaselineMatches(current, focus.last);
}

NOTE_EDIT_MEM void syncNoteEditFocusLastFromCurrentState(NoteEditFocus& focus, NoteId primaryNote,
                                                         const NoteEditCurrentState& currentState) {
  if (!focus.active || focus.movingNoteId == kInvalidNoteId) {
    return;
  }
  if (primaryNote != kInvalidNoteId && primaryNote != focus.movingNoteId) {
    return;
  }
  NoteBaseline current{};
  if (!currentState.readCurrentSpan(focus.movingNoteId, current)) {
    return;
  }
  focus.last = current;
  focus.movingNoteRange.start = current.startTick;
  focus.movingNoteRange.end = current.endTick;
}

NOTE_EDIT_MEM bool isMacroCommitAlignedWithSelectTarget(NoteId selectNoteId,
                                                        uint32_t selectBracketTick,
                                                        const NoteEditFocus& focus,
                                                        uint32_t loopStartTick,
                                                        uint32_t loopLength, bool lengthBracket,
                                                        const NoteEditCurrentState* currentState) {
  if (!focus.active || focus.movingNoteId == kInvalidNoteId) {
    return true;
  }
  if (selectNoteId == kInvalidNoteId) {
    // Deselect seals pending geometry so committedSpan is current before participation clears
    // (session_20260808_013500: seal skipped, overlap repainted its pre-shorten length).
    // Only a mover span that disagrees with current state blocks the seal (014541).
    NoteBaseline moverCurrent{};
    if (currentState != nullptr && currentState->readCurrentSpan(focus.movingNoteId, moverCurrent)) {
      return moverCurrent.pitch == focus.last.pitch &&
             moverCurrent.startTick == focus.last.startTick &&
             moverCurrent.endTick == focus.last.endTick;
    }
    return !noteEditFocusHasPendingCommit(focus);
  }
  if (selectNoteId != focus.movingNoteId) {
    // Mover handoff: seal prior mover + overlap participants before rebuilding focus.
    return true;
  }
  const uint32_t storageBracketTick =
      lengthBracket ? focus.last.endTick : focus.last.startTick;
  const uint32_t driverDisplayBracket = NoteEditDisplaySnapshot::displayStartTickFromStorage(
      storageBracketTick, loopStartTick, loopLength);
  return selectBracketTick == driverDisplayBracket;
}

NOTE_EDIT_MEM uint32_t noteEditDisplayCacheFingerprint(const NoteEditFocus& focus,
                                                       const NoteEditCurrentState* currentState) {
  uint32_t fp = static_cast<uint32_t>(focus.changedOverlapNoteIds.size());
  fp ^= focus.active ? 0xA5A5A5A5u : 0u;
  fp ^= focus.last.startTick + (focus.last.endTick << 1);
  fp ^= static_cast<uint32_t>(focus.last.pitch) << 16;
  fp ^= static_cast<uint32_t>(focus.overlapNotes.size()) << 8;
  for (NoteId noteId : focus.changedOverlapNoteIds) {
    fp ^= static_cast<uint32_t>(noteId) * 0x9E3779B9u;
  }
  if (currentState != nullptr) {
    fp ^= static_cast<uint32_t>(currentState->rows().size()) << 12;
    for (const auto& [noteId, row] : currentState->rows()) {
      if (noteId == kInvalidNoteId) {
        continue;
      }
      fp ^= static_cast<uint32_t>(noteId) * 0x85EBCA6Bu;
      fp ^= static_cast<uint32_t>(row.presence) << 28;
      fp ^= static_cast<uint32_t>(row.overlapParticipation) << 24;
      fp ^= row.currentSpan.startTick + (row.currentSpan.endTick << 1);
      fp ^= row.committedSpan.startTick + (row.committedSpan.endTick << 2);
      fp ^= row.visibleOverlapShortenSealed ? 0x6C078965u : 0u;
    }
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
