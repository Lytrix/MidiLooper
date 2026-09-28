//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Globals.h"
#include "LoopEventStore.h"
#include "MidiEvent.h"

namespace CaptureIncrementalSanity {

/// Caller-owned, exclusively borrowed preparation scratch; never a per-slot field.
struct MinimumLengthWorkspace {
  struct Lane { uint32_t tick; size_t ordinal; };
  static constexpr size_t kChannels = 16;
  static constexpr size_t kNotes = 128;
  static constexpr size_t kLanes = kChannels * kNotes;
  std::array<Lane, kLanes> lanes;
};

enum class MinimumLengthStep : uint8_t { Pending, InputReady, Accepted, PairReady, Denied };

/// Decisions over an immutable tick/Off/On/other ordered stream. This does not
/// compact or publish output. Scratch and generation outlive discard. PairReady
/// consumes its Off once; read/acknowledge the decision before feeding more input.
class MinimumLengthCursor {
 public:
  MinimumLengthCursor() = default;
  MinimumLengthCursor(const MinimumLengthCursor&) = delete;
  MinimumLengthCursor& operator=(const MinimumLengthCursor&) = delete;
  bool begin(MinimumLengthWorkspace& workspace, const uint64_t& generation,
             uint32_t loopLengthTicks, uint32_t minLengthTicks, bool enabled);
  MinimumLengthStep stepReset(size_t quantum);
  MinimumLengthStep consume(const MidiEvent& event);
  bool pendingPair(size_t& onOrdinal, size_t& offOrdinal) const;
  bool acknowledgePair();
  bool finishInput();
  void discard();
  size_t lastStepUnits() const { return lastStepUnits_; }
 private:
  enum class Phase : uint8_t { Idle, Reset, Input, Pair, Complete };
  bool current() const;
  MinimumLengthWorkspace* workspace_ = nullptr;
  const uint64_t* generation_ = nullptr;
  uint64_t expectedGeneration_ = 0;
  uint32_t length_ = 0, minimum_ = 0, lastTick_ = 0;
  size_t position_ = 0, onOrdinal_ = 0, offOrdinal_ = 0, lastStepUnits_ = 0;
  uint8_t lastOrder_ = 0;
  bool enabled_ = false;
  Phase phase_ = Phase::Idle;
};

constexpr size_t kWrapSliceMaxEvents = 64;
constexpr size_t kBudgetSliceMaxEvents = 32;

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
