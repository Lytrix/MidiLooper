//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "CommittedEventRange.h"

#include "Utils/DisplayWindowUtils.h"

#if defined(__IMXRT1062__)
#define PUBLISHED_RANGE_MEM FLASHMEM
#else
#define PUBLISHED_RANGE_MEM
#endif

PUBLISHED_RANGE_MEM CommittedEventRange CommittedEventRange::full(
    const CommittedChunkIdList* const* lists, size_t listCount, uint32_t loopLengthTicks) {
  return CommittedEventRange(lists, listCount, loopLengthTicks, false, 0, 0);
}

PUBLISHED_RANGE_MEM CommittedEventRange CommittedEventRange::inWindow(
    const CommittedChunkIdList* const* lists, size_t listCount, uint32_t loopLengthTicks,
    uint32_t windowStart, uint32_t windowLength) {
  return CommittedEventRange(lists, listCount, loopLengthTicks, true, windowStart, windowLength);
}

CommittedEventRange::CommittedEventRange(const CommittedChunkIdList* const* lists, size_t listCount,
                                         uint32_t loopLengthTicks, bool windowed,
                                         uint32_t windowStart, uint32_t windowLength)
    : lists_(lists),
      listCount_(listCount),
      loopLengthTicks_(loopLengthTicks),
      windowed_(windowed),
      windowStart_(windowStart),
      windowLength_(windowLength) {}

PUBLISHED_RANGE_MEM bool CommittedEventRange::chunkIntersectsWindow(uint32_t firstTick,
                                                                    uint32_t lastTick,
                                                                    uint32_t windowStart,
                                                                    uint32_t windowLength,
                                                                    uint32_t loopLengthTicks) {
  if (loopLengthTicks == 0 || windowLength == 0) {
    return false;
  }
  if (windowLength >= loopLengthTicks) {
    return true;
  }
  return DisplayWindowUtils::noteIntersectsWindow(firstTick, lastTick, windowStart, windowLength,
                                                  loopLengthTicks);
}

namespace {

template <typename MidiEventVector>
void appendCommittedRange(const CommittedChunkIdList* const* lists, size_t listCount,
                          uint32_t loopLengthTicks, bool windowed, uint32_t windowStart,
                          uint32_t windowLength, MidiEventVector& out) {
  out.clear();
  if (lists == nullptr || listCount == 0 || loopLengthTicks == 0) {
    return;
  }
  if (windowed && windowLength == 0) {
    return;
  }

  if (!windowed) {
    for (size_t li = 0; li < listCount; ++li) {
      const CommittedChunkIdList* list = lists[li];
      if (list == nullptr || list->empty()) {
        continue;
      }
      LoopEventStore::appendChunkRefEvents(*list, out);
    }
    return;
  }

  MidiEventVector chunkEvents;
  MidiEventVector windowEvents;
  for (size_t li = 0; li < listCount; ++li) {
    const CommittedChunkIdList* list = lists[li];
    if (list == nullptr || list->empty()) {
      continue;
    }
    for (uint16_t chunkId : *list) {
      uint32_t firstTick = 0;
      uint32_t lastTick = 0;
      if (!LoopEventStore::chunkTickSpan(chunkId, firstTick, lastTick)) {
        continue;
      }
      if (!CommittedEventRange::chunkIntersectsWindow(firstTick, lastTick, windowStart, windowLength,
                                                      loopLengthTicks)) {
        continue;
      }
      chunkEvents.clear();
      LoopEventStore::appendChunkRefEvent(chunkId, chunkEvents);
      DisplayWindowUtils::filterMidiEventsToWindow(chunkEvents, windowEvents, windowStart,
                                                   windowLength, loopLengthTicks);
      if (windowEvents.empty()) {
        continue;
      }
      out.insert(out.end(), windowEvents.begin(), windowEvents.end());
    }
  }
}

}  // namespace

PUBLISHED_RANGE_MEM void CommittedEventRange::appendTo(SessionMidiEventVec& out) const {
  appendCommittedRange(lists_, listCount_, loopLengthTicks_, windowed_, windowStart_, windowLength_,
                       out);
}

PUBLISHED_RANGE_MEM void CommittedEventRange::appendTo(MidiEventVec& out) const {
  appendCommittedRange(lists_, listCount_, loopLengthTicks_, windowed_, windowStart_, windowLength_,
                       out);
}
