//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteEditFocus.h"

#include "Utils/NoteMovementWrap.h"
#include "Utils/NoteUtils.h"

NoteRef noteRefFromDisplay(uint8_t channel, const NoteUtils::DisplayNote& dn) {
  return {channel, dn.note, dn.startTick, dn.endTick};
}

NoteRef noteRefFromBaseline(uint8_t channel, const NoteBaseline& bl) {
  return {channel, bl.pitch, bl.startTick, bl.endTick};
}

uint32_t overlapFootprintDisplayEnd(const NoteEditFocus& focus, uint32_t loopLength) {
  if (!focus.active || loopLength == 0) {
    return focus.overlapFootprint.end;
  }
  const uint32_t end = focus.overlapFootprint.end;
  return (end >= loopLength) ? (end % loopLength) : end;
}

bool isInnerUnderOverlapFootprint(const NoteEditFocus& focus, uint8_t pitch,
                                  uint32_t noteStart, uint32_t noteEnd,
                                  uint32_t loopLength) {
  if (!focus.active || loopLength == 0) {
    return false;
  }
  if (pitch == focus.commitBaseline.pitch) {
    return false;
  }
  return NoteMovementUtils::isNoteWithinMovingSpan(
      noteStart, noteEnd, focus.overlapFootprint.start,
      overlapFootprintDisplayEnd(focus, loopLength), loopLength);
}

OverlapNote* findOverlapNoteEntry(NoteEditFocus& focus, const NoteRef& ref) {
  const auto it = focus.overlapNotes.find(ref);
  return it == focus.overlapNotes.end() ? nullptr : &it->second;
}

const OverlapNote* findOverlapNoteEntry(const NoteEditFocus& focus, const NoteRef& ref) {
  const auto it = focus.overlapNotes.find(ref);
  return it == focus.overlapNotes.end() ? nullptr : &it->second;
}

NoteRef findBaselineRefForNote(const NoteEditFocus& focus, uint8_t channel,
                               uint8_t pitch, uint32_t startTick, uint32_t endTick) {
  const NoteRef probe{channel, pitch, startTick, endTick};
  if (focus.baselineMap.find(probe) != focus.baselineMap.end()) {
    return probe;
  }
  for (const auto& [ref, baseline] : focus.baselineMap) {
    if (baseline.pitch == pitch && baseline.startTick == startTick &&
        baseline.endTick == endTick) {
      return ref;
    }
  }
  return probe;
}

NoteBaseline baselineForDisplayNote(const NoteEditFocus& focus, uint8_t channel,
                                    const NoteUtils::DisplayNote& dn) {
  const NoteRef ref = findBaselineRefForNote(focus, channel, dn.note, dn.startTick,
                                             dn.endTick);
  const auto it = focus.baselineMap.find(ref);
  if (it != focus.baselineMap.end()) {
    return it->second;
  }
  return NoteBaseline{dn.note, dn.velocity, dn.startTick, dn.endTick};
}

uint32_t overlapNoteEffectiveEnd(const OverlapNote& entry) {
  if (entry.state == OverlapNoteStoreState::Shortened) {
    return entry.shortenedEndTick;
  }
  return entry.baseline.endTick;
}

void rebuildNoteEditFocusFromStore(NoteEditFocus& focus, const MidiEventVec& flat,
                                   uint8_t channel, uint32_t loopLength,
                                   int selectedNoteIdx) {
  focus.clear();
  if (loopLength == 0) {
    return;
  }

  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(flat, loopLength, false);
  for (const NoteUtils::DisplayNote& dn : notes) {
    const NoteRef ref{channel, dn.note, dn.startTick, dn.endTick};
    focus.baselineMap[ref] = NoteBaseline{dn.note, dn.velocity, dn.startTick, dn.endTick};
  }

  if (selectedNoteIdx < 0 ||
      selectedNoteIdx >= static_cast<int>(notes.size())) {
    return;
  }

  const NoteUtils::DisplayNote& selected = notes[static_cast<size_t>(selectedNoteIdx)];
  focus.moving = {channel, selected.note, selected.startTick, selected.endTick};
  focus.commitBaseline = {selected.note, selected.velocity, selected.startTick,
                          selected.endTick};
  focus.overlapFootprint.start = selected.startTick;
  focus.overlapFootprint.end = selected.endTick;
  focus.last = focus.commitBaseline;
  focus.active = true;
}

void noteEditFocusApplyLengthEnd(NoteEditFocus& focus, uint32_t newEndTick) {
  if (!focus.active) {
    return;
  }
  focus.last.endTick = newEndTick;
  focus.overlapFootprint.end = newEndTick;
}

void noteEditFocusApplyMoveEnd(NoteEditFocus& focus, uint32_t newStart, uint32_t newEnd) {
  if (!focus.active) {
    return;
  }
  focus.last.startTick = newStart;
  focus.last.endTick = newEnd;
  focus.overlapFootprint.end = newEnd;
}

void noteEditFocusApplyPitch(NoteEditFocus& focus, uint8_t newPitch, uint32_t start,
                             uint32_t end, uint32_t loopLength) {
  if (!focus.active || loopLength == 0) {
    return;
  }
  focus.last.pitch = newPitch;
  focus.last.startTick = start;
  focus.last.endTick = end;
  const uint32_t candidateLength = NoteMovementUtils::calculateNoteLength(
      focus.overlapFootprint.start, end, loopLength);
  const uint32_t baselineLength = NoteMovementUtils::calculateNoteLength(
      focus.overlapFootprint.start, focus.commitBaseline.endTick, loopLength);
  if (candidateLength > baselineLength) {
    focus.overlapFootprint.end = end;
  }
}

bool noteEditFocusHasPendingLengthChange(const NoteEditFocus& focus) {
  return focus.active && focus.last.endTick != focus.commitBaseline.endTick;
}
