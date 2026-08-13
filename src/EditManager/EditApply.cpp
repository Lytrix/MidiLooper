//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditApply.h"

#include "Utils/NoteUtils.h"

#include <algorithm>
#include <functional>

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
  const uint32_t startTick = onEvt.tick;

  for (size_t i = static_cast<size_t>(onIndex) + 1; i < events.size(); ++i) {
    const MidiEvent& evt = events[i];
    if (evt.isNoteOn() && evt.channel == channel && evt.data.noteData.note == note &&
        evt.tick > startTick) {
      break;
    }
    if (evt.isNoteOff() && evt.channel == channel && evt.data.noteData.note == note &&
        evt.tick >= startTick) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void applyDeleteNoteById(MidiEventVec& events, NoteId noteId) {
  const int onIndex = findNoteOnById(events, noteId);
  if (onIndex < 0) {
    return;
  }
  const int offIndex = findNoteOffForOnIndex(events, onIndex);
  std::vector<int> remove;
  remove.push_back(onIndex);
  if (offIndex >= 0) {
    remove.push_back(offIndex);
  }
  std::sort(remove.begin(), remove.end(), std::greater<int>());
  for (int idx : remove) {
    events.erase(events.begin() + idx);
  }
}

void applyMoveNoteById(MidiEventVec& events, NoteId noteId, uint32_t newStart, uint32_t newEnd) {
  const int onIndex = findNoteOnById(events, noteId);
  const int offIndex = findNoteOffForOnIndex(events, onIndex);
  if (onIndex >= 0) {
    events[static_cast<size_t>(onIndex)].tick = newStart;
  }
  if (offIndex >= 0) {
    events[static_cast<size_t>(offIndex)].tick = newEnd;
  }
}

void applyChangePitchById(MidiEventVec& events, NoteId noteId, uint8_t newPitch) {
  const int onIndex = findNoteOnById(events, noteId);
  const int offIndex = findNoteOffForOnIndex(events, onIndex);
  if (onIndex >= 0) {
    events[static_cast<size_t>(onIndex)].data.noteData.note = newPitch;
  }
  if (offIndex >= 0) {
    events[static_cast<size_t>(offIndex)].data.noteData.note = newPitch;
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
    applyDeleteNoteById(events, note.noteId);
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

void deleteNoteById(MidiEventVec& events, NoteId noteId) {
  applyDeleteNoteById(events, noteId);
}

void applyNoteEditPass(MidiEventVec& events, const EditPass& editPass, uint32_t loopLengthTicks) {
  switch (editPass.actionType) {
    case EditActionType::Delete:
      applyDeleteNoteById(events, editPass.targetNoteId);
      break;
    case EditActionType::Create:
      applyAddNote(events, editPass.addedEvents);
      break;
    case EditActionType::Update:
      switch (editPass.propertyType) {
        case EditPropertyType::NoteRange:
          applyMoveNoteById(events, editPass.targetNoteId, editPass.startTick, editPass.endTick);
          break;
        case EditPropertyType::Length:
          applyChangeLengthById(events, editPass.targetNoteId, editPass.endTick, loopLengthTicks);
          break;
        case EditPropertyType::Pitch:
          applyChangePitchById(events, editPass.targetNoteId, editPass.pitch);
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
