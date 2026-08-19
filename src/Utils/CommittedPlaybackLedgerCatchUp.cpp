//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// USB occupy ledger catch-up. Not a playback engine: mutate ActiveNoteLedger only.
// FLASHMEM: occupy must not consume ITCM (same RAM1 rule as wrap-tick catch-up).

#include "Utils/CommittedPlaybackLedgerCatchUp.h"

#include "Utils/IntervalProjection.h"
#include "Utils/TrackMem.h"

namespace CommittedPlaybackLedgerCatchUp {
namespace {

TRACK_COLD_MEM bool eventCrossesAtPhase(const MidiEvent& evt, uint32_t lastTickInLoop,
                                        uint32_t occupyPhase, uint32_t loopLength, uint32_t phase) {
  const uint32_t evPhase = IntervalProjection::playbackEventPhase(evt.tick, loopLength);
  if (evPhase != phase) {
    return false;
  }
  return IntervalProjection::didPlaybackEventCross(false, lastTickInLoop, evPhase, occupyPhase);
}

TRACK_COLD_MEM void applyEventsAtPhase(ActiveNoteLedger& ledger, uint8_t channel,
                                       const MidiEvent* events, size_t eventCount,
                                       uint32_t lastTickInLoop, uint32_t occupyPhase,
                                       uint32_t loopLength, uint32_t phase) {
  for (size_t i = 0; i < eventCount; ++i) {
    const MidiEvent& evt = events[i];
    if (!evt.isNoteOff()) {
      continue;
    }
    if (!eventCrossesAtPhase(evt, lastTickInLoop, occupyPhase, loopLength, phase)) {
      continue;
    }
    (void)ledger.applyPlaybackEvent(channel, evt);
  }
  for (size_t i = 0; i < eventCount; ++i) {
    const MidiEvent& evt = events[i];
    if (evt.isNoteOff()) {
      continue;
    }
    if (!eventCrossesAtPhase(evt, lastTickInLoop, occupyPhase, loopLength, phase)) {
      continue;
    }
    (void)ledger.applyPlaybackEvent(channel, evt);
  }
}

}  // namespace

TRACK_COLD_MEM __attribute__((noinline)) void applyOpenClosedIntervalEvents(
    ActiveNoteLedger& ledger, uint8_t channel, const MidiEvent* events, size_t eventCount,
    uint32_t lastTickInLoop, uint32_t occupyPhase, uint32_t loopLength) {
  if (events == nullptr || eventCount == 0) {
    return;
  }
  if (lastTickInLoop == UINT32_MAX || occupyPhase <= lastTickInLoop || loopLength == 0) {
    return;
  }
  uint32_t appliedPhase = lastTickInLoop;
  for (;;) {
    uint32_t nextPhase = UINT32_MAX;
    for (size_t i = 0; i < eventCount; ++i) {
      const uint32_t evPhase = IntervalProjection::playbackEventPhase(events[i].tick, loopLength);
      if (evPhase <= appliedPhase) {
        continue;
      }
      if (!IntervalProjection::didPlaybackEventCross(false, lastTickInLoop, evPhase, occupyPhase)) {
        continue;
      }
      if (evPhase < nextPhase) {
        nextPhase = evPhase;
      }
    }
    if (nextPhase == UINT32_MAX) {
      return;
    }
    applyEventsAtPhase(ledger, channel, events, eventCount, lastTickInLoop, occupyPhase, loopLength,
                       nextPhase);
    appliedPhase = nextPhase;
  }
}

}  // namespace CommittedPlaybackLedgerCatchUp
