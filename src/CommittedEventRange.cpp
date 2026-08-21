//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "CommittedEventRange.h"

#include "Utils/DisplayWindowUtils.h"

#include <limits>

#if defined(SESSION_CAPTURE) && defined(ARDUINO)
#include "Utils/DebugSessionCapture.h"
#include <Arduino.h>
#endif

#if defined(__IMXRT1062__)
#define PUBLISHED_RANGE_MEM FLASHMEM
#else
#define PUBLISHED_RANGE_MEM
#endif

// Keep diagnostic format strings out of DTCM .rodata — see RT_TAG in RuntimeTimingTelemetry.
#if defined(SESSION_CAPTURE) && defined(__IMXRT1062__)
#define CER_DIAG_FMT(literal) PSTR(literal)
#else
#define CER_DIAG_FMT(literal) literal
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
PUBLISHED_RANGE_MEM void appendCommittedRangeFull(const CommittedChunkIdList* const* lists,
                                                  size_t listCount, uint32_t loopLengthTicks,
                                                  MidiEventVector& out) {
  out.clear();
  if (lists == nullptr || listCount == 0 || loopLengthTicks == 0) {
    return;
  }
  for (size_t li = 0; li < listCount; ++li) {
    const CommittedChunkIdList* list = lists[li];
    if (list == nullptr || list->empty()) {
      continue;
    }
    LoopEventStore::appendChunkRefEvents(*list, out);
  }
}

}  // namespace

PUBLISHED_RANGE_MEM void CommittedEventRange::appendTo(SessionMidiEventVec& out) const {
  if (!windowed_) {
    appendCommittedRangeFull(lists_, listCount_, loopLengthTicks_, out);
    return;
  }
  out.clear();
  WindowTraversalCursor cursor{};
  resetWindowTraversalCursor(cursor);
  appendWindowBudgeted(lists_, listCount_, loopLengthTicks_, windowStart_, windowLength_,
                       std::numeric_limits<size_t>::max(), cursor, out);
}

PUBLISHED_RANGE_MEM void CommittedEventRange::appendTo(MidiEventVec& out) const {
  if (!windowed_) {
    appendCommittedRangeFull(lists_, listCount_, loopLengthTicks_, out);
    return;
  }
  SessionMidiEventVec ext;
  appendTo(ext);
  out.assign(ext.begin(), ext.end());
}

PUBLISHED_RANGE_MEM void CommittedEventRange::resetWindowTraversalCursor(
    WindowTraversalCursor& cursor) {
  cursor.listIndex = 0;
  cursor.chunkIndex = 0;
  cursor.chunkEventOffset = 0;
  cursor.complete = false;
}

PUBLISHED_RANGE_MEM __attribute__((noinline)) CommittedEventRange::WindowTraversalResult
CommittedEventRange::appendWindowBudgeted(const CommittedChunkIdList* const* lists,
                                          size_t listCount, uint32_t loopLengthTicks,
                                          uint32_t windowStart, uint32_t windowLength,
                                          size_t maxEvents, WindowTraversalCursor& cursor,
                                          SessionMidiEventVec& out) {
  WindowTraversalResult result{};
  result.resumed = cursor.listIndex != 0 || cursor.chunkIndex != 0 || cursor.chunkEventOffset != 0;
  if (cursor.complete) {
    result.complete = true;
    return result;
  }
  if (lists == nullptr || listCount == 0 || loopLengthTicks == 0 || windowLength == 0) {
    cursor.complete = true;
    cursor.listIndex = listCount;
    result.complete = true;
    return result;
  }
  if (maxEvents == 0) {
    return result;
  }

  SessionMidiEventVec chunkEvents;
  SessionMidiEventVec windowEvents;

#if defined(SESSION_CAPTURE) && defined(ARDUINO)
  // Decompose the windowed traversal: DIAG,disp_win attributed 26.4s of a 26.7s committed
  // resolve to this call (session_20260821_102119) and the per-chunk work is all linear,
  // so the counts are what identify the cost.
  constexpr uint32_t kTraversalReportUs = 10000u;
  const uint32_t traversalStartUs = micros();
  uint32_t spanUs = 0;
  uint32_t copyUs = 0;
  uint32_t filterUs = 0;
  uint32_t chunksVisited = 0;
  uint32_t chunksNoSpan = 0;
  uint32_t chunksOutsideWindow = 0;
  uint32_t chunksCopied = 0;
  uint32_t eventsCopied = 0;
  const auto emitTraversalDiag = [&](size_t appendedNow) {
    const uint32_t traversalUs = micros() - traversalStartUs;
    if (traversalUs < kTraversalReportUs) {
      return;
    }
    char line[256];
    snprintf(line, sizeof(line),
             CER_DIAG_FMT("#CAP,%lu,DIAG,cer_win,us=%lu,span=%lu,copy=%lu,filter=%lu,lists=%u,"
                          "chunks=%u,nospan=%u,outside=%u,copied=%u,evcopy=%u,evout=%u"),
             static_cast<unsigned long>(micros()), static_cast<unsigned long>(traversalUs),
             static_cast<unsigned long>(spanUs), static_cast<unsigned long>(copyUs),
             static_cast<unsigned long>(filterUs), static_cast<unsigned>(listCount),
             static_cast<unsigned>(chunksVisited), static_cast<unsigned>(chunksNoSpan),
             static_cast<unsigned>(chunksOutsideWindow), static_cast<unsigned>(chunksCopied),
             static_cast<unsigned>(eventsCopied), static_cast<unsigned>(appendedNow));
    DebugSessionCapture::appendCaptureTextLine(line);
  };
#define CER_EMIT_TRAVERSAL(appendedNow) emitTraversalDiag(appendedNow)
#else
#define CER_EMIT_TRAVERSAL(appendedNow) \
  do {                                  \
  } while (0)
#endif

  size_t appended = 0;
  size_t li = cursor.listIndex;
  while (li < listCount) {
    const CommittedChunkIdList* list = lists[li];
    if (list == nullptr || list->empty()) {
      ++li;
      cursor.listIndex = li;
      cursor.chunkIndex = 0;
      cursor.chunkEventOffset = 0;
      continue;
    }

    size_t ci = (li == cursor.listIndex) ? cursor.chunkIndex : 0;
    for (; ci < list->size(); ++ci) {
      const uint16_t chunkId = (*list)[ci];
      uint32_t firstTick = 0;
      uint32_t lastTick = 0;
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
      ++chunksVisited;
      uint32_t stepStartUs = micros();
#endif
      if (!LoopEventStore::chunkTickSpan(chunkId, firstTick, lastTick)) {
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
        spanUs += micros() - stepStartUs;
        ++chunksNoSpan;
#endif
        cursor.chunkEventOffset = 0;
        continue;
      }
      if (!CommittedEventRange::chunkIntersectsWindow(firstTick, lastTick, windowStart, windowLength,
                                                      loopLengthTicks)) {
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
        spanUs += micros() - stepStartUs;
        ++chunksOutsideWindow;
#endif
        cursor.chunkEventOffset = 0;
        continue;
      }
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
      spanUs += micros() - stepStartUs;
      ++chunksCopied;
      stepStartUs = micros();
#endif

      chunkEvents.clear();
      windowEvents.clear();
      LoopEventStore::appendChunkRefEvent(chunkId, chunkEvents);
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
      copyUs += micros() - stepStartUs;
      eventsCopied += static_cast<uint32_t>(chunkEvents.size());
      stepStartUs = micros();
#endif
      DisplayWindowUtils::filterMidiEventsToWindow(chunkEvents, windowEvents, windowStart,
                                                   windowLength, loopLengthTicks);
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
      filterUs += micros() - stepStartUs;
#endif
      if (windowEvents.empty()) {
        cursor.chunkEventOffset = 0;
        continue;
      }

      const size_t startOffset =
          (li == cursor.listIndex && ci == cursor.chunkIndex) ? cursor.chunkEventOffset : 0;
      if (startOffset >= windowEvents.size()) {
        cursor.chunkEventOffset = 0;
        continue;
      }

      const size_t available = windowEvents.size() - startOffset;
      const size_t budget = maxEvents - appended;
      const size_t take = available < budget ? available : budget;
      out.insert(out.end(),
                 windowEvents.begin() +
                     static_cast<SessionMidiEventVec::difference_type>(startOffset),
                 windowEvents.begin() +
                     static_cast<SessionMidiEventVec::difference_type>(startOffset + take));
      appended += take;

      if (take < available) {
        cursor.listIndex = li;
        cursor.chunkIndex = ci;
        cursor.chunkEventOffset = startOffset + take;
        result.eventsAppended = appended;
        result.complete = false;
        CER_EMIT_TRAVERSAL(appended);
        return result;
      }

      cursor.chunkEventOffset = 0;
      if (appended >= maxEvents) {
        cursor.listIndex = li;
        cursor.chunkIndex = ci + 1;
        cursor.chunkEventOffset = 0;
        if (cursor.chunkIndex >= list->size()) {
          cursor.listIndex = li + 1;
          cursor.chunkIndex = 0;
        }
        cursor.complete = cursor.listIndex >= listCount;
        result.eventsAppended = appended;
        result.complete = cursor.complete;
        CER_EMIT_TRAVERSAL(appended);
        return result;
      }
    }

    ++li;
    cursor.listIndex = li;
    cursor.chunkIndex = 0;
    cursor.chunkEventOffset = 0;
  }

  cursor.complete = true;
  cursor.listIndex = listCount;
  cursor.chunkIndex = 0;
  cursor.chunkEventOffset = 0;
  result.eventsAppended = appended;
  result.complete = true;
  CER_EMIT_TRAVERSAL(appended);
  return result;
}

#undef CER_EMIT_TRAVERSAL
