//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "ActiveNoteLedger.h"

#include "Utils/TrackMem.h"

TRACK_COLD_MEM __attribute__((noinline)) bool ActiveNoteLedger::applyPlaybackEvent(
    uint8_t channel, const MidiEvent& evt) {
  return applyPlaybackEventBody(channel, evt);
}

TRACK_COLD_MEM __attribute__((noinline)) ActiveNoteLedger::HandoffClosurePlan
ActiveNoteLedger::planHandoffClosures(const MidiEvent* events, size_t eventCount,
                                      uint32_t servedThroughPhase, uint32_t loopLength) const {
  return planHandoffClosuresBody(events, eventCount, servedThroughPhase, loopLength);
}

TRACK_COLD_MEM __attribute__((noinline)) ActiveNoteLedger::HandoffClosurePlan
ActiveNoteLedger::planOpenNotesMissingFromCommittedNoteOns(const MidiEvent* events,
                                                           size_t eventCount) const {
  return planOpenNotesMissingFromCommittedNoteOnsBody(events, eventCount);
}

TRACK_COLD_MEM __attribute__((noinline)) size_t ActiveNoteLedger::erasePlannedOpenNotes(
    const HandoffClosurePlan& plan) {
  return erasePlannedOpenNotesBody(plan);
}

TRACK_COLD_MEM __attribute__((noinline)) void ActiveNoteLedger::eraseOpenNotesMissingFromCommittedNoteOns(
    const MidiEvent* events, size_t eventCount) {
  (void)erasePlannedOpenNotesBody(planOpenNotesMissingFromCommittedNoteOnsBody(events, eventCount));
}
