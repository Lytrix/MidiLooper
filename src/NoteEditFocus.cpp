//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteEditFocus.h"

#include <algorithm>

#include "Utils/NoteMovementWrap.h"
#include "Utils/NoteUtils.h"

NoteBaseline baselineFromDisplayNote(const NoteUtils::DisplayNote& dn) {
  return NoteBaseline{dn.note, dn.velocity, dn.startTick, dn.endTick};
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

OverlapNote* findOverlapNoteEntry(NoteEditFocus& focus, NoteId noteId) {
  const auto it = focus.overlapNotes.find(noteId);
  return it == focus.overlapNotes.end() ? nullptr : &it->second;
}

const OverlapNote* findOverlapNoteEntry(const NoteEditFocus& focus, NoteId noteId) {
  const auto it = focus.overlapNotes.find(noteId);
  return it == focus.overlapNotes.end() ? nullptr : &it->second;
}

NoteId findBaselineNoteIdForDisplay(const NoteEditFocus& focus,
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

NoteBaseline baselineForDisplayNote(const NoteEditFocus& focus,
                                    const NoteUtils::DisplayNote& dn) {
  const NoteId noteId = findBaselineNoteIdForDisplay(focus, dn);
  const auto it = focus.baselineMap.find(noteId);
  if (it != focus.baselineMap.end()) {
    return it->second;
  }
  return baselineFromDisplayNote(dn);
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
  (void)channel;
  focus.clear();
  if (loopLength == 0) {
    return;
  }

  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(loopMidiEvents, loopLength, false);
  for (const NoteUtils::DisplayNote& dn : notes) {
    if (dn.noteId != kInvalidNoteId) {
      focus.baselineMap[dn.noteId] = baselineFromDisplayNote(dn);
    }
  }

  if (selectedNoteIdx < 0 || selectedNoteIdx >= static_cast<int>(notes.size())) {
    return;
  }

  const NoteUtils::DisplayNote& selected = notes[static_cast<size_t>(selectedNoteIdx)];
  focus.movingNoteId = selected.noteId;
  focus.commitBaseline = baselineFromDisplayNote(selected);
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
                    uint32_t endTick, NoteId noteId) {
  MidiEvent noteOn = MidiEvent::NoteOn(bl.startTick, channel, bl.pitch, bl.velocity);
  noteOn.noteId = noteId;
  flat.push_back(noteOn);
  flat.push_back(MidiEvent::NoteOff(endTick, channel, bl.pitch, 0));
}

void materializeShortenedOverlap(MidiEventVec& flat, uint8_t channel, const OverlapNote& entry,
                                 uint32_t loopLength) {
  const NoteBaseline& bl = entry.baseline;
  const uint32_t targetOff = entry.shortenedEndTick;

  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(flat, loopLength, false);
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

void resolveOverlapNotesForPreCommit(MidiEventVec& sessionStoreEvents, NoteEditFocus& focus,
                                     uint8_t channel, uint32_t loopLength) {
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

namespace {

EditPass makeNoteEditRow(EditActionType actionType, EditPropertyType propertyType) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = actionType;
  row.propertyType = propertyType;
  row.state = EditPassState::Active;
  return row;
}

}  // namespace

EditPassVec buildPreCommitOverlapEditPasses(const NoteEditFocus& focus) {
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

const OverlapNote* findOverlapNoteForDisplayNote(const NoteEditFocus& focus,
                                                 const NoteUtils::DisplayNote& dn) {
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

bool isExcludedFromSelectableDisplayNotes(const NoteEditFocus& focus,
                                          const NoteUtils::DisplayNote& dn) {
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

std::vector<NoteUtils::DisplayNote> filterSelectableDisplayNotes(
    const MidiEventVec& sessionEvents, const NoteEditFocus& focus, uint8_t channel,
    uint32_t loopLength) {
  (void)channel;
  const std::vector<NoteUtils::DisplayNote> allNotes =
      NoteUtils::reconstructNotes(sessionEvents, loopLength, false);
  if (!focus.active || focus.overlapNotes.empty()) {
    return allNotes;
  }

  std::vector<NoteUtils::DisplayNote> filtered;
  filtered.reserve(allNotes.size());
  for (const NoteUtils::DisplayNote& dn : allNotes) {
    if (isExcludedFromSelectableDisplayNotes(focus, dn)) {
      continue;
    }
    filtered.push_back(dn);
  }
  return filtered;
}

NoteId noteIdFromFilteredDisplayNote(const std::vector<NoteUtils::DisplayNote>& filtered,
                                     int filteredIndex) {
  if (filteredIndex < 0 || filteredIndex >= static_cast<int>(filtered.size())) {
    return kInvalidNoteId;
  }
  return filtered[static_cast<size_t>(filteredIndex)].noteId;
}

int filteredDisplayNoteIndexForNoteId(const std::vector<NoteUtils::DisplayNote>& filtered,
                                      NoteId noteId) {
  if (noteId == kInvalidNoteId) {
    return -1;
  }
  for (int i = 0; i < static_cast<int>(filtered.size()); ++i) {
    if (filtered[static_cast<size_t>(i)].noteId == noteId) {
      return i;
    }
  }
  return -1;
}

EditPassVec buildPreCommitEditPasses(const NoteEditFocus& focus, uint8_t channel) {
  EditPassVec rows = buildPreCommitOverlapEditPasses(focus);
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
