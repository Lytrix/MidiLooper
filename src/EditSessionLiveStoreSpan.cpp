//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditSessionLiveStoreSpan.h"

bool readLiveLinearSpan(const MidiEventVec& liveStore, NoteId noteId, uint8_t channel,
                        NoteBaseline& out) {
  const MidiEvent* noteOn = nullptr;
  for (const MidiEvent& event : liveStore) {
    if (event.type == midi::NoteOn && event.channel == channel && event.noteId == noteId) {
      noteOn = &event;
      break;
    }
  }
  if (noteOn == nullptr) {
    return false;
  }
  const MidiEvent* noteOff = nullptr;
  for (const MidiEvent& event : liveStore) {
    if (event.type == midi::NoteOff && event.channel == channel &&
        event.data.noteData.note == noteOn->data.noteData.note && event.tick >= noteOn->tick) {
      if (noteOff == nullptr || event.tick < noteOff->tick) {
        noteOff = &event;
      }
    }
  }
  if (noteOff == nullptr) {
    return false;
  }
  out.pitch = noteOn->data.noteData.note;
  out.velocity = noteOn->data.noteData.velocity;
  out.startTick = noteOn->tick;
  out.endTick = noteOff->tick;
  return true;
}

bool liveStoreHasNotePair(const MidiEventVec& liveStore, NoteId noteId, uint8_t channel) {
  NoteBaseline span{};
  return readLiveLinearSpan(liveStore, noteId, channel, span);
}
