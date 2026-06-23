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

bool isNoteOnFor(const MidiEvent& evt, uint8_t channel, uint8_t note) {
  return evt.isNoteOn() && evt.channel == channel && evt.data.noteData.note == note;
}

bool isNoteOffFor(const MidiEvent& evt, uint8_t channel, uint8_t note) {
  return evt.isNoteOff() && evt.channel == channel && evt.data.noteData.note == note;
}

int findNoteOnIndex(const MidiEventVec& events, const NoteRef& ref) {
  for (size_t i = 0; i < events.size(); ++i) {
    if (isNoteOnFor(events[i], ref.channel, ref.note) && events[i].tick == ref.startTick) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int findNoteOffIndex(const MidiEventVec& events, const NoteRef& ref, int skipIndex = -1) {
  for (size_t i = 0; i < events.size(); ++i) {
    if (static_cast<int>(i) == skipIndex) {
      continue;
    }
    if (isNoteOffFor(events[i], ref.channel, ref.note) && events[i].tick == ref.endTick) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int findNoteOffForRef(const MidiEventVec& events, const NoteRef& ref) {
  const int onIndex = findNoteOnIndex(events, ref);
  if (onIndex < 0) {
    return -1;
  }
  const int exact = findNoteOffIndex(events, ref, onIndex);
  if (exact >= 0) {
    return exact;
  }
  for (size_t i = static_cast<size_t>(onIndex) + 1; i < events.size(); ++i) {
    const MidiEvent& evt = events[i];
    if (isNoteOnFor(evt, ref.channel, ref.note) && evt.tick > ref.startTick) {
      break;
    }
    if (isNoteOffFor(evt, ref.channel, ref.note) && evt.tick >= ref.startTick) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void applyDeleteNote(MidiEventVec& events, const NoteRef& ref) {
  const int onIndex = findNoteOnIndex(events, ref);
  const int offIndex = findNoteOffIndex(events, ref, onIndex);
  std::vector<int> remove;
  if (onIndex >= 0) {
    remove.push_back(onIndex);
  }
  if (offIndex >= 0) {
    remove.push_back(offIndex);
  }
  std::sort(remove.begin(), remove.end(), std::greater<int>());
  for (int idx : remove) {
    events.erase(events.begin() + idx);
  }
}

void applyMoveNote(MidiEventVec& events, const NoteRef& ref, uint32_t newStart, uint32_t newEnd) {
  const int onIndex = findNoteOnIndex(events, ref);
  const int offIndex = findNoteOffIndex(events, ref, onIndex);
  if (onIndex >= 0) {
    events[onIndex].tick = newStart;
  }
  if (offIndex >= 0) {
    events[offIndex].tick = newEnd;
  }
}

void applyChangePitch(MidiEventVec& events, const NoteRef& ref, uint8_t newPitch) {
  const int onIndex = findNoteOnIndex(events, ref);
  const int offIndex = findNoteOffIndex(events, ref, onIndex);
  if (onIndex >= 0) {
    events[onIndex].data.noteData.note = newPitch;
  }
  if (offIndex >= 0) {
    events[offIndex].data.noteData.note = newPitch;
  }
}

void applyChangeVelocity(MidiEventVec& events, const NoteRef& ref, uint8_t newVelocity) {
  const int onIndex = findNoteOnIndex(events, ref);
  if (onIndex >= 0) {
    events[onIndex].data.noteData.velocity = newVelocity;
  }
}

void shortenNoteEnd(MidiEventVec& events, const NoteRef& ref, uint32_t newEndTick) {
  const int onIndex = findNoteOnIndex(events, ref);
  const int offIndex = findNoteOffIndex(events, ref, onIndex);
  if (offIndex >= 0) {
    events[offIndex].tick = newEndTick;
  }
}

void applyChangeLength(MidiEventVec& events, const NoteRef& ref, uint32_t newEnd,
                       uint32_t loopLength) {
  if (newEnd == ref.endTick) {
    return;
  }

  if (newEnd < ref.endTick) {
    shortenNoteEnd(events, ref, newEnd);
    return;
  }

  loopLength = inferLoopLength(events, loopLength);
  const uint32_t newStart = ref.startTick;
  const uint32_t displayNewEnd = newEnd % loopLength;

  const std::vector<NoteUtils::DisplayNote> allNotes =
      NoteUtils::reconstructNotes(events, loopLength, false);
  std::vector<NoteUtils::DisplayNote> notesToDelete;
  std::vector<std::pair<NoteUtils::DisplayNote, uint32_t>> notesToShorten;

  for (const NoteUtils::DisplayNote& note : allNotes) {
    if (note.note != ref.note) {
      continue;
    }
    if (note.startTick == ref.startTick && note.endTick == ref.endTick) {
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
    NoteRef overlapNoteRef{ref.channel, note.note, note.startTick, note.endTick};
    shortenNoteEnd(events, overlapNoteRef, shortenedEnd);
  }
  for (const NoteUtils::DisplayNote& note : notesToDelete) {
    NoteRef overlapNoteRef{ref.channel, note.note, note.startTick, note.endTick};
    applyDeleteNote(events, overlapNoteRef);
  }

  NoteRef currentRef = ref;
  const int onIndex = findNoteOnIndex(events, currentRef);
  const int offIndex = findNoteOffForRef(events, currentRef);
  if (offIndex >= 0) {
    events[offIndex].tick = newEnd;
  } else if (onIndex >= 0) {
    events.push_back(MidiEvent::NoteOff(newEnd, ref.channel, ref.note, 0));
  }

  NoteUtils::orderSamePitchNoteOffsForLifo(events, ref.channel, ref.note);
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

bool noteRefSameIdentity(const NoteRef& a, const NoteRef& b) {
  return a.channel == b.channel && a.note == b.note && a.startTick == b.startTick &&
         a.endTick == b.endTick;
}

}  // namespace

void applyNoteEditPass(MidiEventVec& events, const EditPass& editPass, uint32_t loopLengthTicks) {
  switch (editPass.actionType) {
    case EditActionType::Delete:
      applyDeleteNote(events, editPass.target);
      break;
    case EditActionType::Create:
      applyAddNote(events, editPass.addedEvents);
      break;
    case EditActionType::Update:
      switch (editPass.propertyType) {
        case EditPropertyType::NoteRange:
          applyMoveNote(events, editPass.target, editPass.startTick, editPass.endTick);
          break;
        case EditPropertyType::Length:
          applyChangeLength(events, editPass.target, editPass.endTick, loopLengthTicks);
          break;
        case EditPropertyType::Pitch:
          applyChangePitch(events, editPass.target, editPass.pitch);
          break;
        case EditPropertyType::Velocity:
          applyChangeVelocity(events, editPass.target, editPass.velocity);
          break;
        default:
          break;
      }
      break;
  }
}

void applyNoteEditPassSequence(MidiEventVec& events, const EditPassVec& rows,
                               uint32_t loopLengthTicks) {
  NoteRef trackedBaseline{};
  uint32_t trackedStart = 0;
  uint32_t trackedEnd = 0;
  bool tracked = false;

  for (const EditPass& row : rows) {
    EditPass resolved = row;
    if (tracked && noteRefSameIdentity(resolved.target, trackedBaseline)) {
      resolved.target.startTick = trackedStart;
      resolved.target.endTick = trackedEnd;
    }
    applyNoteEditPass(events, resolved, loopLengthTicks);
    if (resolved.actionType == EditActionType::Update &&
        resolved.propertyType == EditPropertyType::NoteRange) {
      trackedBaseline = row.target;
      trackedStart = row.startTick;
      trackedEnd = row.endTick;
      tracked = true;
    } else if (resolved.actionType == EditActionType::Update &&
               resolved.propertyType == EditPropertyType::Length) {
      trackedBaseline = row.target;
      trackedStart = row.target.startTick;
      trackedEnd = row.endTick;
      tracked = true;
    }
  }
}
