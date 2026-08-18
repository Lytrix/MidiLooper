//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// USB occupy ledger catch-up. Not a playback engine: mutate ActiveNoteLedger only.

#pragma once

#include "ActiveNoteLedger.h"
#include "Loop.h"
#include "MidiEvent.h"
#include "Utils/IntervalProjection.h"
#include "Utils/TrackMem.h"

#include <cstdint>

namespace CommittedPlaybackLedgerCatchUp {

/// USB occupy may catch up the committed ledger only when the interval does not
/// wrap. Clock owns lastTickInLoop, nextEventIndex, send, wrap, and capture.
inline TRACK_COLD_MEM bool shouldApply(const Loop& loop, uint32_t occupyPhase) {
  if (loop.lastTickInLoop == UINT32_MAX) {
    return false;
  }
  if (occupyPhase <= loop.lastTickInLoop) {
    return false;
  }
  if (loop.shouldCommitOverdubWrap(loop.lastTickInLoop, occupyPhase)) {
    return false;
  }
  return true;
}

/// Apply committed events in (lastTickInLoop, occupyPhase]. Does not send MIDI,
/// rebuild merged events, or mutate cursor / nextEventIndex / lastTickInLoop.
template <typename EventVec>
inline TRACK_COLD_MEM void applyOpenClosedInterval(ActiveNoteLedger& ledger, uint8_t channel,
                                                    const EventVec& events, uint32_t lastTickInLoop,
                                                    uint32_t occupyPhase, uint32_t loopLength) {
  if (lastTickInLoop == UINT32_MAX || occupyPhase <= lastTickInLoop || loopLength == 0) {
    return;
  }
  for (const MidiEvent& evt : events) {
    const uint32_t evPhase = IntervalProjection::playbackEventPhase(evt.tick, loopLength);
    if (!IntervalProjection::didPlaybackEventCross(false, lastTickInLoop, evPhase, occupyPhase)) {
      continue;
    }
    (void)ledger.applyPlaybackEvent(channel, evt);
  }
}

}  // namespace CommittedPlaybackLedgerCatchUp
