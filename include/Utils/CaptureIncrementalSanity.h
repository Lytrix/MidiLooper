//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "Globals.h"
#include "LoopEventStore.h"
#include "MidiEvent.h"

namespace CaptureIncrementalSanity {

constexpr size_t kWrapSliceMaxEvents = 64;
constexpr size_t kBudgetSliceMaxEvents = 32;
/// Hot-stop flatten/min-length paths use internal heap; defer when store is larger.
constexpr size_t kMaxHotStopFlattenEvents = 512;

struct PairRepairResult {
  size_t eventsRemoved = 0;
};

struct SliceResult {
  size_t eventsRemoved = 0;
};

/// On NoteOff append: orphan off removal + optional min-length pair drop.
PairRepairResult repairCompletedPair(LoopEventStore& store, const MidiEvent& closedOff,
                                   uint32_t loopLengthTicks, uint32_t minLengthTicks,
                                   bool minLengthRemoveEnabled);

/// Orphan repair within wrap-window tick range on live capture store.
SliceResult repairWrapWindowSlice(LoopEventStore& store, uint32_t loopLengthTicks,
                                  uint32_t wrapWindowTicks = Config::TICKS_PER_BAR);

/// Budgeted orphan repair slice from cursor (main loop).
SliceResult processBudgetSlice(LoopEventStore& store, uint32_t loopLengthTicks, size_t& cursor,
                               size_t maxEvents = kBudgetSliceMaxEvents);

/// Hot stop: drop completed pairs with linear span < minLengthTicks.
size_t removePairsShorterThanNoteMinLength(LoopEventStore& store, uint32_t loopLengthTicks,
                                           uint32_t minLengthTicks, bool enabled);

/// Hot stop: canonical invariant check — log warning on failure; no repair.
bool verifyCaptureHotStop(const LoopEventStore& store, uint32_t loopLengthTicks);

}  // namespace CaptureIncrementalSanity
