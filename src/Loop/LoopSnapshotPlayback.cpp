//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "CommittedEventRange.h"
#include "EditApply.h"
#include "LoopSnapshotBoxes.h"
#include "Utils/LoopMem.h"

#include <algorithm>
#include <vector>

namespace {

struct OrderedSnapshotEvent {
  uint32_t canonicalOrder = 0;
  EventId eventId = kInvalidEventId;
  MidiEvent value{};
};

void appendUniquePassId(std::vector<PassId>& ids, PassId id) {
  if (id == kInvalidPassId) {
    return;
  }
  for (const PassId existing : ids) {
    if (existing == id) {
      return;
    }
  }
  ids.push_back(id);
}

void appendUniqueEditPassId(EditPassIdList& ids, EditPassId id) {
  if (id == kInvalidEditPassId) {
    return;
  }
  for (const EditPassId existing : ids) {
    if (existing == id) {
      return;
    }
  }
  ids.push_back(id);
}

bool isRecordPassId(const LoopPasses& passes, PassId id) {
  return passes.hasRecordPass() && passes.recordPass.id == id;
}

void appendChunkListForPass(const LoopPasses& passes, PassId passId,
                            std::vector<const CommittedChunkIdList*>& lists) {
  if (passId == kInvalidPassId) {
    return;
  }
  if (passes.hasRecordPass() && passes.recordPass.id == passId &&
      passes.recordPass.state == CapturePassState::Active &&
      !passes.recordPass.committedChunkIds.empty()) {
    lists.push_back(&passes.recordPass.committedChunkIds);
    return;
  }
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.id == passId && pass.state == CapturePassState::Active &&
        !pass.committedChunkIds.empty()) {
      lists.push_back(&pass.committedChunkIds);
      return;
    }
  }
}

#if defined(SESSION_CAPTURE) && defined(ARDUINO)
constexpr uint32_t kGatherDestinationReportLimit = 8;

/// Diagnostic guard for the snapshot-window gather destinations.
///
/// The 2026-09-17 device fault was a DACCVIOL *inside* this fill: a block store
/// to address 0xE00 == 128 * sizeof(MidiEvent), i.e. the destination vector's
/// storage pointer had been lost while its element count lived on. Prove the
/// invariant before the fill so a corrupted destination reports itself instead
/// of faulting, and write the report straight to USB serial: the `#CAP` ring is
/// itself under suspicion and PLAYING/OVERDUBBING never flush it, so ring-routed
/// evidence would die with the reset.
bool sessionMidiEventVecStorageIntact(const SessionMidiEventVec& vec) {
  // A non-zero capacity means storage was allocated, so `data()` cannot be null.
  return vec.capacity() == 0 || vec.data() != nullptr;
}

LOOP_COLD_MEM __attribute__((noinline)) void reportGatherDestinationLostStorage(
    const void* owner, const SessionMidiEventVec& vec, const char* site) {
  static uint32_t reports = 0;
  if (reports >= kGatherDestinationReportLimit) {
    return;
  }
  ++reports;
  // Format strings stay in flash (`printf_P` + `PSTR`): DTCM `.rodata` is charged
  // against the RAM1 budget that pays for the stack, which is only a few KB here.
  Serial.printf_P(
      PSTR("#GATHER_DEST_CORRUPT,%lu,site=%s,loop=0x%08lx,dest=0x%08lx,data=0x%08lx,size=%lu,capacity=%lu\r\n"),
      static_cast<unsigned long>(micros()), site, reinterpret_cast<unsigned long>(owner),
      reinterpret_cast<unsigned long>(&vec), reinterpret_cast<unsigned long>(vec.data()),
      static_cast<unsigned long>(vec.size()), static_cast<unsigned long>(vec.capacity()));
}

/// False when the destination cannot be filled safely; the caller must then
/// report a gather miss instead of writing through lost storage. Out of line so
/// the report path never inflates the gather's own stack frame.
LOOP_COLD_MEM __attribute__((noinline)) bool gatherDestinationIsWritable(
    const void* owner, SessionMidiEventVec& out, const char* site) {
  if (sessionMidiEventVecStorageIntact(out)) {
    return true;
  }
  reportGatherDestinationLostStorage(owner, out, site);
  return false;
}
#endif

void gatherPublishedSnapshotMembership(const LoopSnapshot& snapshot, SessionMidiEventVec& out,
                                       uint32_t windowStart, uint32_t windowLength) {
  const uint32_t loopLength = snapshot.loopLengthTicks();
  if (loopLength == 0 || windowLength == 0) {
    out.clear();
    return;
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
    const auto membership = snapshot.eventMembership(bar);
    if (!membership) {
      continue;
    }
    forEachSnapshotMembershipReference(
        membership.get(),
        [&collected, windowStart, windowEnd](const LoopSnapshotEventRef& reference) {
          if (!reference) {
            return;
          }
          const uint32_t tick = reference->value.tick;
          if (tick < windowStart || tick >= windowEnd) {
            return;
          }
          collected.push_back(
              {reference->canonicalOrder, reference->eventId, reference->value});
        });
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
}

}  // namespace

LOOP_COLD_MEM bool Loop::tryGatherPublishedSnapshotEventsInWindow(
    SessionMidiEventVec& out, uint32_t windowStart, uint32_t windowLength,
    SnapshotWindowGatherPath* pathOut) const {
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
  if (!gatherDestinationIsWritable(this, out, "snapshot_window")) {
    if (pathOut != nullptr) {
      *pathOut = SnapshotWindowGatherPath::None;
    }
    return false;
  }
#endif
  const ImmutableLoopSnapshotRef snapshot = publishedSnapshot();
  if (!snapshot) {
    if (pathOut != nullptr) {
      *pathOut = SnapshotWindowGatherPath::None;
    }
    return false;
  }

  gatherPublishedSnapshotMembership(*snapshot, out, windowStart, windowLength);
  if (publishedSnapshotIsCurrent()) {
    if (pathOut != nullptr) {
      *pathOut = SnapshotWindowGatherPath::Snapshot;
    }
    return true;
  }

  std::vector<PassId> unpublishedPassIds;
  if (hasOverdubSession()) {
    for (size_t i = 0; i < overdubSessionCursor_ && i < overdubSessionPassIds_.size(); ++i) {
      const PassId passId = overdubSessionPassIds_[i];
      if (!isRecordPassId(passes, passId)) {
        appendUniquePassId(unpublishedPassIds, passId);
      }
    }
    if (!isRecordPassId(passes, lastCommittedPassId_)) {
      appendUniquePassId(unpublishedPassIds, lastCommittedPassId_);
    }
  }
  if (pendingSnapshotPublishBox_) {
    const PendingSnapshotPublishBox& box = *pendingSnapshotPublishBox_;
    for (const PassId passId : box.queuedPassIds) {
      if (!isRecordPassId(passes, passId)) {
        appendUniquePassId(unpublishedPassIds, passId);
      }
    }
    for (const PassId passId : box.appliedPassIds) {
      if (!isRecordPassId(passes, passId)) {
        appendUniquePassId(unpublishedPassIds, passId);
      }
    }
    if (!isRecordPassId(passes, box.constructionPassId)) {
      appendUniquePassId(unpublishedPassIds, box.constructionPassId);
    }
    // A wrapped pass is tracked as a wrap journal, not as a queued pass: the wrap
    // commit journals it (`queueOverdubWrapSnapshotJournal`) and the stop's session-exit
    // queue then skips it as already tracked (`passAlreadyTracked`). Enumerating it here
    // is the only place that keeps it audible while the root is stale -- the root itself
    // predates the wrap and the journal's own publish is deferred behind the session exit.
    // Without this the wrapped pass stayed omitted from the stale-root overlay after its
    // session closed (its `overdubSessionPassIds_` scan is gone), so the display resolved
    // it from `passes` while playback never emitted it (user report 2026-09-17: record 8
    // bars, overdub starting at bar 3; pass 1 inaudible, the wrapped tail audible).
    for (const PassId passId : box.journaledPassIds) {
      if (!isRecordPassId(passes, passId)) {
        appendUniquePassId(unpublishedPassIds, passId);
      }
    }
    for (const SnapshotWrapJournalFragment& journal : box.queuedWrapJournals) {
      if (!isRecordPassId(passes, journal.wrapPassId)) {
        appendUniquePassId(unpublishedPassIds, journal.wrapPassId);
      }
    }
  }

  // Overlay EVERY queued (not-yet-published) session exit, not just one: a
  // successor's committed passes must never be omitted from the stale-root view
  // while an earlier session is still draining through the idle publisher.
  for (size_t receiptIndex = 0; receiptIndex < sessionExitReceiptCount(); ++receiptIndex) {
    const SessionExitSnapshotHandoff* receipt = sessionExitReceiptAt(receiptIndex);
    if (receipt == nullptr) continue;
    for (const PassId id : receipt->passIds) {
      if (!isRecordPassId(passes, id)) appendUniquePassId(unpublishedPassIds, id);
    }
    if (!isRecordPassId(passes, receipt->lastPassId))
      appendUniquePassId(unpublishedPassIds, receipt->lastPassId);
  }

  std::vector<const CommittedChunkIdList*> unpublishedLists;
  for (const PassId passId : unpublishedPassIds) {
    appendChunkListForPass(passes, passId, unpublishedLists);
  }

  SessionMidiEventVec unpublishedEvents;
  if (!unpublishedLists.empty()) {
    CommittedEventRange::inWindow(unpublishedLists.data(), unpublishedLists.size(),
                                  loopLengthTicks, windowStart, windowLength)
        .appendTo(unpublishedEvents);
  }

  EditPassIdList unpublishedCompanionIds;
  if (hasOverdubSession()) {
    for (size_t i = 0; i < overdubSessionCursor_ && i < overdubSessionCompanionIds_.size();
         ++i) {
      for (const EditPassId companionId : overdubSessionCompanionIds_[i]) {
        appendUniqueEditPassId(unpublishedCompanionIds, companionId);
      }
    }
  }
  if (pendingSnapshotPublishBox_) {
    for (const EditPassId companionId : pendingSnapshotPublishBox_->companionIds) {
      appendUniqueEditPassId(unpublishedCompanionIds, companionId);
    }
  }

  for (size_t receiptIndex = 0; receiptIndex < sessionExitReceiptCount(); ++receiptIndex) {
    const SessionExitSnapshotHandoff* receipt = sessionExitReceiptAt(receiptIndex);
    if (receipt == nullptr) continue;
    const size_t rows = std::min(receipt->companionRowCount, receipt->companionIds.size());
    for (size_t i = 0; i < rows; ++i) {
      for (const EditPassId id : receipt->companionIds[i])
        appendUniqueEditPassId(unpublishedCompanionIds, id);
    }
  }

  EditPassVec unpublishedCompanions;
  for (const EditPassId companionId : unpublishedCompanionIds) {
    for (const EditPass& row : passes.editPasses) {
      if (row.id == companionId && row.state == EditPassState::Active &&
          row.passType == EditPassType::Note) {
        unpublishedCompanions.push_back(row);
        break;
      }
    }
  }

  if (!unpublishedEvents.empty()) {
    out.insert(out.end(), unpublishedEvents.begin(), unpublishedEvents.end());
    std::sort(out.begin(), out.end(),
              [](const MidiEvent& left, const MidiEvent& right) { return left.tick < right.tick; });
  }
  if (!unpublishedCompanions.empty()) {
    applyNoteEditPassSequence(out, unpublishedCompanions, loopLengthTicks);
  }
  if (pathOut != nullptr) {
    *pathOut = SnapshotWindowGatherPath::Overlay;
  }
  return true;
}
