//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/CaptureIncrementalSanity.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "Logger.h"
#include "Utils/LoopEventValidation.h"
#include "Utils/LoopStopFinalize.h"
#include "Utils/NoteUtils.h"
#include "Utils/LoopMem.h"

namespace CaptureIncrementalSanity {

LOOP_COLD_MEM bool MinimumLengthCursor::current() const {
  return workspace_ && generation_ && *generation_ == expectedGeneration_;
}

LOOP_COLD_MEM bool MinimumLengthCursor::begin(MinimumLengthWorkspace& workspace,
    const uint64_t& generation, uint32_t length, uint32_t minimum, bool enabled) {
  if (phase_ != Phase::Idle) return false;
  workspace_ = &workspace; generation_ = &generation; expectedGeneration_ = generation;
  length_ = length; minimum_ = minimum; enabled_ = enabled && minimum != 0;
  position_ = lastStepUnits_ = 0; lastTick_ = 0; lastOrder_ = 0;
  phase_ = Phase::Reset;
  return true;
}

LOOP_COLD_MEM MinimumLengthStep MinimumLengthCursor::stepReset(size_t quantum) {
  lastStepUnits_ = 0;
  if (!current() || !quantum) return MinimumLengthStep::Denied;
  if (phase_ == Phase::Input) return MinimumLengthStep::InputReady;
  if (phase_ != Phase::Reset) return MinimumLengthStep::Denied;
  while (lastStepUnits_ < quantum && position_ < MinimumLengthWorkspace::kLanes) {
    workspace_->lanes[position_++] = {0, SIZE_MAX};
    ++lastStepUnits_;
  }
  if (position_ != MinimumLengthWorkspace::kLanes) return MinimumLengthStep::Pending;
  position_ = 0; phase_ = Phase::Input;
  return MinimumLengthStep::InputReady;
}

LOOP_COLD_MEM MinimumLengthStep MinimumLengthCursor::consume(const MidiEvent& event) {
  lastStepUnits_ = 0;
  if (!current() || phase_ != Phase::Input || position_ == SIZE_MAX)
    return MinimumLengthStep::Denied;
  const bool on = event.isNoteOn(), off = event.isNoteOff();
  const uint8_t order = off ? 0 : (on ? 1 : 2);
  if (position_ && (event.tick < lastTick_ ||
      (event.tick == lastTick_ && order < lastOrder_))) return MinimumLengthStep::Denied;
  if ((on || off) && (!event.channel || event.channel > MinimumLengthWorkspace::kChannels ||
      event.data.noteData.note >= MinimumLengthWorkspace::kNotes)) return MinimumLengthStep::Denied;
  if (enabled_ && (on || off)) {
    auto& lane = workspace_->lanes[(event.channel - 1) * MinimumLengthWorkspace::kNotes +
                                   event.data.noteData.note];
    if (on) lane = {event.tick, position_};
    else if (lane.ordinal != SIZE_MAX) {
      if (event.tick >= lane.tick && event.tick - lane.tick < minimum_ &&
          !NoteUtils::isWrappedLoopNotePair(lane.tick, event.tick, length_)) {
        onOrdinal_ = lane.ordinal; offOrdinal_ = position_; phase_ = Phase::Pair;
      }
      lane.ordinal = SIZE_MAX;
    }
  }
  lastTick_ = event.tick; lastOrder_ = order; ++position_; lastStepUnits_ = 1;
  return phase_ == Phase::Pair ? MinimumLengthStep::PairReady : MinimumLengthStep::Accepted;
}

LOOP_COLD_MEM bool MinimumLengthCursor::pendingPair(size_t& on, size_t& off) const {
  if (!current() || phase_ != Phase::Pair) return false;
  on = onOrdinal_; off = offOrdinal_;
  return true;
}

LOOP_COLD_MEM bool MinimumLengthCursor::acknowledgePair() {
  if (!current() || phase_ != Phase::Pair) return false;
  phase_ = Phase::Input;
  return true;
}

LOOP_COLD_MEM bool MinimumLengthCursor::finishInput() {
  if (!current() || (phase_ != Phase::Input && phase_ != Phase::Complete)) return false;
  phase_ = Phase::Complete;
  return true;
}

LOOP_COLD_MEM void MinimumLengthCursor::discard() {
  workspace_ = nullptr; generation_ = nullptr; phase_ = Phase::Idle;
  position_ = lastStepUnits_ = 0;
}

namespace {

void reloadStoreFromFlat(LoopEventStore& store, const MidiEventVec& flat) {
  store.clear();
  if (!flat.empty()) {
    store.loadFromEvents(flat);
  }
}

bool noteEventLess(const MidiEvent& a, const MidiEvent& b) {
  return LoopStopFinalize::noteEventLess(a, b);
}

bool eventMatchesNoteOff(const MidiEvent& evt, const MidiEvent& closedOff) {
  return evt.isNoteOff() && evt.channel == closedOff.channel &&
         evt.data.noteData.note == closedOff.data.noteData.note &&
         evt.tick == closedOff.tick;
}

}  // namespace

PairRepairResult repairCompletedPair(LoopEventStore& store, const MidiEvent& closedOff,
                                   uint32_t loopLengthTicks, uint32_t minLengthTicks,
                                   bool minLengthRemoveEnabled) {
  PairRepairResult result;
  (void)minLengthTicks;
  (void)minLengthRemoveEnabled;
  if (store.empty() || !closedOff.isNoteOff()) {
    return result;
  }

  MidiEventVec flat;
  store.copyEventsTo(flat);
  if (flat.empty()) {
    return result;
  }

  std::sort(flat.begin(), flat.end(), noteEventLess);

  ssize_t offIdx = -1;
  for (ssize_t i = static_cast<ssize_t>(flat.size()) - 1; i >= 0; --i) {
    if (eventMatchesNoteOff(flat[static_cast<size_t>(i)], closedOff)) {
      offIdx = i;
      break;
    }
  }
  if (offIdx < 0) {
    return result;
  }

  std::vector<size_t> removeIndices;
  ssize_t onIdx = -1;
  for (ssize_t i = offIdx - 1; i >= 0; --i) {
    const MidiEvent& evt = flat[static_cast<size_t>(i)];
    if (evt.isNoteOn() && evt.data.noteData.velocity > 0 && evt.channel == closedOff.channel &&
        evt.data.noteData.note == closedOff.data.noteData.note) {
      onIdx = i;
      break;
    }
  }

  if (onIdx < 0) {
    removeIndices.push_back(static_cast<size_t>(offIdx));
  } else {
    // Min-length removal is hot-stop only (Q16) — not during live capture.
    for (ssize_t i = onIdx - 1; i >= 0; --i) {
      const MidiEvent& evt = flat[static_cast<size_t>(i)];
      if (evt.isNoteOn() && evt.data.noteData.velocity > 0 && evt.channel == closedOff.channel &&
          evt.data.noteData.note == closedOff.data.noteData.note) {
        removeIndices.push_back(static_cast<size_t>(i));
        break;
      }
    }
  }

  if (removeIndices.empty()) {
    return result;
  }

  std::sort(removeIndices.begin(), removeIndices.end());
  removeIndices.erase(std::unique(removeIndices.begin(), removeIndices.end()),
                      removeIndices.end());

  MidiEventVec kept;
  kept.reserve(flat.size() - removeIndices.size());
  size_t removeCursor = 0;
  for (size_t i = 0; i < flat.size(); ++i) {
    if (removeCursor < removeIndices.size() && i == removeIndices[removeCursor]) {
      ++removeCursor;
      ++result.eventsRemoved;
      continue;
    }
    kept.push_back(flat[i]);
  }

  reloadStoreFromFlat(store, kept);
  (void)loopLengthTicks;
  return result;
}

SliceResult repairWrapWindowSlice(LoopEventStore& store, uint32_t loopLengthTicks,
                                  uint32_t wrapWindowTicks) {
  SliceResult result;
  if (store.empty() || loopLengthTicks == 0) {
    return result;
  }

  const uint32_t window = std::min(wrapWindowTicks, loopLengthTicks);
  const uint32_t tailStart = loopLengthTicks > window ? loopLengthTicks - window : 0;
  const uint32_t headEnd = window;

  const auto inWindow = [&](uint32_t tick) {
    return tick < headEnd || (tick >= tailStart && tick < loopLengthTicks);
  };

  MidiEventVec flat;
  store.copyEventsTo(flat);
  if (flat.empty()) {
    return result;
  }

  MidiEventVec outside;
  MidiEventVec windowEvents;
  outside.reserve(flat.size());
  windowEvents.reserve(std::min(flat.size(), kWrapSliceMaxEvents));

  for (const MidiEvent& evt : flat) {
    if (inWindow(evt.tick) && windowEvents.size() < kWrapSliceMaxEvents) {
      windowEvents.push_back(evt);
    } else {
      outside.push_back(evt);
    }
  }

  if (windowEvents.empty()) {
    return result;
  }

  const LoopEventValidation::OrphanRepairResult repair =
      LoopEventValidation::repairOrphanNoteEvents(windowEvents, loopLengthTicks, wrapWindowTicks);
  if (repair.orphanedRemoved == 0) {
    return result;
  }

  MidiEventVec merged;
  merged.reserve(outside.size() + windowEvents.size());
  merged.insert(merged.end(), outside.begin(), outside.end());
  merged.insert(merged.end(), windowEvents.begin(), windowEvents.end());
  std::sort(merged.begin(), merged.end(), noteEventLess);
  reloadStoreFromFlat(store, merged);
  result.eventsRemoved = repair.orphanedRemoved;
  return result;
}

SliceResult processBudgetSlice(LoopEventStore& store, uint32_t loopLengthTicks, size_t& cursor,
                               size_t maxEvents) {
  SliceResult result;
  if (store.empty() || loopLengthTicks == 0 || maxEvents == 0) {
    cursor = 0;
    return result;
  }

  MidiEventVec flat;
  store.copyEventsTo(flat);
  if (flat.empty()) {
    cursor = 0;
    return result;
  }

  const size_t scanEnd = std::min(cursor + maxEvents, flat.size());
  MidiEventVec prefix(flat.begin(), flat.begin() + static_cast<ptrdiff_t>(scanEnd));

  const LoopEventValidation::OrphanRepairResult repair =
      LoopEventValidation::repairOrphanNoteEvents(prefix, loopLengthTicks, Config::TICKS_PER_BAR);
  if (repair.orphanedRemoved > 0) {
    MidiEventVec rebuilt;
    rebuilt.reserve(flat.size() - repair.orphanedRemoved);
    rebuilt.insert(rebuilt.end(), prefix.begin(), prefix.end());
    rebuilt.insert(rebuilt.end(), flat.begin() + static_cast<ptrdiff_t>(scanEnd), flat.end());
    std::sort(rebuilt.begin(), rebuilt.end(), noteEventLess);
    reloadStoreFromFlat(store, rebuilt);
    result.eventsRemoved = repair.orphanedRemoved;
  }

  cursor = scanEnd >= flat.size() ? 0 : scanEnd;
  return result;
}

size_t removePairsShorterThanNoteMinLength(LoopEventStore& store, uint32_t loopLengthTicks,
                                           uint32_t minLengthTicks, bool enabled) {
  if (!enabled || minLengthTicks == 0 || store.empty()) {
    return 0;
  }

  MidiEventVec flat;
  store.copyEventsTo(flat);
  if (flat.empty()) {
    return 0;
  }

  std::sort(flat.begin(), flat.end(), noteEventLess);

  std::vector<bool> keep(flat.size(), true);
  size_t removedPairs = 0;

  std::unordered_map<std::pair<uint8_t, uint8_t>, size_t, LoopStopFinalize::NoteKeyHash> activeOn;

  for (size_t i = 0; i < flat.size(); ++i) {
    const MidiEvent& evt = flat[i];
    if (!keep[i]) {
      continue;
    }
    if (evt.isNoteOn() && evt.data.noteData.velocity > 0) {
      activeOn[{evt.data.noteData.note, evt.channel}] = i;
    } else if (evt.isNoteOff()) {
      const auto key = std::make_pair(evt.data.noteData.note, evt.channel);
      auto it = activeOn.find(key);
      if (it == activeOn.end()) {
        continue;
      }
      const size_t onIdx = it->second;
      activeOn.erase(it);
      const uint32_t onTick = flat[onIdx].tick;
      if (evt.tick < onTick) {
        continue;
      }
      const uint32_t span = evt.tick - onTick;
      if (loopLengthTicks > 0 &&
          NoteUtils::isWrappedLoopNotePair(onTick, evt.tick, loopLengthTicks)) {
        continue;
      }
      if (span < minLengthTicks) {
        keep[onIdx] = false;
        keep[i] = false;
        ++removedPairs;
      }
    }
  }

  if (removedPairs == 0) {
    return 0;
  }

  MidiEventVec cleaned;
  cleaned.reserve(flat.size() - removedPairs * 2);
  for (size_t i = 0; i < flat.size(); ++i) {
    if (keep[i]) {
      cleaned.push_back(flat[i]);
    }
  }
  reloadStoreFromFlat(store, cleaned);

  logger.log(CAT_MIDI, LOG_INFO, "capture NoteMinLength removed %u pairs (min=%lu ticks)",
             static_cast<unsigned>(removedPairs),
             static_cast<unsigned long>(minLengthTicks));
  return removedPairs;
}

bool verifyCaptureHotStop(const LoopEventStore& store, uint32_t loopLengthTicks) {
  if (store.empty()) {
    return true;
  }
  MidiEventVec flat;
  store.copyEventsTo(flat);
  const LoopEventValidation::LoopEventValidationResult result =
      LoopEventValidation::validateLoopEvents(flat, loopLengthTicks,
                                                LoopEventValidation::kCanonicalInvariantMask);
  if (!result.passed) {
    logger.log(CAT_MIDI, LOG_WARNING,
               "capture hot stop verify: non-canonical storage (check=%u)",
               static_cast<unsigned>(result.firstFailure));
  }
  return result.passed;
}

}  // namespace CaptureIncrementalSanity
