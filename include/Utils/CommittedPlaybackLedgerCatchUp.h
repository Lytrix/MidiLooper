//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// USB occupy ledger catch-up. Not a playback engine: mutate ActiveNoteLedger only.

#pragma once

#include "ActiveNoteLedger.h"
#include "Loop.h"
#include "MidiEvent.h"
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

/// Apply committed events in (lastTickInLoop, occupyPhase]. Phase order, and at
/// each phase Off then On (same rule as NoteUtils::sortMidiEventsChronologically).
/// Per-phase walks; no scratch. A global Off-then-On two-pass cannot close a
/// NoteOn that starts in the same interval (open-NoteOn stack, DEC-042).
/// Does not send MIDI, rebuild merged events, or mutate cursor / nextEventIndex /
/// lastTickInLoop.
/// Firmware: defined in CommittedPlaybackLedgerCatchUp.cpp (FLASHMEM).
void applyOpenClosedIntervalEvents(ActiveNoteLedger& ledger, uint8_t channel,
                                   const MidiEvent* events, size_t eventCount,
                                   uint32_t lastTickInLoop, uint32_t occupyPhase,
                                   uint32_t loopLength);

template <typename EventVec>
inline void applyOpenClosedInterval(ActiveNoteLedger& ledger, uint8_t channel, const EventVec& events,
                                    uint32_t lastTickInLoop, uint32_t occupyPhase,
                                    uint32_t loopLength) {
  applyOpenClosedIntervalEvents(ledger, channel, events.data(), events.size(), lastTickInLoop,
                                occupyPhase, loopLength);
}

}  // namespace CommittedPlaybackLedgerCatchUp
