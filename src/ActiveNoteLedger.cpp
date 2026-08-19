//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "ActiveNoteLedger.h"

#include "Utils/TrackMem.h"

TRACK_COLD_MEM __attribute__((noinline)) bool ActiveNoteLedger::applyPlaybackEvent(
    uint8_t channel, const MidiEvent& evt) {
  return applyPlaybackEventBody(channel, evt);
}

TRACK_COLD_MEM __attribute__((noinline)) void ActiveNoteLedger::eraseOpenNotesMissingFromCommittedNoteOns(
    const MidiEvent* events, size_t eventCount) {
  eraseOpenNotesMissingFromCommittedNoteOnsBody(events, eventCount);
}
