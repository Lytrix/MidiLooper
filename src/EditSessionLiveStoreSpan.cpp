//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditSessionLiveStoreSpan.h"
#include "Utils/NoteEditMem.h"

#include <vector>

namespace {

NOTE_EDIT_MEM bool noteOnIdentityEqual(const MidiEvent& left, const MidiEvent& right) {
  return left.noteId == right.noteId && left.tick == right.tick &&
         left.data.noteData.note == right.data.noteData.note;
}

NOTE_EDIT_MEM bool findLifoOffTickForNoteOn(const MidiEventVec& events, const MidiEvent& noteOn,
                                            uint32_t& outOffTick) {
  const uint8_t pitch = noteOn.data.noteData.note;
  std::vector<const MidiEvent*> activeNoteOnStack;
  for (const MidiEvent& evt : events) {
    if (evt.channel != noteOn.channel) {
      continue;
    }
    const bool isNoteOn =
        evt.isNoteOn() && evt.data.noteData.velocity > 0 && evt.data.noteData.note == pitch;
    const bool isNoteOff = evt.isNoteOff() && evt.data.noteData.note == pitch;
    if (isNoteOn) {
      activeNoteOnStack.push_back(&evt);
    } else if (isNoteOff) {
      for (int stackIndex = static_cast<int>(activeNoteOnStack.size()) - 1; stackIndex >= 0;
           --stackIndex) {
        const MidiEvent* candidateOn = activeNoteOnStack[static_cast<size_t>(stackIndex)];
        if (evt.tick <= candidateOn->tick) {
          continue;
        }
        const MidiEvent* pairedOn = candidateOn;
        activeNoteOnStack.erase(activeNoteOnStack.begin() + stackIndex);
        if (noteOnIdentityEqual(*pairedOn, noteOn)) {
          outOffTick = evt.tick;
          return true;
        }
        break;
      }
    }
  }
  return false;
}

}  // namespace

NOTE_EDIT_MEM bool readLiveLinearSpan(const MidiEventVec& liveStore, NoteId noteId, uint8_t channel,
                        NoteBaseline& out) {
  if (noteId == kInvalidNoteId) {
    return false;
  }

  const MidiEvent* noteOn = nullptr;
  for (const MidiEvent& event : liveStore) {
    if (event.channel == channel && event.noteId == noteId && event.isNoteOn() &&
        event.data.noteData.velocity > 0) {
      noteOn = &event;
      break;
    }
  }
  if (noteOn == nullptr) {
    for (const MidiEvent& event : liveStore) {
      if (event.noteId == noteId && event.isNoteOn() && event.data.noteData.velocity > 0) {
        noteOn = &event;
        break;
      }
    }
  }
  if (noteOn == nullptr) {
    return false;
  }

  // Prefer noteId-tagged offs when present (RestoreNote / stamped apply paths).
  const MidiEvent* taggedOff = nullptr;
  for (const MidiEvent& event : liveStore) {
    if (!event.isNoteOff() || event.channel != noteOn->channel || event.noteId != noteId ||
        event.tick <= noteOn->tick ||
        event.data.noteData.note != noteOn->data.noteData.note) {
      continue;
    }
    if (taggedOff == nullptr || event.tick > taggedOff->tick) {
      taggedOff = &event;
    }
  }
  if (taggedOff != nullptr) {
    out.pitch = noteOn->data.noteData.note;
    out.velocity = noteOn->data.noteData.velocity;
    out.startTick = noteOn->tick;
    out.endTick = taggedOff->tick;
    return true;
  }

  // Note-offs are often noteId-less (MidiEvent contract: identity on note-on only).
  // LIFO pitch pairing matches findLinearOffForNoteId fallback — never nearest-off-by-pitch,
  // which steals a same-pitch neighbor's end when spans overlap in the store.
  uint32_t lifoOffTick = 0;
  if (!findLifoOffTickForNoteOn(liveStore, *noteOn, lifoOffTick)) {
    return false;
  }
  out.pitch = noteOn->data.noteData.note;
  out.velocity = noteOn->data.noteData.velocity;
  out.startTick = noteOn->tick;
  out.endTick = lifoOffTick;
  return true;
}

NOTE_EDIT_MEM bool findCommittedLinearSpanForPitchStart(MidiEventVec& committedEvents,
                                                          uint8_t channel, uint8_t pitch,
                                                          uint32_t startTick, uint32_t loopLength,
                                                          NoteBaseline& out) {
  const MidiEvent* noteOn = nullptr;
  for (const MidiEvent& evt : committedEvents) {
    if (evt.channel == channel && evt.isNoteOn() && evt.data.noteData.velocity > 0 &&
        evt.data.noteData.note == pitch && evt.tick == startTick) {
      noteOn = &evt;
      break;
    }
  }
  if (noteOn == nullptr) {
    for (const MidiEvent& evt : committedEvents) {
      if (evt.isNoteOn() && evt.data.noteData.velocity > 0 && evt.data.noteData.note == pitch &&
          evt.tick == startTick) {
        noteOn = &evt;
        break;
      }
    }
  }
  if (noteOn == nullptr) {
    return false;
  }

  uint32_t offTick = 0;
  if (!findLifoOffTickForNoteOn(committedEvents, *noteOn, offTick)) {
    return false;
  }
  if (offTick <= startTick) {
    return false;
  }
  if (loopLength > 0 && offTick > startTick + loopLength) {
    return false;
  }
  out.pitch = pitch;
  out.velocity = noteOn->data.noteData.velocity;
  out.startTick = startTick;
  out.endTick = offTick;
  return true;
}

NOTE_EDIT_MEM NoteId findLiveNoteIdForPitchStart(const MidiEventVec& liveStore, uint8_t channel,
                                                 uint8_t pitch, uint32_t startTick) {
  for (const MidiEvent& evt : liveStore) {
    if (evt.channel != channel || !evt.isNoteOn() || evt.data.noteData.velocity == 0) {
      continue;
    }
    if (evt.data.noteData.note == pitch && evt.tick == startTick &&
        evt.noteId != kInvalidNoteId) {
      return evt.noteId;
    }
  }
  for (const MidiEvent& evt : liveStore) {
    if (!evt.isNoteOn() || evt.data.noteData.velocity == 0) {
      continue;
    }
    if (evt.data.noteData.note == pitch && evt.tick == startTick &&
        evt.noteId != kInvalidNoteId) {
      return evt.noteId;
    }
  }
  return kInvalidNoteId;
}

NOTE_EDIT_MEM bool readLiveLinearSpanForPitchStart(const MidiEventVec& liveStore, uint8_t channel,
                                                   uint8_t pitch, uint32_t startTick,
                                                   NoteId& outNoteId, NoteBaseline& out) {
  const MidiEvent* noteOn = nullptr;
  for (const MidiEvent& event : liveStore) {
    if (event.channel != channel || !event.isNoteOn() || event.data.noteData.velocity == 0) {
      continue;
    }
    if (event.data.noteData.note == pitch && event.tick == startTick) {
      noteOn = &event;
      break;
    }
  }
  if (noteOn == nullptr) {
    for (const MidiEvent& event : liveStore) {
      if (!event.isNoteOn() || event.data.noteData.velocity == 0) {
        continue;
      }
      if (event.data.noteData.note == pitch && event.tick == startTick) {
        noteOn = &event;
        break;
      }
    }
  }
  if (noteOn == nullptr) {
    return false;
  }
  outNoteId = noteOn->noteId;
  if (outNoteId != kInvalidNoteId &&
      readLiveLinearSpan(liveStore, outNoteId, channel, out)) {
    return true;
  }

  const MidiEvent* taggedOff = nullptr;
  for (const MidiEvent& event : liveStore) {
    if (!event.isNoteOff() || event.channel != noteOn->channel || event.tick <= noteOn->tick ||
        event.data.noteData.note != pitch) {
      continue;
    }
    if (event.noteId != kInvalidNoteId && event.noteId != outNoteId) {
      continue;
    }
    if (taggedOff == nullptr || event.tick > taggedOff->tick) {
      taggedOff = &event;
    }
  }
  if (taggedOff != nullptr) {
    out.pitch = pitch;
    out.velocity = noteOn->data.noteData.velocity;
    out.startTick = startTick;
    out.endTick = taggedOff->tick;
    return true;
  }

  uint32_t lifoOffTick = 0;
  if (!findLifoOffTickForNoteOn(liveStore, *noteOn, lifoOffTick)) {
    return false;
  }
  out.pitch = pitch;
  out.velocity = noteOn->data.noteData.velocity;
  out.startTick = startTick;
  out.endTick = lifoOffTick;
  return true;
}

NOTE_EDIT_MEM bool liveStoreHasNotePair(const MidiEventVec& liveStore, NoteId noteId, uint8_t channel) {
  NoteBaseline span{};
  return readLiveLinearSpan(liveStore, noteId, channel, span);
}

NOTE_EDIT_MEM void sortNoteIdVector(std::vector<NoteId, InternalHeapFirstAllocator<NoteId>>& ids) {
  for (size_t i = 1; i < ids.size(); ++i) {
    const NoteId key = ids[i];
    size_t j = i;
    while (j > 0 && ids[j - 1] > key) {
      ids[j] = ids[j - 1];
      --j;
    }
    ids[j] = key;
  }
}
