//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteEditFocus.h"

#include <algorithm>

#include "Utils/NoteMovementWrap.h"
#include "Utils/NoteUtils.h"

NoteRef noteRefFromDisplay(uint8_t channel, const NoteUtils::DisplayNote& dn) {
  return {channel, dn.note, dn.startTick, dn.endTick};
}

NoteRef noteRefFromBaseline(uint8_t channel, const NoteBaseline& bl) {
  return {channel, bl.pitch, bl.startTick, bl.endTick};
}

uint32_t movingNoteRangeDisplayEnd(const NoteEditFocus& focus, uint32_t loopLength) {
  if (!focus.active || loopLength == 0) {
    return focus.movingNoteRange.end;
  }
  const uint32_t end = focus.movingNoteRange.end;
  return (end >= loopLength) ? (end % loopLength) : end;
}

bool isInnerOverlapNoteInMovingNoteRange(const NoteEditFocus& focus, uint8_t pitch,
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

void rebuildNoteEditFocusFromStore(NoteEditFocus& focus, const MidiEventVec& loopMidiEvents,
                                   uint8_t channel, uint32_t loopLength,
                                   int selectedNoteIdx) {
  focus.clear();
  if (loopLength == 0) {
    return;
  }

  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(loopMidiEvents, loopLength, false);
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
  focus.movingNoteRange.start = selected.startTick;
  focus.movingNoteRange.end = selected.endTick;
  focus.last = focus.commitBaseline;
  focus.active = true;
}

void noteEditFocusApplyLengthEnd(NoteEditFocus& focus, uint32_t newEndTick) {
  if (!focus.active) {
    return;
  }
  focus.last.endTick = newEndTick;
  focus.movingNoteRange.end = newEndTick;
}

void noteEditFocusApplyMoveEnd(NoteEditFocus& focus, uint32_t newStart, uint32_t newEnd) {
  if (!focus.active) {
    return;
  }
  focus.last.startTick = newStart;
  focus.last.endTick = newEnd;
  focus.movingNoteRange.end = newEnd;
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
      focus.movingNoteRange.start, end, loopLength);
  const uint32_t baselineLength = NoteMovementUtils::calculateNoteLength(
      focus.movingNoteRange.start, focus.commitBaseline.endTick, loopLength);
  if (candidateLength > baselineLength) {
    focus.movingNoteRange.end = end;
  }
}

bool noteEditFocusHasPendingLengthChange(const NoteEditFocus& focus) {
  return focus.active && focus.last.endTick != focus.commitBaseline.endTick;
}

namespace {

bool eventMatchesNoteEndpoint(const MidiEvent& e, uint8_t channel, uint8_t pitch,
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

void eraseNoteEndpoint(MidiEventVec& flat, uint8_t channel, uint8_t pitch, uint32_t tick,
                       bool wantOn) {
  flat.erase(std::remove_if(flat.begin(), flat.end(),
                            [&](const MidiEvent& e) {
                              return eventMatchesNoteEndpoint(e, channel, pitch, tick, wantOn);
                            }),
             flat.end());
}

void eraseNotePairAtBaseline(MidiEventVec& flat, uint8_t channel, const NoteBaseline& bl) {
  eraseNoteEndpoint(flat, channel, bl.pitch, bl.startTick, true);
  eraseNoteEndpoint(flat, channel, bl.pitch, bl.endTick, false);
}

MidiEvent* findNoteOnAt(MidiEventVec& flat, uint8_t channel, uint8_t pitch, uint32_t startTick) {
  for (MidiEvent& e : flat) {
    if (eventMatchesNoteEndpoint(e, channel, pitch, startTick, true)) {
      return &e;
    }
  }
  return nullptr;
}

MidiEvent* findNoteOffForOn(MidiEventVec& flat, uint8_t channel, uint8_t pitch,
                            uint32_t startTick, uint32_t endTick) {
  (void)startTick;
  for (MidiEvent& e : flat) {
    if (eventMatchesNoteEndpoint(e, channel, pitch, endTick, false)) {
      return &e;
    }
  }
  return nullptr;
}

void insertNotePair(MidiEventVec& flat, uint8_t channel, const NoteBaseline& bl,
                    uint32_t endTick) {
  flat.push_back(MidiEvent::NoteOn(bl.startTick, channel, bl.pitch, bl.velocity));
  flat.push_back(MidiEvent::NoteOff(endTick, channel, bl.pitch, 0));
}

void materializeShortenedOverlap(MidiEventVec& flat, uint8_t channel, const OverlapNote& entry,
                                 uint32_t loopLength) {
  const NoteBaseline& bl = entry.baseline;
  const uint32_t targetOff = entry.shortenedEndTick;

  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(flat, loopLength, false);
  for (const NoteUtils::DisplayNote& dn : notes) {
    if (dn.note == bl.pitch && dn.startTick == bl.startTick && dn.endTick == targetOff) {
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

  insertNotePair(flat, channel, bl, targetOff);
}

}  // namespace

void resolveOverlapNotesForPreCommit(MidiEventVec& sessionStoreEvents, NoteEditFocus& focus, uint8_t channel,
                                     uint32_t loopLength) {
  if (!focus.active || loopLength == 0) {
    return;
  }

  for (auto& [ref, entry] : focus.overlapNotes) {
    (void)ref;
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

EditChangeList buildPreCommitOverlapEditChanges(const NoteEditFocus& focus) {
  EditChangeList changes;
  if (!focus.active) {
    return changes;
  }

  for (const auto& [ref, entry] : focus.overlapNotes) {
    (void)ref;
    if (entry.state == OverlapNoteStoreState::Hidden) {
      EditChange del;
      del.type = EditChangeType::DeleteNote;
      del.target = entry.ref;
      changes.push_back(del);
    }
  }

  for (const auto& [ref, entry] : focus.overlapNotes) {
    (void)ref;
    if (entry.state == OverlapNoteStoreState::Shortened &&
        entry.shortenedEndTick != entry.baseline.endTick) {
      EditChange ch;
      ch.type = EditChangeType::ChangeLength;
      ch.target = entry.ref;
      ch.newEndTick = entry.shortenedEndTick;
      changes.push_back(ch);
    }
  }

  return changes;
}

namespace {

const OverlapNote* findOverlapNoteForDisplayNote(const NoteEditFocus& focus, uint8_t channel,
                                                 const NoteUtils::DisplayNote& dn) {
  const NoteRef directRef =
      findBaselineRefForNote(focus, channel, dn.note, dn.startTick, dn.endTick);
  if (const OverlapNote* entry = findOverlapNoteEntry(focus, directRef)) {
    return entry;
  }
  for (const auto& [ref, entry] : focus.overlapNotes) {
    (void)ref;
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

bool isExcludedFromSelectableDisplayNotes(const NoteEditFocus& focus, uint8_t channel,
                                          const NoteUtils::DisplayNote& dn) {
  const OverlapNote* overlap = findOverlapNoteForDisplayNote(focus, channel, dn);
  if (overlap == nullptr) {
    return false;
  }
  if (overlap->state == OverlapNoteStoreState::Hidden) {
    return true;
  }
  return overlap->innerUnderMovingNote;
}

}  // namespace

std::vector<NoteUtils::DisplayNote> filterSelectableDisplayNotes(
    const MidiEventVec& sessionEvents, const NoteEditFocus& focus, uint8_t channel,
    uint32_t loopLength) {
  const std::vector<NoteUtils::DisplayNote> allNotes =
      NoteUtils::reconstructNotes(sessionEvents, loopLength, false);
  if (!focus.active || focus.overlapNotes.empty()) {
    return allNotes;
  }

  std::vector<NoteUtils::DisplayNote> filtered;
  filtered.reserve(allNotes.size());
  for (const NoteUtils::DisplayNote& dn : allNotes) {
    if (isExcludedFromSelectableDisplayNotes(focus, channel, dn)) {
      continue;
    }
    filtered.push_back(dn);
  }
  return filtered;
}

NoteRef noteRefFromFilteredDisplayNote(uint8_t channel, const NoteEditFocus& focus,
                                       const std::vector<NoteUtils::DisplayNote>& filtered,
                                       int filteredIndex) {
  if (filteredIndex < 0 || filteredIndex >= static_cast<int>(filtered.size())) {
    return {};
  }
  const NoteUtils::DisplayNote& dn = filtered[static_cast<size_t>(filteredIndex)];
  return findBaselineRefForNote(focus, channel, dn.note, dn.startTick, dn.endTick);
}

int filteredDisplayNoteIndexForNoteRef(uint8_t channel, const NoteEditFocus& focus,
                                       const std::vector<NoteUtils::DisplayNote>& filtered,
                                       const NoteRef& ref) {
  for (int i = 0; i < static_cast<int>(filtered.size()); ++i) {
    const NoteRef atIndex = noteRefFromFilteredDisplayNote(channel, focus, filtered, i);
    if (noteRefEquals(atIndex, ref)) {
      return i;
    }
    const NoteUtils::DisplayNote& dn = filtered[static_cast<size_t>(i)];
    if (atIndex.channel == ref.channel && atIndex.note == ref.note &&
        dn.startTick == ref.startTick && dn.endTick == ref.endTick) {
      return i;
    }
  }
  return -1;
}

EditChangeList buildPreCommitEditChanges(const NoteEditFocus& focus, uint8_t channel) {
  EditChangeList changes = buildPreCommitOverlapEditChanges(focus);
  if (!focus.active) {
    return changes;
  }

  const NoteRef moverRef{channel, focus.commitBaseline.pitch, focus.commitBaseline.startTick,
                         focus.commitBaseline.endTick};

  const bool startChanged = focus.last.startTick != focus.commitBaseline.startTick;
  const bool endChanged = focus.last.endTick != focus.commitBaseline.endTick;
  const bool pitchChanged = focus.last.pitch != focus.commitBaseline.pitch;

  if (startChanged) {
    EditChange move;
    move.type = EditChangeType::MoveNote;
    move.target = moverRef;
    move.newStartTick = focus.last.startTick;
    move.newEndTick = focus.last.endTick;
    changes.push_back(move);
  } else if (endChanged) {
    EditChange ch;
    ch.type = EditChangeType::ChangeLength;
    ch.target = moverRef;
    ch.newEndTick = focus.last.endTick;
    changes.push_back(ch);
  }

  if (pitchChanged) {
    EditChange pch;
    pch.type = EditChangeType::ChangePitch;
    pch.target = moverRef;
    pch.newPitch = focus.last.pitch;
    changes.push_back(pch);
  }

  return changes;
}
