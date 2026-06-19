//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditApply.h"

#include "Utils/NoteUtils.h"

#include <algorithm>
#include <functional>

namespace {

void collectActiveTakesSorted(const TakeVec& takes, std::vector<const Take*>& out) {
  out.clear();
  out.reserve(takes.size());
  for (const Take& take : takes) {
    if (take.state == TakeState::Active && !take.chunkRefs.empty()) {
      out.push_back(&take);
    }
  }
  std::sort(out.begin(), out.end(), [](const Take* a, const Take* b) {
    return a->mergeSequence < b->mergeSequence;
  });
}

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

/// A NoteRef identifies a single note by (channel, note, startTick, endTick): its note-on
/// sits at startTick and its note-off at endTick. We resolve exactly one note-on and one
/// note-off so an edit never bleeds onto a same-pitch overlap note that shares a tick.
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

/// Resolve note-off for a NoteRef; exact end tick first, then LIFO pair from note-on.
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
  // Erase higher indices first so earlier ones stay valid.
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

void applyAddNote(MidiEventVec& events, const EditChange& change) {
  for (const MidiEvent& evt : change.addedEvents) {
    events.push_back(evt);
  }
  std::sort(events.begin(), events.end(),
            [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
}

bool noteRefSameIdentity(const NoteRef& a, const NoteRef& b) {
  return a.channel == b.channel && a.note == b.note && a.startTick == b.startTick &&
         a.endTick == b.endTick;
}

void applyEditChangeList(MidiEventVec& events, const EditChangeList& changes,
                         uint32_t loopLengthTicks) {
  NoteRef trackedBaseline{};
  uint32_t trackedStart = 0;
  uint32_t trackedEnd = 0;
  bool tracked = false;

  for (const EditChange& change : changes) {
    EditChange resolved = change;
    if (tracked) {
      if (noteRefSameIdentity(resolved.target, trackedBaseline)) {
        resolved.target.startTick = trackedStart;
        resolved.target.endTick = trackedEnd;
      }
    }
    applyEditChange(events, resolved, loopLengthTicks);
    if (resolved.type == EditChangeType::MoveNote) {
      trackedBaseline = change.target;
      trackedStart = change.newStartTick;
      trackedEnd = change.newEndTick;
      tracked = true;
    } else if (resolved.type == EditChangeType::ChangeLength) {
      trackedBaseline = change.target;
      trackedStart = change.target.startTick;
      trackedEnd = change.newEndTick;
      tracked = true;
    }
  }
}

}  // namespace

void applyEditChange(MidiEventVec& events, const EditChange& change, uint32_t loopLengthTicks) {
  switch (change.type) {
    case EditChangeType::DeleteNote:
      applyDeleteNote(events, change.target);
      break;
    case EditChangeType::MoveNote:
      applyMoveNote(events, change.target, change.newStartTick, change.newEndTick);
      break;
    case EditChangeType::ChangePitch:
      applyChangePitch(events, change.target, change.newPitch);
      break;
    case EditChangeType::ChangeLength:
      applyChangeLength(events, change.target, change.newEndTick, loopLengthTicks);
      break;
    case EditChangeType::AddNote:
      applyAddNote(events, change);
      break;
  }
}

void applyEditsToFlat(const TakeVec& takes, const EditVec& edits, MidiEventVec& out,
                      uint32_t loopLengthTicks) {
  out.clear();
  std::vector<const Take*> active;
  collectActiveTakesSorted(takes, active);
  for (const Take* take : active) {
    LoopEventStore::appendFlattenedChunkIds(take->chunkRefs, out);
  }

  for (const Edit& edit : edits) {
    if (edit.state != EditState::Active) {
      continue;
    }
    applyEditChangeList(out, edit.changes, loopLengthTicks);
  }
}

void applyEdits(const TakeVec& takes, const EditVec& edits, LoopEventStore& out,
                uint32_t loopLengthTicks) {
  MidiEventVec flat;
  applyEditsToFlat(takes, edits, flat, loopLengthTicks);
  out.clear();
  if (!flat.empty()) {
    out.loadFromFlat(flat);
  }
}
