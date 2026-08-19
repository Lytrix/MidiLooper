//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditApply.h"

#include "Utils/NoteUtils.h"

#include <algorithm>
#include <functional>
#include <vector>

namespace {

uint32_t inferLoopLength(const MidiEventVec& events, uint32_t hint) {
  if (hint > 0) {
    return hint;
  }
  uint32_t maxTick = 0;
  for (const MidiEvent& evt : events) {
    maxTick = std::max(maxTick, evt.tick);
  }
  return std::max<uint32_t>(maxTick + 1u, 768u);
}

int findNoteOffForOnIndex(const MidiEventVec& events, int onIndex) {
  if (onIndex < 0 || static_cast<size_t>(onIndex) >= events.size()) {
    return -1;
  }
  const MidiEvent& onEvt = events[static_cast<size_t>(onIndex)];
  const uint8_t channel = onEvt.channel;
  const uint8_t note = onEvt.data.noteData.note;

  // Same-pitch pairing is LIFO everywhere else (findLinearOffForNoteOnLifo,
  // findCorrespondingNoteOff, pairedNoteOnTickForOffAtIndex). Walk vector order,
  // except at an equal tick process Off before On (same keys as
  // sortMidiEventsChronologically / reconstruct / playback clock). Do not sort
  // by tick across the whole stream: capture stores sequential pairs
  // (On, Off, On, Off); a full tick sort would nest them (On, On, Off, Off) and
  // steal the wrong Off (pre-commit Length of the later note).
  // Tick-only gather is On then Off at a shared tick; vector-order LIFO then
  // gives the Off to the later On (121141 Off@168 → 6073).
  // Do not mutate the vector: applyChangeLengthById lengthen ends with tick-only
  // stable_sort and would restore On-before-Off for a later row.
  // Tick-sorted wrap capture (195941: Off@96 then On@2592) hits the off while the stack is
  // empty, so LIFO never pairs it. After the linear walk, pair a still-open on to its wrap
  // off (tick < on.tick) — tagged NoteId first, else the unique unpaired wrap off.
  std::vector<size_t> walkOrder;
  walkOrder.reserve(events.size());
  for (size_t i = 0; i < events.size(); ++i) {
    const MidiEvent& evt = events[i];
    if (evt.channel == channel && evt.data.noteData.note == note &&
        (evt.isNoteOn() || evt.isNoteOff())) {
      walkOrder.push_back(i);
    }
  }
  std::stable_sort(walkOrder.begin(), walkOrder.end(), [&](size_t a, size_t b) {
    const MidiEvent& ea = events[a];
    const MidiEvent& eb = events[b];
    if (ea.tick == eb.tick) {
      return NoteUtils::midiEventChronologicalLess(ea, eb);
    }
    return a < b;
  });

  std::vector<size_t> openNoteOnIndices;
  std::vector<uint8_t> offPaired(events.size(), 0);
  for (const size_t i : walkOrder) {
    const MidiEvent& evt = events[i];
    if (evt.channel != channel || evt.data.noteData.note != note) {
      continue;
    }
    if (evt.isNoteOn()) {
      openNoteOnIndices.push_back(i);
      continue;
    }
    if (!evt.isNoteOff() || openNoteOnIndices.empty()) {
      continue;
    }
    const size_t pairedOnIndex = openNoteOnIndices.back();
    openNoteOnIndices.pop_back();
    offPaired[i] = 1;
    if (pairedOnIndex == static_cast<size_t>(onIndex)) {
      return static_cast<int>(i);
    }
  }

  bool targetStillOpen = false;
  for (const size_t openIndex : openNoteOnIndices) {
    if (openIndex == static_cast<size_t>(onIndex)) {
      targetStillOpen = true;
      break;
    }
  }
  if (!targetStillOpen) {
    return -1;
  }

  int taggedWrapOff = -1;
  int untaggedWrapOff = -1;
  uint32_t untaggedWrapOffCount = 0;
  for (size_t i = 0; i < events.size(); ++i) {
    if (offPaired[i] != 0) {
      continue;
    }
    const MidiEvent& evt = events[i];
    if (!evt.isNoteOff() || evt.channel != channel || evt.data.noteData.note != note) {
      continue;
    }
    if (evt.tick >= onEvt.tick) {
      continue;
    }
    if (onEvt.noteId != kInvalidNoteId && evt.noteId == onEvt.noteId) {
      taggedWrapOff = static_cast<int>(i);
      break;
    }
    if (evt.noteId == kInvalidNoteId) {
      untaggedWrapOff = static_cast<int>(i);
      ++untaggedWrapOffCount;
    }
  }
  if (taggedWrapOff >= 0) {
    return taggedWrapOff;
  }
  if (untaggedWrapOffCount == 1) {
    return untaggedWrapOff;
  }
  return -1;
}

int findWrapHeadOffForOnIndex(const MidiEventVec& events, int onIndex, uint32_t loopLength) {
  if (onIndex < 0 || static_cast<size_t>(onIndex) >= events.size() || loopLength == 0) {
    return -1;
  }
  const MidiEvent& onEvt = events[static_cast<size_t>(onIndex)];
  const uint8_t channel = onEvt.channel;
  const uint8_t note = onEvt.data.noteData.note;
  int tagged = -1;
  int preferred = -1;
  uint32_t preferredCount = 0;
  for (size_t i = 0; i < events.size(); ++i) {
    const MidiEvent& evt = events[i];
    if (!evt.isNoteOff() || evt.channel != channel || evt.data.noteData.note != note) {
      continue;
    }
    uint32_t headOffTick = evt.tick;
    if (headOffTick >= loopLength) {
      headOffTick %= loopLength;
    }
    if (!NoteUtils::isPreferredWrapTailForHeadOff(onEvt.tick, headOffTick, events, note, channel,
                                                  loopLength)) {
      continue;
    }
    if (onEvt.noteId != kInvalidNoteId && evt.noteId == onEvt.noteId) {
      tagged = static_cast<int>(i);
      break;
    }
    preferred = static_cast<int>(i);
    ++preferredCount;
  }
  if (tagged >= 0) {
    return tagged;
  }
  if (preferredCount == 1) {
    return preferred;
  }
  return -1;
}

void eraseEventIndices(MidiEventVec& events, std::vector<int> indices) {
  std::sort(indices.begin(), indices.end(), std::greater<int>());
  int last = -1;
  for (int idx : indices) {
    if (idx < 0 || idx == last || static_cast<size_t>(idx) >= events.size()) {
      continue;
    }
    events.erase(events.begin() + idx);
    last = idx;
  }
}

void applyDeleteNoteById(MidiEventVec& events, NoteId noteId, uint32_t loopLength) {
  const int onIndex = findNoteOnById(events, noteId);
  if (onIndex < 0) {
    return;
  }
  const int lifoOff = findNoteOffForOnIndex(events, onIndex);
  const int wrapOff = findWrapHeadOffForOnIndex(events, onIndex, inferLoopLength(events, loopLength));
  std::vector<int> remove;
  remove.push_back(onIndex);
  if (lifoOff >= 0) {
    remove.push_back(lifoOff);
  }
  if (wrapOff >= 0) {
    remove.push_back(wrapOff);
  }
  eraseEventIndices(events, remove);
}

void applyMoveNoteById(MidiEventVec& events, NoteId noteId, uint32_t newStart, uint32_t newEnd,
                       uint32_t loopLength) {
  const int onIndex = findNoteOnById(events, noteId);
  if (onIndex < 0) {
    return;
  }
  loopLength = inferLoopLength(events, loopLength);
  const int lifoOff = findNoteOffForOnIndex(events, onIndex);
  const int wrapOff = findWrapHeadOffForOnIndex(events, onIndex, loopLength);
  events[static_cast<size_t>(onIndex)].tick = newStart;
  const int offToMove = wrapOff >= 0 ? wrapOff : lifoOff;
  if (offToMove >= 0) {
    events[static_cast<size_t>(offToMove)].tick = newEnd;
  } else {
    const MidiEvent& onEvt = events[static_cast<size_t>(onIndex)];
    events.push_back(MidiEvent::NoteOff(newEnd, onEvt.channel, onEvt.data.noteData.note, 0));
  }
  if (wrapOff >= 0 && lifoOff >= 0 && lifoOff != wrapOff) {
    eraseEventIndices(events, {lifoOff});
  }
}

void applyChangePitchById(MidiEventVec& events, NoteId noteId, uint8_t newPitch,
                          uint32_t loopLength) {
  const int onIndex = findNoteOnById(events, noteId);
  if (onIndex < 0) {
    return;
  }
  const int lifoOff = findNoteOffForOnIndex(events, onIndex);
  const int wrapOff = findWrapHeadOffForOnIndex(events, onIndex, inferLoopLength(events, loopLength));
  events[static_cast<size_t>(onIndex)].data.noteData.note = newPitch;
  if (lifoOff >= 0) {
    events[static_cast<size_t>(lifoOff)].data.noteData.note = newPitch;
  }
  if (wrapOff >= 0) {
    events[static_cast<size_t>(wrapOff)].data.noteData.note = newPitch;
  }
}

void applyChangeVelocityById(MidiEventVec& events, NoteId noteId, uint8_t newVelocity) {
  const int onIndex = findNoteOnById(events, noteId);
  if (onIndex >= 0) {
    events[static_cast<size_t>(onIndex)].data.noteData.velocity = newVelocity;
  }
}

void shortenNoteEndById(MidiEventVec& events, NoteId noteId, uint32_t newEndTick) {
  const int onIndex = findNoteOnById(events, noteId);
  const int offIndex = findNoteOffForOnIndex(events, onIndex);
  if (offIndex >= 0) {
    events[static_cast<size_t>(offIndex)].tick = newEndTick;
  }
}

void applyChangeLengthById(MidiEventVec& events, NoteId noteId, uint32_t newEnd,
                           uint32_t loopLength) {
  const int onIndex = findNoteOnById(events, noteId);
  if (onIndex < 0) {
    return;
  }
  const MidiEvent& onEvt = events[static_cast<size_t>(onIndex)];
  const uint32_t refStart = onEvt.tick;
  const int offIndex = findNoteOffForOnIndex(events, onIndex);
  loopLength = inferLoopLength(events, loopLength);
  const uint32_t refEnd =
      offIndex >= 0 ? events[static_cast<size_t>(offIndex)].tick : refStart;
  if (offIndex < 0 && newEnd == loopLength) {
    return;
  }

  if (newEnd == refEnd) {
    return;
  }

  if (newEnd < refEnd) {
    shortenNoteEndById(events, noteId, newEnd);
    return;
  }

  const uint32_t newStart = refStart;
  const uint32_t displayNewEnd = (newEnd > loopLength) ? (newEnd % loopLength) : newEnd;

  const std::vector<NoteUtils::DisplayNote> allNotes =
      NoteUtils::reconstructNotes(events, loopLength, false);
  std::vector<NoteUtils::DisplayNote> notesToDelete;
  std::vector<std::pair<NoteUtils::DisplayNote, uint32_t>> notesToShorten;

  for (const NoteUtils::DisplayNote& note : allNotes) {
    if (note.noteId == noteId) {
      continue;
    }
    if (note.note != onEvt.data.noteData.note) {
      continue;
    }
    if (!NoteUtils::notesOverlap(newStart, displayNewEnd, note.startTick, note.endTick,
                                 loopLength)) {
      continue;
    }

    bool noteCompletelyContained = false;
    if (displayNewEnd >= newStart) {
      noteCompletelyContained =
          (note.startTick >= newStart && note.endTick <= displayNewEnd);
    } else {
      noteCompletelyContained =
          (note.startTick >= newStart || note.endTick <= displayNewEnd);
    }

    if (noteCompletelyContained) {
      if (note.startTick > newStart && note.endTick == displayNewEnd) {
        continue;
      }
      notesToDelete.push_back(note);
      continue;
    }

    if (note.startTick >= newStart && note.endTick > displayNewEnd) {
      continue;
    }

    if (note.startTick == displayNewEnd && note.endTick > displayNewEnd) {
      notesToDelete.push_back(note);
      continue;
    }

    if (note.startTick < newStart) {
      const uint32_t shortenedEnd =
          (newStart == 0) ? (loopLength - 1) : (newStart - 1);
      notesToShorten.push_back({note, shortenedEnd});
    } else {
      notesToDelete.push_back(note);
    }
  }

  for (const auto& [note, shortenedEnd] : notesToShorten) {
    shortenNoteEndById(events, note.noteId, shortenedEnd);
  }
  for (const NoteUtils::DisplayNote& note : notesToDelete) {
    applyDeleteNoteById(events, note.noteId, loopLength);
  }

  const int refreshedOnIndex = findNoteOnById(events, noteId);
  const int refreshedOffIndex = findNoteOffForOnIndex(events, refreshedOnIndex);
  if (refreshedOffIndex >= 0) {
    events[static_cast<size_t>(refreshedOffIndex)].tick = newEnd;
  } else if (refreshedOnIndex >= 0) {
    const MidiEvent& refreshedOn = events[static_cast<size_t>(refreshedOnIndex)];
    events.push_back(
        MidiEvent::NoteOff(newEnd, refreshedOn.channel, refreshedOn.data.noteData.note, 0));
  }

  NoteUtils::orderSamePitchNoteOffsForLifo(events, onEvt.channel, onEvt.data.noteData.note);
  std::stable_sort(events.begin(), events.end(),
                   [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
}

void applyAddNote(MidiEventVec& events, const MidiEventVec& addedEvents) {
  for (const MidiEvent& evt : addedEvents) {
    events.push_back(evt);
  }
  std::sort(events.begin(), events.end(),
            [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
}

}  // namespace

int findNoteOnById(const MidiEventVec& events, NoteId noteId) {
  if (noteId == kInvalidNoteId) {
    return -1;
  }
  for (size_t i = 0; i < events.size(); ++i) {
    if (events[i].isNoteOn() && events[i].noteId == noteId) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

PersistIdentityResolve resolvePersistIdentityForExistingNote(
    const MidiEventVec& rematerializeEvents, uint8_t commitBaselinePitch,
    uint32_t commitBaselineStartTick) {
  PersistIdentityResolve result;
  NoteId uniqueId = kInvalidNoteId;
  for (const MidiEvent& evt : rematerializeEvents) {
    if (!evt.isNoteOn() || evt.data.noteData.note != commitBaselinePitch ||
        evt.tick != commitBaselineStartTick) {
      continue;
    }
    ++result.matchCount;
    uniqueId = evt.noteId;
  }
  if (result.matchCount == 1 && uniqueId != kInvalidNoteId) {
    result.status = PersistIdentityResolveStatus::Unique;
    result.noteId = uniqueId;
    return result;
  }
  if (result.matchCount > 1) {
    result.status = PersistIdentityResolveStatus::Ambiguous;
    return result;
  }
  result.status = PersistIdentityResolveStatus::Unresolved;
  return result;
}

static void retargetMoverEditPassRows(EditPassVec& rows, NoteId sessionNoteId, NoteId persistNoteId) {
  if (sessionNoteId == kInvalidNoteId || persistNoteId == kInvalidNoteId ||
      sessionNoteId == persistNoteId) {
    return;
  }
  for (EditPass& row : rows) {
    if (row.actionType != EditActionType::Update || row.targetNoteId != sessionNoteId) {
      continue;
    }
    switch (row.propertyType) {
      case EditPropertyType::NoteRange:
      case EditPropertyType::Pitch:
      case EditPropertyType::Length:
      case EditPropertyType::Velocity:
        row.targetNoteId = persistNoteId;
        break;
      default:
        break;
    }
  }
}

PersistIdentityReconcileResult reconcileMoverPersistIdentity(
    const MidiEventVec& rematerializeEvents, NoteId sessionNoteId, uint8_t commitBaselinePitch,
    uint32_t commitBaselineStartTick, EditPassVec& rows) {
  PersistIdentityReconcileResult result;
  if (sessionNoteId != kInvalidNoteId && findNoteOnById(rematerializeEvents, sessionNoteId) >= 0) {
    result.status = PersistIdentityReconcileStatus::AlreadyPresent;
    result.persistNoteId = sessionNoteId;
    return result;
  }
  const PersistIdentityResolve resolved = resolvePersistIdentityForExistingNote(
      rematerializeEvents, commitBaselinePitch, commitBaselineStartTick);
  result.matchCount = resolved.matchCount;
  if (resolved.status == PersistIdentityResolveStatus::Unique) {
    result.status = PersistIdentityReconcileStatus::Unique;
    result.persistNoteId = resolved.noteId;
    retargetMoverEditPassRows(rows, sessionNoteId, resolved.noteId);
    return result;
  }
  if (resolved.status == PersistIdentityResolveStatus::Ambiguous) {
    result.status = PersistIdentityReconcileStatus::Ambiguous;
    return result;
  }
  result.status = PersistIdentityReconcileStatus::Unresolved;
  return result;
}

void deleteNoteById(MidiEventVec& events, NoteId noteId) {
  applyDeleteNoteById(events, noteId, 0);
}

void applyNoteEditPass(MidiEventVec& events, const EditPass& editPass, uint32_t loopLengthTicks) {
  switch (editPass.actionType) {
    case EditActionType::Delete:
      applyDeleteNoteById(events, editPass.targetNoteId, loopLengthTicks);
      break;
    case EditActionType::Create:
      applyAddNote(events, editPass.addedEvents);
      break;
    case EditActionType::Update:
      switch (editPass.propertyType) {
        case EditPropertyType::NoteRange:
          applyMoveNoteById(events, editPass.targetNoteId, editPass.startTick, editPass.endTick,
                            loopLengthTicks);
          break;
        case EditPropertyType::Length:
          applyChangeLengthById(events, editPass.targetNoteId, editPass.endTick, loopLengthTicks);
          break;
        case EditPropertyType::Pitch:
          applyChangePitchById(events, editPass.targetNoteId, editPass.pitch, loopLengthTicks);
          break;
        case EditPropertyType::Velocity:
          applyChangeVelocityById(events, editPass.targetNoteId, editPass.velocity);
          break;
        default:
          break;
      }
      break;
  }
}

void applyNoteEditPassSequence(MidiEventVec& events, const EditPassVec& rows,
                               uint32_t loopLengthTicks) {
  // Rows locate their note by targetNoteId, so startTick / endTick are payload only. Before
  // stable NoteId they doubled as the span lookup key and a later row had to be re-pointed at
  // an earlier row's span; carrying that rewrite forward overwrote the payload of a second row
  // for the same note.
  for (const EditPass& row : rows) {
    applyNoteEditPass(events, row, loopLengthTicks);
  }
}

void applyNoteEditPassSequence(SessionMidiEventVec& events, const EditPassVec& rows,
                               uint32_t loopLengthTicks) {
  MidiEventVec scratch(events.begin(), events.end());
  applyNoteEditPassSequence(scratch, rows, loopLengthTicks);
  events.assign(scratch.begin(), scratch.end());
}
