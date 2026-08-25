//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "Utils/LoopMem.h"

#include <algorithm>
#include <vector>

namespace {

struct OrderedSnapshotEvent {
  uint32_t canonicalOrder = 0;
  EventId eventId = kInvalidEventId;
  MidiEvent value{};
};

}  // namespace

LOOP_COLD_MEM bool Loop::tryGatherPublishedSnapshotEventsInWindow(
    SessionMidiEventVec& out, uint32_t windowStart, uint32_t windowLength) const {
  if (!publishedSnapshotIsCurrent()) {
    return false;
  }
  const ImmutableLoopSnapshotRef snapshot = publishedSnapshot();
  if (!snapshot) {
    return false;
  }

  const uint32_t loopLength = snapshot->loopLengthTicks();
  if (loopLength == 0 || windowLength == 0) {
    out.clear();
    return true;
  }
  if (windowLength >= loopLength) {
    windowStart = 0;
    windowLength = loopLength;
  }

  const uint32_t windowEnd = windowStart + windowLength;
  const uint32_t lastTick = windowEnd > 0 ? windowEnd - 1 : 0;
  const uint32_t firstBar = windowStart / Config::TICKS_PER_BAR;
  const uint32_t lastBar = lastTick / Config::TICKS_PER_BAR;

  std::vector<OrderedSnapshotEvent> collected;
  for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
    const auto membership = snapshot->eventMembership(bar);
    if (!membership) {
      continue;
    }
    for (const auto& reference : membership->references) {
      if (!reference) {
        continue;
      }
      const uint32_t tick = reference->value.tick;
      if (tick < windowStart || tick >= windowEnd) {
        continue;
      }
      collected.push_back(
          {reference->canonicalOrder, reference->eventId, reference->value});
    }
  }

  std::sort(collected.begin(), collected.end(),
            [](const OrderedSnapshotEvent& left, const OrderedSnapshotEvent& right) {
              if (left.canonicalOrder != right.canonicalOrder) {
                return left.canonicalOrder < right.canonicalOrder;
              }
              return left.eventId < right.eventId;
            });

  out.clear();
  out.reserve(collected.size());
  for (const OrderedSnapshotEvent& event : collected) {
    out.push_back(event.value);
  }
  return true;
}
