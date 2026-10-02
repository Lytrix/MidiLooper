//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file WindowPagedStream.h
 * @brief DEC-108 §14 / task 3.12b: the **bounded legacy compatibility path** — content that exceeds the admission
 * limit L (or any interval that outgrows one page) served through fixed-capacity immutable event pages, consumed by a
 * resumable cursor.
 *
 * Why pages and not a bigger window: L is the window's storage capacity (see `kWindowAdmissionLimit`), and the point of
 * the paged path is that **memory stays bounded while content does not have to be** — a drained page returns to the ring
 * and is refilled with the next slice. As task 5.5 puts it, segmentation proves a **storage bound, not throughput**.
 *
 * Exactly-once and ordering come from positions alone:
 *   - the **producer** position is `CommittedEventRange::WindowTraversalCursor`, the existing resumable primitive, reused
 *     rather than reinvented (task 3.8's audit);
 *   - the **consumer** position is `(sourceRevision, nextSequence, offsetInPage)`, so re-entering continues instead of
 *     repeating, and a cursor from another revision cannot consume this stream at all;
 *   - a page is never written after it is published, and a published page is reused only once drained.
 *
 * There is deliberately **no blocking state**: a consumer that finds its next sequence unpublished tells the caller to
 * produce (`NeedsPage`); it never waits, so the clock path can never be delayed by an event that has not been prepared.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "CommittedEventRange.h"
#include "LoopEventStore.h"
#include "MidiEvent.h"

/// One fixed-capacity page. Its buffer is preallocated at `reset` and never grown again: the budgeted primitive appends
/// into it and its `maxEvents` is what keeps the slice inside the page.
constexpr size_t kWindowPageCapacity = 128;
/// The ring's bound: three pages in flight is what "a bounded, preallocated transit buffer" means here. A producer that
/// finds every page published and undrained must let the consumer catch up (`RingFull`) rather than allocate.
constexpr size_t kWindowPageCount = 3;

struct WindowEventPage {
  SessionMidiEventVec events;
  uint32_t sourceRevision = 0;
  uint32_t sequence = 0;
  bool published = false;
};

/// The consumer's position — and the only thing that can admit a page into a stream. Carrying the revision is what makes
/// it **generation-qualified**: a cursor that outlived its revision is refused by name rather than resuming another
/// operation's events (the same rule as the shared-view identity, DEC-108 §13d).
struct WindowPageCursor {
  uint32_t sourceRevision = 0;
  uint32_t nextSequence = 0;
  size_t offsetInPage = 0;
  bool exhausted = false;
};

class WindowPagedStream {
 public:
  enum class ProduceOutcome : uint8_t {
    Produced,
    /// The source has no further events for this window: the stream is complete, and nothing was truncated.
    SourceComplete,
    /// Every page is published and undrained: let the consumer catch up first.
    RingFull,
    /// The caller's revision is not this stream's.
    StaleRevision,
  };
  enum class ConsumeOutcome : uint8_t {
    Emitted,
    /// The next sequence is not published yet: the caller produces. Never a wait.
    NeedsPage,
    StaleRevision,
    /// Every page drained and the source complete.
    Exhausted,
  };

  /// Establish a stream for one source revision and preallocate the ring. Off-clock.
  void reset(uint32_t sourceRevision) {
    sourceRevision_ = sourceRevision;
    nextProduceSequence_ = 0;
    sourceComplete_ = false;
    for (WindowEventPage& page : pages_) {
      page.events.clear();
      page.events.reserve(kWindowPageCapacity);
      page.sourceRevision = 0;
      page.sequence = 0;
      page.published = false;
    }
    CommittedEventRange::resetWindowTraversalCursor(traversal_);
  }

  uint32_t sourceRevision() const { return sourceRevision_; }
  bool sourceComplete() const { return sourceComplete_; }

  size_t pagesPublished() const {
    size_t count = 0;
    for (const WindowEventPage& page : pages_) {
      if (page.published) {
        ++count;
      }
    }
    return count;
  }

  /// One bounded production step: fill the free page with the next slice of the window and publish it with its
  /// sequence and revision. The slice comes from the existing budgeted primitive, so the traversal resumes across calls
  /// and the work per call is bounded by the page capacity — no inline rebuild, no whole-interval scan.
  ProduceOutcome produce(const CommittedChunkIdList* const* lists, size_t listCount, uint32_t loopLengthTicks,
                         uint32_t windowStart, uint32_t windowLength, uint32_t expectedRevision) {
    if (expectedRevision != sourceRevision_) {
      return ProduceOutcome::StaleRevision;
    }
    if (sourceComplete_) {
      return ProduceOutcome::SourceComplete;
    }
    WindowEventPage* freePage = nullptr;
    for (WindowEventPage& page : pages_) {
      if (!page.published) {
        freePage = &page;
        break;
      }
    }
    if (freePage == nullptr) {
      return ProduceOutcome::RingFull;
    }
    freePage->events.clear();
    const CommittedEventRange::WindowTraversalResult result = CommittedEventRange::appendWindowBudgeted(
        lists, listCount, loopLengthTicks, windowStart, windowLength, kWindowPageCapacity, traversal_,
        freePage->events);
    freePage->sourceRevision = sourceRevision_;
    freePage->sequence = nextProduceSequence_++;
    freePage->published = true;
    if (result.complete) {
      sourceComplete_ = true;
    }
    return ProduceOutcome::Produced;
  }

  /// One bounded consumption step: emit from the page the cursor names, continuing exactly where it left off. A drained
  /// page returns to the ring only after its last event has been emitted, and the cursor moves on, so no event can be
  /// emitted twice or skipped.
  ConsumeOutcome consume(WindowPageCursor& cursor, MidiEvent* out, size_t capacity, size_t& emittedOut) {
    emittedOut = 0;
    if (cursor.sourceRevision != 0 && cursor.sourceRevision != sourceRevision_) {
      return ConsumeOutcome::StaleRevision;
    }
    cursor.sourceRevision = sourceRevision_;
    if (cursor.exhausted) {
      return ConsumeOutcome::Exhausted;
    }
    WindowEventPage* page = pageForSequence(cursor.nextSequence);
    if (page == nullptr) {
      if (sourceComplete_ && pagesPublished() == 0) {
        cursor.exhausted = true;
        return ConsumeOutcome::Exhausted;
      }
      return ConsumeOutcome::NeedsPage;
    }
    while (cursor.offsetInPage < page->events.size() && emittedOut < capacity) {
      out[emittedOut++] = page->events[cursor.offsetInPage++];
    }
    if (cursor.offsetInPage >= page->events.size()) {
      page->published = false;
      page->events.clear();
      cursor.offsetInPage = 0;
      ++cursor.nextSequence;
      if (sourceComplete_ && pagesPublished() == 0) {
        cursor.exhausted = true;
      }
    }
    return ConsumeOutcome::Emitted;
  }

 private:
  WindowEventPage* pageForSequence(uint32_t sequence) {
    for (WindowEventPage& page : pages_) {
      if (page.published && page.sequence == sequence) {
        return &page;
      }
    }
    return nullptr;
  }

  WindowEventPage pages_[kWindowPageCount];
  uint32_t sourceRevision_ = 0;
  uint32_t nextProduceSequence_ = 0;
  CommittedEventRange::WindowTraversalCursor traversal_{};
  bool sourceComplete_ = false;
};
