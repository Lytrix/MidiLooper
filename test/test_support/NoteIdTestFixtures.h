//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "MidiEvent.h"
#include "LoopEventBuffer.h"
#include "LoopEventStore.h"
#include "MidiEvent.h"
#include "Utils/NoteUtils.h"

namespace NoteIdTestFixtures {

inline NoteId& nextNoteIdCounter() {
  static NoteId next = 1;
  return next;
}

inline void resetNoteIdCounter(NoteId start = 1) { nextNoteIdCounter() = start; }

inline MidiEvent noteOnWithNoteId(uint32_t tick, uint8_t channel, uint8_t pitch,
                                  uint8_t velocity = 100, NoteId id = kInvalidNoteId) {
  MidiEvent evt = MidiEvent::NoteOn(tick, channel, pitch, velocity);
  evt.noteId = (id != kInvalidNoteId) ? id : nextNoteIdCounter()++;
  return evt;
}

inline bool storeAppendNoteOn(LoopEventStore& store, uint32_t tick, uint8_t channel,
                              uint8_t pitch, uint8_t velocity = 100, NoteId id = kInvalidNoteId) {
  return store.append(noteOnWithNoteId(tick, channel, pitch, velocity, id));
}

inline NoteId noteIdForNoteOn(const MidiEventVec& events, uint8_t channel, uint8_t pitch,
                               uint32_t startTick) {
  for (const MidiEvent& evt : events) {
    if (evt.isNoteOn() && evt.channel == channel && evt.data.noteData.note == pitch &&
        evt.tick == startTick) {
      return evt.noteId;
    }
  }
  return kInvalidNoteId;
}

inline void assignSequentialNoteIds(MidiEventVec& events, NoteId start = 1) {
  NoteId next = start;
  for (MidiEvent& evt : events) {
    if (evt.isNoteOn() && evt.noteId == kInvalidNoteId) {
      evt.noteId = next++;
    }
  }
}

inline NoteUtils::DisplayNote displayNote(NoteId noteId, uint8_t pitch, uint8_t velocity,
                                          uint32_t startTick, uint32_t endTick) {
  NoteUtils::DisplayNote dn{};
  dn.noteId = noteId;
  dn.note = pitch;
  dn.velocity = velocity;
  dn.startTick = startTick;
  dn.endTick = endTick;
  return dn;
}

}  // namespace
