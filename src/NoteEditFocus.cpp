//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteEditFocus.h"

#include <algorithm>
#include <unordered_set>

#include "Utils/NoteMovementWrap.h"
#include "Utils/LoopTickNormalize.h"
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

namespace {

bool isDisplayWrappedBaseline(const NoteBaseline& baseline) {
  return baseline.endTick < baseline.startTick;
}

}  // namespace

bool isPlausibleStorageSpan(uint32_t startTick, uint32_t endTick, uint32_t loopLength) {
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

bool isInflatedDisplaySpan(const NoteUtils::DisplayNote& dn, uint32_t loopLength) {
  if (loopLength == 0 || dn.endTick < dn.startTick) {
    return dn.endTick < dn.startTick;
  }
  return (dn.endTick - dn.startTick) > loopLength / 2;
}

NoteBaseline linearBaselineForOverlapRestore(const NoteEditFocus& focus, const OverlapNote& entry,
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

bool resolveLinearNoteSpanForOverlap(const NoteEditFocus& focus, MidiEventVec& events,
                                     uint8_t channel, const NoteUtils::DisplayNote& dn,
                                     NoteBaseline& out, uint32_t loopLength) {
  const NoteId noteId = findBaselineNoteIdForDisplay(focus, dn);
  if (noteId != kInvalidNoteId &&
      findLinearNoteSpanForNoteId(events, noteId, channel, out, dn.startTick, loopLength)) {
    return true;
  }
  if (noteId != kInvalidNoteId &&
      findLinearNoteSpanForNoteId(events, noteId, channel, out, UINT32_MAX, loopLength)) {
    return true;
  }
  if (noteId != kInvalidNoteId) {
    const auto mapIt = focus.baselineMap.find(noteId);
    if (mapIt != focus.baselineMap.end() &&
        mapIt->second.endTick >= mapIt->second.startTick &&
        mapIt->second.pitch == dn.note && mapIt->second.startTick == dn.startTick &&
        (loopLength == 0 ||
         isPlausibleStorageSpan(mapIt->second.startTick, mapIt->second.endTick, loopLength))) {
      out = mapIt->second;
      return true;
    }
  }
  if (dn.endTick >= dn.startTick &&
      (loopLength == 0 || !isInflatedDisplaySpan(dn, loopLength)) &&
      (loopLength == 0 ||
       isPlausibleStorageSpan(dn.startTick, dn.endTick, loopLength))) {
    out = baselineFromDisplayNote(dn);
    return true;
  }
  return false;
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
  std::unordered_set<NoteId> noteIds;
  for (const NoteUtils::DisplayNote& dn : notes) {
    if (dn.noteId != kInvalidNoteId) {
      noteIds.insert(dn.noteId);
    }
  }
  MidiEventVec mutableEvents = loopMidiEvents;
  for (NoteId noteId : noteIds) {
    NoteBaseline linear;
    if (findLinearNoteSpanForNoteId(mutableEvents, noteId, channel, linear, UINT32_MAX,
                                    loopLength)) {
      focus.baselineMap[noteId] = linear;
    }
  }
  for (const NoteUtils::DisplayNote& dn : notes) {
    if (dn.noteId == kInvalidNoteId || focus.baselineMap.count(dn.noteId) > 0) {
      continue;
    }
    focus.baselineMap[dn.noteId] = baselineFromDisplayNote(dn);
  }

  if (selectedNoteIdx < 0 || selectedNoteIdx >= static_cast<int>(notes.size())) {
    return;
  }

  const NoteUtils::DisplayNote& selected = notes[static_cast<size_t>(selectedNoteIdx)];
  focus.movingNoteId = selected.noteId;
  NoteBaseline linearBaseline;
  if (findLinearNoteSpanForNoteId(mutableEvents, selected.noteId, channel, linearBaseline,
                                  UINT32_MAX, loopLength)) {
    focus.commitBaseline = linearBaseline;
  } else {
    focus.commitBaseline = baselineFromDisplayNote(selected);
  }
  focus.movingNoteRange.start = focus.commitBaseline.startTick;
  focus.movingNoteRange.end = focus.commitBaseline.endTick;
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
  if (end >= focus.movingNoteRange.end || end > loopLength) {
    focus.movingNoteRange.end = end;
  }
}

bool isMovingNoteOverlapScratchEntry(const NoteEditFocus& focus, NoteId noteId,
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

bool noteEditFocusHasPendingLengthChange(const NoteEditFocus& focus) {
  return focus.active && focus.last.endTick != focus.commitBaseline.endTick;
}

namespace {

MidiEvent* findLinearOffForNoteOnLifo(MidiEventVec& events, MidiEvent* noteOnEvent, uint8_t pitch) {
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

MidiEvent* findPlausibleOffForNoteOn(MidiEventVec& events, const MidiEvent& noteOn,
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

MidiEvent* findLinearOffForNoteId(MidiEventVec& events, const MidiEvent& noteOn, NoteId noteId,
                                  uint32_t loopLength) {
  if (noteId == kInvalidNoteId) {
    return nullptr;
  }
  MidiEvent* farthestOff = nullptr;
  for (auto& evt : events) {
    if (!evt.isNoteOff() || evt.noteId != noteId || evt.tick <= noteOn.tick) {
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

bool findLinearNoteSpanForNoteId(MidiEventVec& events, NoteId noteId, uint8_t channel,
                                 NoteBaseline& outBaseline, uint32_t preferredStartTick,
                                 uint32_t loopLength) {
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
      if (loopLength > 0) {
        noteOffEvent = findPlausibleOffForNoteOn(events, evt, loopLength);
      } else {
        noteOffEvent = findLinearOffForNoteOnLifo(events, &evt, evt.data.noteData.note);
      }
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

bool syncNoteEditFocusLinearFromSessionStore(NoteEditFocus& focus, MidiEventVec& events,
                                            uint8_t channel, uint32_t loopLength) {
  if (!focus.active) {
    return false;
  }
  NoteBaseline linearSpan;
  if (focus.movingNoteId != kInvalidNoteId &&
      (findLinearNoteSpanForNoteId(events, focus.movingNoteId, channel, linearSpan,
                                   focus.last.startTick, loopLength) ||
       findLinearNoteSpanForNoteId(events, focus.movingNoteId, channel, linearSpan,
                                   focus.commitBaseline.startTick, loopLength) ||
       findLinearNoteSpanForNoteId(events, focus.movingNoteId, channel, linearSpan, UINT32_MAX,
                                   loopLength))) {
    focus.last = linearSpan;
    focus.movingNoteRange.start = linearSpan.startTick;
    focus.movingNoteRange.end = linearSpan.endTick;
    return true;
  }
  const uint32_t startCandidates[] = {focus.last.startTick, focus.commitBaseline.startTick};
  for (uint32_t startTick : startCandidates) {
    for (auto& evt : events) {
      if (evt.channel != channel) {
        continue;
      }
      if (!evt.isNoteOn() || evt.data.noteData.velocity == 0 ||
          evt.data.noteData.note != focus.last.pitch || evt.tick != startTick) {
        continue;
      }
      MidiEvent* noteOffEvent =
          findLinearOffForNoteOnLifo(events, &evt, focus.last.pitch);
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
  }
  return false;
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

void pruneOverlapNotesBeforePreCommit(NoteEditFocus& focus, MidiEventVec& events,
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

std::unordered_set<NoteId> buildEditClosureNoteIds(const NoteEditFocus& focus,
                                                 const MidiEventVec& sessionEvents,
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
  const uint8_t pitch = focus.last.pitch;
  const uint32_t wrapMargin =
      std::min(LoopTickNormalize::kDefaultWrapWindowTicks, loopLength);

  for (const MidiEvent& onEvt : sessionEvents) {
    if (!onEvt.isNoteOn() || onEvt.data.noteData.velocity == 0 || onEvt.channel != channel ||
        onEvt.noteId == kInvalidNoteId || onEvt.data.noteData.note != pitch ||
        ids.count(onEvt.noteId) > 0) {
      continue;
    }
    for (const MidiEvent& offEvt : sessionEvents) {
      if (!offEvt.isNoteOff() || offEvt.channel != channel ||
          offEvt.data.noteData.note != pitch) {
        continue;
      }
      if (NoteUtils::isHeadTailWrappedPair(onEvt.tick, offEvt.tick, loopLength, wrapMargin)) {
        ids.insert(onEvt.noteId);
        break;
      }
    }
  }
  return ids;
}

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

int filteredDisplayNoteIndexForNoteIdAndStart(const std::vector<NoteUtils::DisplayNote>& filtered,
                                              NoteId noteId, uint32_t startTick) {
  if (noteId == kInvalidNoteId) {
    return -1;
  }
  for (int i = 0; i < static_cast<int>(filtered.size()); ++i) {
    const NoteUtils::DisplayNote& dn = filtered[static_cast<size_t>(i)];
    if (dn.noteId == noteId && dn.startTick == startTick) {
      return i;
    }
  }
  return -1;
}

int filteredDisplayNoteIndexForMovingNote(const std::vector<NoteUtils::DisplayNote>& filtered,
                                          NoteId noteId, uint32_t linearStartTick) {
  return filteredDisplayNoteIndexForNoteIdAndStart(filtered, noteId, linearStartTick);
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
