//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteEditFocus.h"

#include <algorithm>
#include <unordered_set>

#include "EditSessionLiveStoreSpan.h"
#include "Utils/IntervalProjection.h"
#include "Utils/NoteMovementWrap.h"
#include "Utils/LoopTickNormalize.h"
#include "Utils/NoteUtils.h"
#include "Utils/NoteEditMem.h"

NOTE_EDIT_MEM NoteBaseline baselineFromDisplayNote(const NoteUtils::DisplayNote& dn) {
  return NoteBaseline{dn.note, dn.velocity, dn.startTick, dn.endTick};
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

NOTE_EDIT_MEM OverlapNote* findOverlapNoteEntry(NoteEditFocus& focus, NoteId noteId) {
  const auto it = focus.overlapNotes.find(noteId);
  return it == focus.overlapNotes.end() ? nullptr : &it->second;
}

NOTE_EDIT_MEM const OverlapNote* findOverlapNoteEntry(const NoteEditFocus& focus, NoteId noteId) {
  const auto it = focus.overlapNotes.find(noteId);
  return it == focus.overlapNotes.end() ? nullptr : &it->second;
}

NOTE_EDIT_MEM bool evictOverlapScratchForSelectedNote(NoteEditFocus& focus, NoteId selectedNoteId) {
  if (selectedNoteId == kInvalidNoteId) {
    return false;
  }
  return focus.overlapNotes.erase(selectedNoteId) > 0;
}

NOTE_EDIT_MEM NoteId findBaselineNoteIdForDisplay(const NoteEditFocus& focus,
                                    const NoteUtils::DisplayNote& dn) {
  if (dn.noteId != kInvalidNoteId && focus.baselineMap.find(dn.noteId) != focus.baselineMap.end()) {
    return dn.noteId;
  }
  for (const auto& [noteId, baseline] : focus.baselineMap) {
    if (baseline.pitch == dn.note && baseline.startTick == dn.startTick &&
        baseline.endTick == dn.endTick) {
      return noteId;
    }
  }
  return dn.noteId;
}

NOTE_EDIT_MEM NoteBaseline baselineForDisplayNote(const NoteEditFocus& focus,
                                    const NoteUtils::DisplayNote& dn) {
  const NoteId noteId = findBaselineNoteIdForDisplay(focus, dn);
  const auto it = focus.baselineMap.find(noteId);
  if (it != focus.baselineMap.end()) {
    return it->second;
  }
  return baselineFromDisplayNote(dn);
}

namespace {

NOTE_EDIT_MEM bool isDisplayWrappedBaseline(const NoteBaseline& baseline) {
  return baseline.endTick < baseline.startTick;
}

}  // namespace

NOTE_EDIT_MEM bool isPlausibleStorageSpan(uint32_t startTick, uint32_t endTick, uint32_t loopLength) {
  if (loopLength == 0) {
    return endTick > startTick;
  }
  if (endTick <= startTick) {
    return endTick + loopLength > startTick;
  }
  const uint32_t length = endTick - startTick;
  if (length > loopLength) {
    return false;
  }
  if (endTick <= loopLength) {
    return true;
  }
  return startTick > loopLength / 2 && endTick <= startTick + loopLength;
}

NOTE_EDIT_MEM bool isInflatedDisplaySpan(const NoteUtils::DisplayNote& dn, uint32_t loopLength) {
  return IntervalProjection::isInflatedDisplaySpan(dn, loopLength);
}

namespace {

NOTE_EDIT_MEM bool projectCanonicalBaselineForEdit(const NoteId noteId, const NoteBaseline& canonical,
                                                   int32_t originTick, uint32_t loopLength,
                                                   NoteBaseline& out) {
  if (noteId == kInvalidNoteId || loopLength == 0) {
    return false;
  }
  if (canonical.endTick < canonical.startTick) {
    return false;
  }
  ProjectionContext context{};
  context.type = ProjectionType::Edit;
  context.loopLength = loopLength;
  context.window = IntervalProjection::makeFullLoopEditAnalysisWindow(loopLength);
  context.originTick = originTick;
  const CanonicalNoteSpan span{
      noteId,
      TickInterval{static_cast<int32_t>(canonical.startTick),
                   static_cast<int32_t>(canonical.endTick)},
      canonical.pitch,
      canonical.velocity,
  };
  const ProjectedNoteInterval projected =
      IntervalProjection::projectEditLinearSpan(span, context);
  if (projected.noteId == kInvalidNoteId) {
    return false;
  }
  out = canonical;
  out.startTick = static_cast<uint32_t>(projected.interval.start);
  out.endTick = static_cast<uint32_t>(projected.interval.end);
  return true;
}

}  // namespace

NOTE_EDIT_MEM NoteBaseline linearBaselineForOverlapRestore(const NoteEditFocus& focus, const OverlapNote& entry,
                                             MidiEventVec* sessionEvents, uint8_t channel) {
  (void)sessionEvents;
  (void)channel;
  NoteBaseline baseline = entry.baseline;
  if (entry.noteId == kInvalidNoteId) {
    return baseline;
  }
  const auto mapIt = focus.baselineMap.find(entry.noteId);
  if (mapIt == focus.baselineMap.end() ||
      mapIt->second.endTick < mapIt->second.startTick) {
    return baseline;
  }
  if (mapIt->second.startTick != baseline.startTick) {
    return baseline;
  }
  if (entry.state == OverlapNoteStoreState::Hidden) {
    if (isDisplayWrappedBaseline(baseline)) {
      baseline = mapIt->second;
    }
    return baseline;
  }
  if (entry.state == OverlapNoteStoreState::Shortened) {
    baseline = mapIt->second;
    if (mapIt->second.endTick > entry.shortenedEndTick) {
      baseline.endTick = mapIt->second.endTick;
    }
    return baseline;
  }
  baseline = mapIt->second;
  return baseline;
}

NOTE_EDIT_MEM bool resolveLinearNoteSpanForOverlap(const NoteEditFocus& focus, MidiEventVec& events,
                                     uint8_t channel, const NoteUtils::DisplayNote& dn,
                                     NoteBaseline& out, uint32_t loopLength) {
  const NoteId noteId = dn.noteId != kInvalidNoteId ? dn.noteId
                                                    : findBaselineNoteIdForDisplay(focus, dn);
  NoteBaseline canonical{};
  bool hasCanonical = false;
  if (noteId != kInvalidNoteId) {
    const auto mapIt = focus.baselineMap.find(noteId);
    if (mapIt != focus.baselineMap.end() &&
        (loopLength == 0 ||
         isPlausibleStorageSpan(mapIt->second.startTick, mapIt->second.endTick, loopLength))) {
      canonical = mapIt->second;
      hasCanonical = true;
    }
  }
  if (!hasCanonical && dn.endTick >= dn.startTick &&
      (loopLength == 0 || !isInflatedDisplaySpan(dn, loopLength)) &&
      (loopLength == 0 ||
       isPlausibleStorageSpan(dn.startTick, dn.endTick, loopLength))) {
    canonical = baselineFromDisplayNote(dn);
    hasCanonical = true;
  }
  if (!hasCanonical && noteId != kInvalidNoteId &&
      findLinearNoteSpanForNoteId(events, noteId, channel, canonical, dn.startTick, loopLength)) {
    hasCanonical = true;
  }
  if (!hasCanonical && noteId != kInvalidNoteId &&
      findLinearNoteSpanForNoteId(events, noteId, channel, canonical, UINT32_MAX, loopLength)) {
    hasCanonical = true;
  }
  if (!hasCanonical) {
    return false;
  }
  const int32_t originTick = static_cast<int32_t>(dn.startTick);
  return projectCanonicalBaselineForEdit(noteId, canonical, originTick, loopLength, out);
}

NOTE_EDIT_MEM uint32_t overlapNoteEffectiveEnd(const OverlapNote& entry) {
  if (entry.state == OverlapNoteStoreState::Shortened) {
    return entry.shortenedEndTick;
  }
  return entry.baseline.endTick;
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

NOTE_EDIT_MEM bool noteEditFocusHasPendingLengthChange(const NoteEditFocus& focus) {
  return focus.active && focus.last.endTick != focus.commitBaseline.endTick;
}

namespace {

template <typename Alloc>
NOTE_EDIT_MEM bool readLiveBaselineForOverlapDiff(const std::vector<MidiEvent, Alloc>& sessionEvents,
                                                  NoteId noteId, const NoteBaseline& baseline,
                                                  uint8_t channel, uint32_t loopLength,
                                                  NoteBaseline& out) {
  std::vector<MidiEvent, Alloc>& mutableEvents =
      const_cast<std::vector<MidiEvent, Alloc>&>(sessionEvents);
  if (noteId != kInvalidNoteId &&
      findLinearNoteSpanForNoteId(mutableEvents, noteId, channel, out, UINT32_MAX, loopLength)) {
    return true;
  }
  NoteId resolvedId = kInvalidNoteId;
  for (const MidiEvent& evt : sessionEvents) {
    if (evt.channel != channel || !evt.isNoteOn() || evt.data.noteData.velocity == 0) {
      continue;
    }
    if (evt.data.noteData.note == baseline.pitch && evt.tick == baseline.startTick &&
        evt.noteId != kInvalidNoteId) {
      resolvedId = evt.noteId;
      break;
    }
  }
  if (resolvedId == kInvalidNoteId) {
    resolvedId = noteId;
  }
  return findLinearNoteSpanForNoteId(mutableEvents, resolvedId, channel, out, baseline.startTick,
                                     loopLength);
}

}  // namespace

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
        readLiveBaselineForOverlapDiff(sessionEvents, noteId, baseline, channel, loopLength, live);
    if (!hasLive) {
      return true;
    }
    if (live.pitch != baseline.pitch) {
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
  for (const auto& [noteId, entry] : focus.overlapNotes) {
    (void)noteId;
    if (entry.state == OverlapNoteStoreState::Hidden && !entry.preCommitEmitted) {
      return true;
    }
  }
  for (const auto& [noteId, entry] : focus.overlapNotes) {
    (void)noteId;
    if (entry.state == OverlapNoteStoreState::Shortened &&
        entry.shortenedEndTick != entry.baseline.endTick) {
      return true;
    }
  }
  if (focus.last.startTick != focus.commitBaseline.startTick) {
    return true;
  }
  if (focus.last.endTick != focus.commitBaseline.endTick) {
    return true;
  }
  return focus.last.pitch != focus.commitBaseline.pitch;
}

namespace {

NOTE_EDIT_MEM bool pitchLaneClearNeedsRestore(const NoteEditFocus& focus, uint8_t clearedPitch) {
  for (const auto& [noteId, entry] : focus.overlapNotes) {
    (void)noteId;
    if (entry.baseline.pitch != clearedPitch) {
      continue;
    }
    if (entry.state == OverlapNoteStoreState::Visible) {
      continue;
    }
    if (isMovingNoteOverlapScratchEntry(focus, noteId, entry.baseline)) {
      continue;
    }
    return true;
  }
  return false;
}

/// Phase 4 geometry pipeline hides/shortens without writing overlapNotes scratch. Leaving that
/// pitch must take the full path so RestoreNote can reinsert/extend from baselineMap.
NOTE_EDIT_MEM bool baselineMapPitchLaneNeedsRestore(const NoteEditFocus& focus,
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

NOTE_EDIT_MEM bool pitchLaneHasNonVisibleOverlapScratch(const NoteEditFocus& focus, uint8_t pitch) {
  for (const auto& [noteId, entry] : focus.overlapNotes) {
    (void)noteId;
    if (entry.baseline.pitch != pitch) {
      continue;
    }
    if (entry.state != OverlapNoteStoreState::Visible) {
      return true;
    }
  }
  return false;
}

NOTE_EDIT_MEM bool linearStorageSpansOverlapLocal(uint32_t start1, uint32_t end1, uint32_t start2,
                                                  uint32_t end2) {
  return start1 < end2 && start2 < end1;
}

}  // namespace

NOTE_EDIT_MEM bool canApplySimplePitchChange(MidiEventVec& sessionEvents, const NoteEditFocus& focus,
                                             uint8_t channel, uint8_t currentPitch,
                                             uint8_t targetPitch, uint32_t moverStart,
                                             uint32_t moverEnd, uint32_t loopLength) {
  if (!focus.active || focus.movingNoteId == kInvalidNoteId || loopLength == 0) {
    return false;
  }
  if (pitchLaneClearNeedsRestore(focus, currentPitch)) {
    return false;
  }
  if (baselineMapPitchLaneNeedsRestore(focus, sessionEvents, channel, currentPitch)) {
    return false;
  }
  if (pitchLaneHasNonVisibleOverlapScratch(focus, targetPitch)) {
    return false;
  }

  for (const MidiEvent& evt : sessionEvents) {
    if (!evt.isNoteOn() || evt.data.noteData.velocity == 0 || evt.channel != channel ||
        evt.data.noteData.note != targetPitch || evt.noteId == kInvalidNoteId ||
        evt.noteId == focus.movingNoteId) {
      continue;
    }
    const OverlapNote* overlapEntry = findOverlapNoteEntry(focus, evt.noteId);
    if (overlapEntry != nullptr && overlapEntry->state != OverlapNoteStoreState::Visible) {
      return false;
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

namespace {

template <typename Alloc>
NOTE_EDIT_MEM
MidiEvent* findLinearOffForNoteOnLifo(std::vector<MidiEvent, Alloc>& events, MidiEvent* noteOnEvent,
                                       uint8_t pitch) {
  if (noteOnEvent == nullptr) {
    return nullptr;
  }
  std::vector<MidiEvent*> activeNoteOnStack;
  for (auto& evt : events) {
    const bool isNoteOn =
        evt.isNoteOn() && evt.data.noteData.velocity > 0 && evt.data.noteData.note == pitch;
    const bool isNoteOff = evt.isNoteOff() && evt.data.noteData.note == pitch;
    if (isNoteOn) {
      activeNoteOnStack.push_back(&evt);
    } else if (isNoteOff) {
      for (int stackIndex = static_cast<int>(activeNoteOnStack.size()) - 1; stackIndex >= 0;
           --stackIndex) {
        MidiEvent* candidateOn = activeNoteOnStack[static_cast<size_t>(stackIndex)];
        if (evt.tick <= candidateOn->tick) {
          continue;
        }
        MidiEvent* pairedOn = candidateOn;
        activeNoteOnStack.erase(activeNoteOnStack.begin() + stackIndex);
        if (pairedOn == noteOnEvent) {
          return &evt;
        }
        break;
      }
    }
  }
  return nullptr;
}

template <typename Alloc>
NOTE_EDIT_MEM
MidiEvent* findPlausibleOffForNoteOn(std::vector<MidiEvent, Alloc>& events, const MidiEvent& noteOn,
                                     uint32_t loopLength) {
  const uint8_t pitch = noteOn.data.noteData.note;
  const uint32_t startTick = noteOn.tick;
  MidiEvent* nearestInLoop = nullptr;
  MidiEvent* nearestBeyondLoop = nullptr;
  for (auto& evt : events) {
    if (!evt.isNoteOff() || evt.data.noteData.note != pitch || evt.tick <= startTick) {
      continue;
    }
    if (!isPlausibleStorageSpan(startTick, evt.tick, loopLength)) {
      continue;
    }
    if (evt.tick <= loopLength) {
      if (nearestInLoop == nullptr || evt.tick < nearestInLoop->tick) {
        nearestInLoop = &evt;
      }
    } else if (nearestBeyondLoop == nullptr || evt.tick < nearestBeyondLoop->tick) {
      nearestBeyondLoop = &evt;
    }
  }
  if (nearestInLoop != nullptr && nearestBeyondLoop != nullptr && startTick > loopLength / 2) {
    return nearestBeyondLoop;
  }
  if (nearestInLoop != nullptr) {
    return nearestInLoop;
  }
  return nearestBeyondLoop;
}

}  // namespace

template <typename Alloc>
NOTE_EDIT_MEM
MidiEvent* findLinearOffForNoteId(std::vector<MidiEvent, Alloc>& events, const MidiEvent& noteOn,
                                  NoteId noteId, uint32_t loopLength) {
  if (noteId == kInvalidNoteId) {
    return nullptr;
  }
  const uint8_t pitch = noteOn.data.noteData.note;
  MidiEvent* farthestOff = nullptr;
  for (auto& evt : events) {
    if (!evt.isNoteOff() || evt.noteId != noteId || evt.tick <= noteOn.tick) {
      continue;
    }
    if (evt.data.noteData.note != pitch) {
      continue;
    }
    if (loopLength > 0 && !isPlausibleStorageSpan(noteOn.tick, evt.tick, loopLength)) {
      continue;
    }
    if (farthestOff == nullptr || evt.tick > farthestOff->tick) {
      farthestOff = &evt;
    }
  }
  if (farthestOff != nullptr) {
    return farthestOff;
  }
  MidiEvent* mutableOn = nullptr;
  for (auto& evt : events) {
    if (evt.noteId == noteId && evt.isNoteOn() && evt.data.noteData.velocity > 0 &&
        evt.tick == noteOn.tick && evt.data.noteData.note == noteOn.data.noteData.note) {
      mutableOn = &evt;
      break;
    }
  }
  if (mutableOn == nullptr) {
    return nullptr;
  }
  return findLinearOffForNoteOnLifo(events, mutableOn, noteOn.data.noteData.note);
}

template MidiEvent* findLinearOffForNoteId<InternalHeapFirstAllocator<MidiEvent>>(
    MidiEventVec&, const MidiEvent&, NoteId, uint32_t);
template MidiEvent* findLinearOffForNoteId<ExternalMemoryFirstAllocator<MidiEvent>>(
    SessionMidiEventVec&, const MidiEvent&, NoteId, uint32_t);

template <typename Alloc>
NOTE_EDIT_MEM
bool findLinearNoteSpanForNoteId(std::vector<MidiEvent, Alloc>& events, NoteId noteId,
                                 uint8_t channel, NoteBaseline& outBaseline,
                                 uint32_t preferredStartTick, uint32_t loopLength) {
  if (noteId == kInvalidNoteId) {
    return false;
  }
  auto tryNoteOn = [&](MidiEvent& evt) -> bool {
    if (evt.noteId != noteId || evt.channel != channel) {
      return false;
    }
    if (!evt.isNoteOn() || evt.data.noteData.velocity == 0) {
      return false;
    }
    if (preferredStartTick != UINT32_MAX && evt.tick != preferredStartTick) {
      return false;
    }
    MidiEvent* noteOffEvent = nullptr;
    if (evt.noteId != kInvalidNoteId) {
      noteOffEvent = findLinearOffForNoteId(events, evt, evt.noteId, loopLength);
    }
    if (noteOffEvent == nullptr) {
      // LIFO pitch pairing — never nearest-off (findPlausibleOffForNoteOn), which attaches a
      // same-pitch left neighbor's shortened end to the mover while spans overlap in-store
      // (session_20260804_220842 display end theft).
      noteOffEvent = findLinearOffForNoteOnLifo(events, &evt, evt.data.noteData.note);
    }
    if (noteOffEvent == nullptr) {
      return false;
    }
    outBaseline.pitch = evt.data.noteData.note;
    outBaseline.velocity = evt.data.noteData.velocity;
    outBaseline.startTick = evt.tick;
    outBaseline.endTick = noteOffEvent->tick;
    if (loopLength > 0 &&
        !isPlausibleStorageSpan(outBaseline.startTick, outBaseline.endTick, loopLength)) {
      return false;
    }
    return true;
  };

  if (preferredStartTick != UINT32_MAX) {
    for (MidiEvent& evt : events) {
      if (tryNoteOn(evt)) {
        return true;
      }
    }
  }
  for (MidiEvent& evt : events) {
    if (tryNoteOn(evt)) {
      return true;
    }
  }
  return false;
}

template bool findLinearNoteSpanForNoteId<InternalHeapFirstAllocator<MidiEvent>>(
    MidiEventVec&, NoteId, uint8_t, NoteBaseline&, uint32_t, uint32_t);
template bool findLinearNoteSpanForNoteId<ExternalMemoryFirstAllocator<MidiEvent>>(
    SessionMidiEventVec&, NoteId, uint8_t, NoteBaseline&, uint32_t, uint32_t);

namespace {

template <typename Alloc>
NOTE_EDIT_MEM MidiEvent* findNoteOnAtChannelPitchTick(std::vector<MidiEvent, Alloc>& events,
                                                      uint8_t channel, uint8_t pitch,
                                                      uint32_t tick) {
  for (auto& evt : events) {
    if (evt.channel == channel && evt.isNoteOn() && evt.data.noteData.velocity > 0 &&
        evt.data.noteData.note == pitch && evt.tick == tick) {
      return &evt;
    }
  }
  return nullptr;
}

template <typename Alloc>
NOTE_EDIT_MEM MidiEvent* findNoteOnForNoteIdAtTick(std::vector<MidiEvent, Alloc>& events,
                                                   NoteId noteId, uint8_t channel, uint8_t pitch,
                                                   uint32_t tick) {
  for (auto& evt : events) {
    if (evt.noteId != noteId || evt.channel != channel || !evt.isNoteOn() ||
        evt.data.noteData.velocity == 0 || evt.data.noteData.note != pitch ||
        evt.tick != tick) {
      continue;
    }
    return &evt;
  }
  return nullptr;
}

template <typename Alloc>
NOTE_EDIT_MEM MidiEvent* findNoteOnForNoteIdAnyChannel(std::vector<MidiEvent, Alloc>& events,
                                                       NoteId noteId, uint8_t pitch) {
  for (auto& evt : events) {
    if (evt.noteId != noteId || !evt.isNoteOn() || evt.data.noteData.velocity == 0 ||
        evt.data.noteData.note != pitch) {
      continue;
    }
    return &evt;
  }
  return nullptr;
}

}  // namespace

template <typename Alloc>
NOTE_EDIT_MEM
MidiEvent* findNoteOnForMovingNoteEdit(std::vector<MidiEvent, Alloc>& events,
                                       const NoteEditFocus& focus, uint8_t channel,
                                       uint8_t pitch, uint32_t startTick, uint32_t loopLength) {
  if (focus.active && focus.movingNoteId != kInvalidNoteId) {
    NoteBaseline linearSpan;
    const uint32_t preferredStarts[] = {startTick, focus.commitBaseline.startTick, UINT32_MAX};
    for (uint32_t preferredStart : preferredStarts) {
      if (!findLinearNoteSpanForNoteId(events, focus.movingNoteId, channel, linearSpan,
                                       preferredStart, loopLength)) {
        continue;
      }
      if (MidiEvent* on =
              findNoteOnForNoteIdAtTick(events, focus.movingNoteId, channel, linearSpan.pitch,
                                        linearSpan.startTick)) {
        return on;
      }
    }
    if (MidiEvent* on = findNoteOnForNoteIdAnyChannel(events, focus.movingNoteId, focus.last.pitch)) {
      return on;
    }
  }

  const uint8_t pitchCandidates[] = {pitch, focus.active ? focus.last.pitch : pitch,
                                     focus.active ? focus.commitBaseline.pitch : pitch};
  for (uint8_t pitchCandidate : pitchCandidates) {
    const uint32_t tickCandidates[] = {startTick, focus.active ? focus.commitBaseline.startTick : startTick,
                                       focus.active ? focus.movingNoteRange.start : startTick};
    for (uint32_t tick : tickCandidates) {
      if (MidiEvent* on = findNoteOnForNoteIdAtTick(events, focus.movingNoteId, channel,
                                                    pitchCandidate, tick)) {
        return on;
      }
    }
  }

  if (!focus.active || focus.movingNoteId == kInvalidNoteId) {
    const uint32_t tickCandidates[] = {startTick, focus.active ? focus.commitBaseline.startTick : startTick,
                                       focus.active ? focus.movingNoteRange.start : startTick};
    for (uint32_t tick : tickCandidates) {
      if (MidiEvent* on = findNoteOnAtChannelPitchTick(events, channel, pitch, tick)) {
        return on;
      }
    }
  }
  return nullptr;
}

template MidiEvent* findNoteOnForMovingNoteEdit<InternalHeapFirstAllocator<MidiEvent>>(
    MidiEventVec&, const NoteEditFocus&, uint8_t, uint8_t, uint32_t, uint32_t);
template MidiEvent* findNoteOnForMovingNoteEdit<ExternalMemoryFirstAllocator<MidiEvent>>(
    SessionMidiEventVec&, const NoteEditFocus&, uint8_t, uint8_t, uint32_t, uint32_t);

template <typename Alloc>
NOTE_EDIT_MEM
bool syncNoteEditFocusLinearFromSessionStore(NoteEditFocus& focus,
                                             std::vector<MidiEvent, Alloc>& events,
                                             uint8_t channel, uint32_t loopLength) {
  if (!focus.active) {
    return false;
  }
  NoteBaseline linearSpan;
  if (focus.movingNoteId != kInvalidNoteId &&
      findLinearNoteSpanForNoteId(events, focus.movingNoteId, channel, linearSpan,
                                  focus.last.startTick, loopLength)) {
    focus.last.startTick = linearSpan.startTick;
    focus.last.endTick = linearSpan.endTick;
    focus.last.pitch = linearSpan.pitch;
    focus.last.velocity = linearSpan.velocity;
    focus.movingNoteRange.start = linearSpan.startTick;
    focus.movingNoteRange.end = linearSpan.endTick;
    return true;
  }
  if (focus.movingNoteId != kInvalidNoteId &&
      findLinearNoteSpanForNoteId(events, focus.movingNoteId, channel, linearSpan,
                                  focus.commitBaseline.startTick, loopLength)) {
    focus.last.startTick = linearSpan.startTick;
    focus.last.endTick = linearSpan.endTick;
    focus.movingNoteRange.start = linearSpan.startTick;
    focus.movingNoteRange.end = linearSpan.endTick;
    return true;
  }
  if (focus.movingNoteId != kInvalidNoteId &&
      findLinearNoteSpanForNoteId(events, focus.movingNoteId, channel, linearSpan, UINT32_MAX,
                                  loopLength)) {
    focus.last.startTick = linearSpan.startTick;
    focus.last.endTick = linearSpan.endTick;
    focus.movingNoteRange.start = linearSpan.startTick;
    focus.movingNoteRange.end = linearSpan.endTick;
    return true;
  }
  const uint32_t startCandidates[] = {focus.last.startTick, focus.commitBaseline.startTick};
  for (uint32_t startTick : startCandidates) {
    MidiEvent* noteOnEvent =
        findNoteOnAtChannelPitchTick(events, channel, focus.last.pitch, startTick);
    if (noteOnEvent == nullptr) {
      continue;
    }
    MidiEvent* noteOffEvent = nullptr;
    if (focus.movingNoteId != kInvalidNoteId) {
      noteOffEvent =
          findLinearOffForNoteId(events, *noteOnEvent, focus.movingNoteId, loopLength);
    }
    if (noteOffEvent == nullptr) {
      noteOffEvent = findLinearOffForNoteOnLifo(events, noteOnEvent, focus.last.pitch);
    }
    if (noteOffEvent == nullptr) {
      continue;
    }
    focus.last.startTick = startTick;
    focus.last.endTick = noteOffEvent->tick;
    focus.movingNoteRange.start = startTick;
    if (focus.movingNoteRange.end <= focus.last.startTick ||
        focus.last.endTick > focus.movingNoteRange.end) {
      focus.movingNoteRange.end = focus.last.endTick;
    }
    return true;
  }
  return false;
}

template bool syncNoteEditFocusLinearFromSessionStore<InternalHeapFirstAllocator<MidiEvent>>(
    NoteEditFocus&, MidiEventVec&, uint8_t, uint32_t);
template bool syncNoteEditFocusLinearFromSessionStore<ExternalMemoryFirstAllocator<MidiEvent>>(
    NoteEditFocus&, SessionMidiEventVec&, uint8_t, uint32_t);

template <typename Alloc>
NOTE_EDIT_MEM void stampNoteIdsOntoPairedNoteOffs(std::vector<MidiEvent, Alloc>& events,
                                                  uint8_t channel) {
  std::vector<MidiEvent*> activeNoteOnStack;
  for (MidiEvent& evt : events) {
    if (evt.channel != channel) {
      continue;
    }
    const uint8_t pitch = evt.data.noteData.note;
    if (evt.isNoteOn() && evt.data.noteData.velocity > 0) {
      activeNoteOnStack.push_back(&evt);
      continue;
    }
    if (!evt.isNoteOff()) {
      continue;
    }
    for (int stackIndex = static_cast<int>(activeNoteOnStack.size()) - 1; stackIndex >= 0;
         --stackIndex) {
      MidiEvent* candidateOn = activeNoteOnStack[static_cast<size_t>(stackIndex)];
      if (candidateOn->data.noteData.note != pitch || evt.tick <= candidateOn->tick) {
        continue;
      }
      activeNoteOnStack.erase(activeNoteOnStack.begin() + stackIndex);
      if (evt.noteId == kInvalidNoteId && candidateOn->noteId != kInvalidNoteId) {
        evt.noteId = candidateOn->noteId;
      }
      break;
    }
  }
}

template void stampNoteIdsOntoPairedNoteOffs<InternalHeapFirstAllocator<MidiEvent>>(MidiEventVec&,
                                                                                    uint8_t);
template void stampNoteIdsOntoPairedNoteOffs<ExternalMemoryFirstAllocator<MidiEvent>>(
    SessionMidiEventVec&, uint8_t);

namespace {

NOTE_EDIT_MEM bool eventMatchesNoteEndpoint(const MidiEvent& e, uint8_t channel, uint8_t pitch,
                              uint32_t tick, bool wantOn) {
  if (e.channel != channel || e.data.noteData.note != pitch || e.tick != tick) {
    return false;
  }
  if (wantOn) {
    return e.type == midi::NoteOn && e.data.noteData.velocity > 0;
  }
  return e.type == midi::NoteOff ||
         (e.type == midi::NoteOn && e.data.noteData.velocity == 0);
}

template <typename Alloc>
NOTE_EDIT_MEM
void eraseNoteEndpoint(std::vector<MidiEvent, Alloc>& flat, uint8_t channel, uint8_t pitch,
                         uint32_t tick, bool wantOn) {
  flat.erase(std::remove_if(flat.begin(), flat.end(),
                            [&](const MidiEvent& e) {
                              return eventMatchesNoteEndpoint(e, channel, pitch, tick, wantOn);
                            }),
             flat.end());
}

template <typename Alloc>
NOTE_EDIT_MEM
void eraseNotePairAtBaseline(std::vector<MidiEvent, Alloc>& flat, uint8_t channel,
                             const NoteBaseline& bl) {
  eraseNoteEndpoint(flat, channel, bl.pitch, bl.startTick, true);
  eraseNoteEndpoint(flat, channel, bl.pitch, bl.endTick, false);
}

template <typename Alloc>
NOTE_EDIT_MEM
MidiEvent* findNoteOnAt(std::vector<MidiEvent, Alloc>& flat, uint8_t channel, uint8_t pitch,
                        uint32_t startTick) {
  for (MidiEvent& e : flat) {
    if (eventMatchesNoteEndpoint(e, channel, pitch, startTick, true)) {
      return &e;
    }
  }
  return nullptr;
}

template <typename Alloc>
NOTE_EDIT_MEM
MidiEvent* findNoteOffForOn(std::vector<MidiEvent, Alloc>& flat, uint8_t channel, uint8_t pitch,
                            uint32_t startTick, uint32_t endTick) {
  (void)startTick;
  for (MidiEvent& e : flat) {
    if (eventMatchesNoteEndpoint(e, channel, pitch, endTick, false)) {
      return &e;
    }
  }
  return nullptr;
}

template <typename Alloc>
NOTE_EDIT_MEM
void insertNotePair(std::vector<MidiEvent, Alloc>& flat, uint8_t channel, const NoteBaseline& bl,
                    uint32_t endTick, NoteId noteId) {
  MidiEvent noteOn = MidiEvent::NoteOn(bl.startTick, channel, bl.pitch, bl.velocity);
  noteOn.noteId = noteId;
  flat.push_back(noteOn);
  flat.push_back(MidiEvent::NoteOff(endTick, channel, bl.pitch, 0));
}

template <typename Alloc>
NOTE_EDIT_MEM
void materializeShortenedOverlap(std::vector<MidiEvent, Alloc>& flat, uint8_t channel,
                                 const OverlapNote& entry, uint32_t loopLength) {
  const NoteBaseline& bl = entry.baseline;
  const uint32_t targetOff = entry.shortenedEndTick;

  const NoteUtils::DisplayNoteVec notes =
      NoteUtils::reconstructDisplayNotes(flat, loopLength, false);
  for (const NoteUtils::DisplayNote& dn : notes) {
    if (dn.noteId == entry.noteId ||
        (dn.note == bl.pitch && dn.startTick == bl.startTick && dn.endTick == targetOff)) {
      return;
    }
  }

  eraseNoteEndpoint(flat, channel, bl.pitch, bl.endTick, false);

  MidiEvent* noteOn = findNoteOnAt(flat, channel, bl.pitch, bl.startTick);
  if (noteOn != nullptr) {
    MidiEvent* noteOff = findNoteOffForOn(flat, channel, bl.pitch, bl.startTick, targetOff);
    if (noteOff == nullptr) {
      noteOff = findNoteOffForOn(flat, channel, bl.pitch, bl.startTick, bl.endTick);
    }
    if (noteOff != nullptr) {
      noteOff->tick = targetOff;
      return;
    }
    flat.push_back(MidiEvent::NoteOff(targetOff, channel, bl.pitch, 0));
    return;
  }

  insertNotePair(flat, channel, bl, targetOff, entry.noteId);
}

}  // namespace

template <typename Alloc>
NOTE_EDIT_MEM
void resolveOverlapNotesForPreCommit(std::vector<MidiEvent, Alloc>& sessionStoreEvents,
                                     NoteEditFocus& focus, uint8_t channel, uint32_t loopLength) {
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

namespace {

NOTE_EDIT_MEM EditPass makeNoteEditRow(EditActionType actionType, EditPropertyType propertyType) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = actionType;
  row.propertyType = propertyType;
  row.state = EditPassState::Active;
  return row;
}

}  // namespace

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

NOTE_EDIT_MEM EditPassVec buildPreCommitOverlapEditPasses(const NoteEditFocus& focus) {
  EditPassVec rows;
  if (!focus.active) {
    return rows;
  }

  for (const auto& [noteId, entry] : focus.overlapNotes) {
    (void)noteId;
    if (entry.state == OverlapNoteStoreState::Hidden && !entry.preCommitEmitted) {
      EditPass row = makeNoteEditRow(EditActionType::Delete, EditPropertyType::None);
      row.targetNoteId = entry.noteId;
      rows.push_back(row);
    }
  }

  for (const auto& [noteId, entry] : focus.overlapNotes) {
    (void)noteId;
    if (entry.state == OverlapNoteStoreState::Shortened &&
        entry.shortenedEndTick != entry.baseline.endTick) {
      EditPass row = makeNoteEditRow(EditActionType::Update, EditPropertyType::Length);
      row.targetNoteId = entry.noteId;
      row.startTick = entry.baseline.startTick;
      row.endTick = entry.shortenedEndTick;
      rows.push_back(row);
    }
  }

  return rows;
}

namespace {

NOTE_EDIT_MEM const OverlapNote* findOverlapNoteForDisplayNote(const NoteEditFocus& focus,
                                                 const NoteUtils::DisplayNote& dn) {
  if (focus.active && focus.movingNoteId != kInvalidNoteId &&
      dn.noteId == focus.movingNoteId) {
    if (const OverlapNote* entry = findOverlapNoteEntry(focus, dn.noteId)) {
      return entry;
    }
    return nullptr;
  }
  if (dn.noteId != kInvalidNoteId) {
    if (const OverlapNote* entry = findOverlapNoteEntry(focus, dn.noteId)) {
      return entry;
    }
  }
  for (const auto& [noteId, entry] : focus.overlapNotes) {
    (void)noteId;
    if (entry.baseline.pitch != dn.note || entry.baseline.startTick != dn.startTick) {
      continue;
    }
    if (entry.state == OverlapNoteStoreState::Shortened &&
        entry.shortenedEndTick == dn.endTick) {
      return &entry;
    }
    if (entry.baseline.endTick == dn.endTick) {
      return &entry;
    }
  }
  return nullptr;
}

NOTE_EDIT_MEM bool isExcludedFromSelectableDisplayNotes(const NoteEditFocus& focus,
                                          const NoteUtils::DisplayNote& dn) {
  if (focus.active && focus.movingNoteId != kInvalidNoteId &&
      dn.noteId == focus.movingNoteId) {
    return false;
  }
  const OverlapNote* overlap = findOverlapNoteForDisplayNote(focus, dn);
  if (overlap == nullptr) {
    return false;
  }
  if (overlap->state == OverlapNoteStoreState::Hidden) {
    return true;
  }
  return overlap->innerUnderMovingNote;
}

}  // namespace

template <typename AllocA, typename AllocB>
NOTE_EDIT_MEM void populateBaselineMapForEditClosure(
    NoteEditFocus& focus, const std::vector<MidiEvent, AllocA>& committedLoopEvents,
    const std::vector<MidiEvent, AllocB>& sessionEvents, uint8_t channel, uint32_t loopLength) {
  if (!focus.active || loopLength == 0) {
    return;
  }
  // Transaction baseline is frozen at edit-driver boundary (D19). Insert-if-missing only —
  // never prune from live-store presence (hidden notes must keep their baseline entry).
  const std::unordered_set<NoteId> closure =
      buildEditClosureNoteIds(focus, sessionEvents, channel, loopLength);
  std::vector<MidiEvent, AllocA> mutableCommitted = committedLoopEvents;
  // Pass materialize noteIds can differ from session-store ids — resolve committed span by
  // live pitch+start when noteId lookup fails.
  std::vector<MidiEvent, AllocB> mutableSession = sessionEvents;
  for (NoteId noteId : closure) {
    if (noteId == kInvalidNoteId || focus.baselineMap.find(noteId) != focus.baselineMap.end()) {
      continue;
    }
    NoteBaseline baseline{};
    if (findLinearNoteSpanForNoteId(mutableCommitted, noteId, channel, baseline, UINT32_MAX,
                                    loopLength)) {
      focus.baselineMap[noteId] = baseline;
      continue;
    }
    NoteBaseline liveSpan{};
    if (!findLinearNoteSpanForNoteId(mutableSession, noteId, channel, liveSpan, UINT32_MAX,
                                     loopLength)) {
      continue;
    }
    // AllocA for committed is always InternalHeapFirstAllocator (MidiEventVec) at call sites.
    if (findCommittedLinearSpanForPitchStart(mutableCommitted, channel, liveSpan.pitch,
                                             liveSpan.startTick, loopLength, baseline)) {
      focus.baselineMap[noteId] = baseline;
    } else {
      focus.baselineMap[noteId] = liveSpan;
    }
  }
}

template void populateBaselineMapForEditClosure<InternalHeapFirstAllocator<MidiEvent>,
                                                InternalHeapFirstAllocator<MidiEvent>>(
    NoteEditFocus&, const MidiEventVec&, const MidiEventVec&, uint8_t, uint32_t);
template void populateBaselineMapForEditClosure<InternalHeapFirstAllocator<MidiEvent>,
                                                ExternalMemoryFirstAllocator<MidiEvent>>(
    NoteEditFocus&, const MidiEventVec&, const SessionMidiEventVec&, uint8_t, uint32_t);

template <typename Alloc>
std::unordered_set<NoteId> buildEditClosureNoteIds(const NoteEditFocus& focus,
                                                 const std::vector<MidiEvent, Alloc>& sessionEvents,
                                                 uint8_t channel, uint32_t loopLength) {
  std::unordered_set<NoteId> ids;
  if (!focus.active || loopLength == 0) {
    return ids;
  }
  if (focus.movingNoteId != kInvalidNoteId) {
    ids.insert(focus.movingNoteId);
  }
  for (const auto& [noteId, entry] : focus.overlapNotes) {
    (void)entry;
    if (noteId != kInvalidNoteId) {
      ids.insert(noteId);
    }
  }
  // Full-loop transaction baseline (D19 / D21): every live noteId participates so restore
  // candidates survive pitch changes. Analyze still pitch-gates Hide/Shorten (Q14).
  for (const MidiEvent& onEvt : sessionEvents) {
    if (!onEvt.isNoteOn() || onEvt.data.noteData.velocity == 0 || onEvt.channel != channel ||
        onEvt.noteId == kInvalidNoteId) {
      continue;
    }
    ids.insert(onEvt.noteId);
  }
  // BaselineMap keys that are already frozen (including notes hidden from live store).
  for (const auto& [noteId, baseline] : focus.baselineMap) {
    (void)baseline;
    if (noteId != kInvalidNoteId) {
      ids.insert(noteId);
    }
  }
  return ids;
}

template std::unordered_set<NoteId> buildEditClosureNoteIds<InternalHeapFirstAllocator<MidiEvent>>(
    const NoteEditFocus&, const MidiEventVec&, uint8_t, uint32_t);
template std::unordered_set<NoteId> buildEditClosureNoteIds<ExternalMemoryFirstAllocator<MidiEvent>>(
    const NoteEditFocus&, const SessionMidiEventVec&, uint8_t, uint32_t);

template <typename Alloc>
NOTE_EDIT_MEM
NoteUtils::DisplayNoteVec filterSelectableDisplayNotes(
    const std::vector<MidiEvent, Alloc>& sessionEvents, const NoteEditFocus& focus,
    uint8_t channel, uint32_t loopLength) {
  (void)channel;
  NoteUtils::DisplayNoteVec allNotes =
      NoteUtils::reconstructDisplayNotes(sessionEvents, loopLength, false);
  if (!focus.active || focus.overlapNotes.empty()) {
    return allNotes;
  }

  NoteUtils::DisplayNoteVec filtered;
  filtered.reserve(allNotes.size());
  for (const NoteUtils::DisplayNote& dn : allNotes) {
    if (isExcludedFromSelectableDisplayNotes(focus, dn)) {
      continue;
    }
    filtered.push_back(dn);
  }
  return filtered;
}

template NoteUtils::DisplayNoteVec filterSelectableDisplayNotes<InternalHeapFirstAllocator<MidiEvent>>(
    const MidiEventVec&, const NoteEditFocus&, uint8_t, uint32_t);
template NoteUtils::DisplayNoteVec
filterSelectableDisplayNotes<ExternalMemoryFirstAllocator<MidiEvent>>(const SessionMidiEventVec&,
                                                                      const NoteEditFocus&, uint8_t,
                                                                      uint32_t);

NOTE_EDIT_MEM EditPassVec buildPreCommitBaselineLiveDiffOverlapPasses(
    const NoteEditFocus& focus, const MidiEventVec& sessionEvents, uint8_t channel,
    uint32_t loopLength) {
  EditPassVec rows;
  if (!focus.active) {
    return rows;
  }
  (void)loopLength;

  for (const auto& [noteId, baseline] : focus.baselineMap) {
    if (noteId == kInvalidNoteId || noteId == focus.movingNoteId) {
      continue;
    }
    NoteBaseline live{};
    const bool hasLive =
        readLiveBaselineForOverlapDiff(sessionEvents, noteId, baseline, channel, loopLength, live);
    if (!hasLive) {
      EditPass row = makeNoteEditRow(EditActionType::Delete, EditPropertyType::None);
      row.targetNoteId = noteId;
      rows.push_back(row);
      continue;
    }
    if (live.pitch != baseline.pitch) {
      continue;
    }
    if (live.startTick != baseline.startTick) {
      EditPass row = makeNoteEditRow(EditActionType::Update, EditPropertyType::NoteRange);
      row.targetNoteId = noteId;
      row.startTick = live.startTick;
      row.endTick = live.endTick;
      rows.push_back(row);
    } else if (live.endTick != baseline.endTick) {
      EditPass row = makeNoteEditRow(EditActionType::Update, EditPropertyType::Length);
      row.targetNoteId = noteId;
      row.startTick = baseline.startTick;
      row.endTick = live.endTick;
      rows.push_back(row);
    }
  }
  return rows;
}

NOTE_EDIT_MEM EditPassVec buildPreCommitEditPasses(const NoteEditFocus& focus, uint8_t channel,
                                                   const MidiEventVec* sessionStoreEvents,
                                                   uint32_t loopLength) {
  EditPassVec rows;
  if (sessionStoreEvents != nullptr && loopLength > 0) {
    rows = buildPreCommitBaselineLiveDiffOverlapPasses(focus, *sessionStoreEvents, channel,
                                                       loopLength);
  } else {
    rows = buildPreCommitOverlapEditPasses(focus);
  }
  if (!focus.active) {
    return rows;
  }
  (void)channel;

  const bool startChanged = focus.last.startTick != focus.commitBaseline.startTick;
  const bool endChanged = focus.last.endTick != focus.commitBaseline.endTick;
  const bool pitchChanged = focus.last.pitch != focus.commitBaseline.pitch;

  if (startChanged) {
    EditPass row = makeNoteEditRow(EditActionType::Update, EditPropertyType::NoteRange);
    row.targetNoteId = focus.movingNoteId;
    row.startTick = focus.last.startTick;
    row.endTick = focus.last.endTick;
    rows.push_back(row);
  } else if (endChanged) {
    EditPass row = makeNoteEditRow(EditActionType::Update, EditPropertyType::Length);
    row.targetNoteId = focus.movingNoteId;
    row.startTick = focus.commitBaseline.startTick;
    row.endTick = focus.last.endTick;
    rows.push_back(row);
  }

  if (pitchChanged) {
    EditPass row = makeNoteEditRow(EditActionType::Update, EditPropertyType::Pitch);
    row.targetNoteId = focus.movingNoteId;
    row.pitch = focus.last.pitch;
    rows.push_back(row);
  }

  return rows;
}
