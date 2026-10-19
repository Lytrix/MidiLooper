//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "CommittedEventRange.h"
#include <array>
#include "LoopInternal.h"
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
                                       uint32_t windowStart, uint32_t windowLength,
                                       uint32_t* inspectedEntriesOut) {
  if (inspectedEntriesOut != nullptr) {
    *inspectedEntriesOut = 0;
  }
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
  uint32_t inspected = 0;
  for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
    const auto membership = snapshot.eventMembership(bar);
    if (!membership) {
      continue;
    }
    forEachSnapshotMembershipReference(
        membership.get(),
        [&collected, &inspected, windowStart, windowEnd](const LoopSnapshotEventRef& reference) {
          if (!reference) {
            return;
          }
          // Every reference this walk *looks at* is inspected work, including the ones the window rejects:
          // W-2.10's bound is about the scan, not about what it appends.
          ++inspected;
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
  if (inspectedEntriesOut != nullptr) {
    *inspectedEntriesOut = inspected;
  }
}

}  // namespace

namespace {

/// DEC-143 -- the record stop closure policy, applied only where the served window **is** the loop. Inside a
/// whole-loop window a note-on with no off genuinely has no off left in the loop, so it is closed with one synthetic
/// off at the loop end carrying the on's own identity: the same rule
/// `RuntimePlaybackView::projectRawCoverageToLoopLength` applies to a Record window, so the overlay and the frozen
/// reader cannot disagree about what a held note means. A partial window closes nothing -- its off may still be in a
/// later window, exactly as committed content is treated there.
void closeRecordOverlayLanesAtLoopEnd(SessionMidiEventVec& events, uint32_t loopLengthTicks) {
  if (loopLengthTicks == 0) {
    return;
  }
  constexpr size_t kOverlayChannels = 16;
  constexpr size_t kOverlayNotes = 128;
  constexpr size_t kOverlayLaneWords = (kOverlayChannels * kOverlayNotes) / 64;
  static_assert(kOverlayLaneWords * 64 >= kOverlayChannels * kOverlayNotes,
                "the overlay lane map covers the MIDI domain");
  std::array<uint64_t, kOverlayLaneWords> closed{};
  SessionMidiEventVec closures;
  for (size_t i = events.size(); i-- > 0;) {
    const MidiEvent& event = events[i];
    if (!event.isNoteOn() && !event.isNoteOff()) {
      continue;
    }
    if (event.channel == 0 || event.channel > kOverlayChannels ||
        event.data.noteData.note >= kOverlayNotes) {
      continue;
    }
    const size_t lane =
        static_cast<size_t>(event.channel - 1) * kOverlayNotes + event.data.noteData.note;
    const uint64_t bit = static_cast<uint64_t>(1) << (lane & 63u);
    uint64_t& word = closed[lane >> 6];
    if (event.isNoteOff()) {
      word |= bit;
      continue;
    }
    if ((word & bit) != 0) {
      word &= ~bit;
      continue;
    }
    MidiEvent terminal =
        MidiEvent::NoteOff(loopLengthTicks - 1, event.channel, event.data.noteData.note, 0);
    terminal.noteId = event.noteId;
    closures.push_back(terminal);
    word |= bit;
  }
  if (closures.empty()) {
    return;
  }
  events.insert(events.end(), closures.begin(), closures.end());
}

}  // namespace

LOOP_COLD_MEM bool Loop::tryGatherPublishedSnapshotEventsInWindow(
    SessionMidiEventVec& out, uint32_t windowStart, uint32_t windowLength,
    SnapshotWindowGatherPath* pathOut, uint32_t* inspectedEntriesOut,
    bool publishedRecordOverlay) const {
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
    // **DEC-143 — a published Record slice is read material before its root publishes.** The caller has stated that
    // this slot's Record take crossed the immutable `RecordPublished` edge (Track owns that edge; the Loop sees only
    // the extent it is already required to read). The window is then filled from that take's **sealed extent** alone:
    // never from mutable `capture.store` rows -- the mutability rule 224719 protects, which this branch does not touch
    // -- and never from a pass's chunk-id list, whose ids undo/redo can recycle while the presentation is still the
    // active reader (the form withdrawn on the device). The extent arrives as this Loop's own read borrow, keyed on
    // the presentation record's identity, and `CaptureRange::read` revalidates the generation, the pool occupant and
    // the seal on every row, so a read here cannot follow a recycled chunk. The branch reports `RecordOverlay` so
    // callers and telemetry can tell it from a published root, and it is superseded the moment a root supplies the
    // same window (`Snapshot` below), so nothing is ever merged twice.
    const LoopEventStore::CaptureRange* publishedRecordExtent = nullptr;
    if (publishedRecordOverlay) {
      const CaptureReadSource readSource = captureReadSource();
      if (readSource.finalizingReady()) {
        publishedRecordExtent = readSource.finalizing;
      }
    }
    // The extent is not the only gate: **undo inside this window disables the pass** (`setCapturePassState`) and bumps
    // the revision, so an extent that is still sealed would keep an undone take sounding until a root republished
    // without it. The publication state cannot express that -- the pass state can -- so a take that is no longer the
    // active Record material is refused here and the caller keeps its own miss.
    const bool recordTakeStillCurrent =
        !passes.hasRecordPass() || passes.recordPass.state == CapturePassState::Active;
    if (publishedRecordExtent != nullptr && !recordTakeStillCurrent) {
      publishedRecordExtent = nullptr;
    }
    if (publishedRecordExtent == nullptr) {
      if (pathOut != nullptr) {
        *pathOut = SnapshotWindowGatherPath::None;
      }
      if (inspectedEntriesOut != nullptr) {
        *inspectedEntriesOut = 0;
      }
      return false;
    }
    out.clear();
    uint32_t overlayStart = windowStart;
    uint32_t overlayLength = windowLength;
    if (loopLengthTicks == 0 || overlayLength == 0) {
      if (pathOut != nullptr) {
        *pathOut = SnapshotWindowGatherPath::None;
      }
      return false;
    }
    const bool wholeLoopWindow = overlayLength >= loopLengthTicks;
    if (wholeLoopWindow) {
      overlayStart = 0;
      overlayLength = loopLengthTicks;
    }
    const uint32_t overlayEnd = overlayStart + overlayLength;
    // Row by row through the sealed descriptor -- the access the frozen Record window and the settled-capture readers
    // already use, bounded by the captured end of the extent rather than by a live extent. The record stop projection
    // is the same one the canonical Record window applies, so a wrapped take cannot arrive out of the loop.
    const uint64_t extentGeneration = publishedRecordExtent->generation();
    const size_t extentChunks = publishedRecordExtent->chunkCount();
    bool extentReadable = true;
    for (size_t chunkIndex = 0; chunkIndex < extentChunks && extentReadable; ++chunkIndex) {
      for (uint16_t eventIndex = 0;; ++eventIndex) {
        MidiEvent event{};
        bool atEnd = false;
        if (!publishedRecordExtent->read(extentGeneration, chunkIndex, eventIndex, event, atEnd)) {
          extentReadable = false;
          break;
        }
        if (atEnd) break;
        if (!LoopEventStore::projectRecordStopTick(event, loopLengthTicks)) continue;
        if (event.tick < overlayStart || event.tick >= overlayEnd) continue;
        out.push_back(event);
      }
    }
    if (!extentReadable) {
      // A row that cannot be read (the extent was released, its generation moved, a chunk left the pool) makes the
      // whole window a miss rather than a truncated success: half a take is worse than holding the miss until the root
      // publishes.
      out.clear();
      if (pathOut != nullptr) {
        *pathOut = SnapshotWindowGatherPath::None;
      }
      return false;
    }
    if (out.empty()) {
      // An empty extent is not a served window; the caller keeps its own miss rather than a silent success.
      if (pathOut != nullptr) {
        *pathOut = SnapshotWindowGatherPath::None;
      }
      return false;
    }
    sortSessionEventsByTick(out);
    if (wholeLoopWindow) {
      closeRecordOverlayLanesAtLoopEnd(out, loopLengthTicks);
      sortSessionEventsByTick(out);
    }
    // No snapshot membership was walked, so nothing is charged to the window-preparation inspection bound: the
    // overlay's own cost is the extent walk over this take's rows, which is the subject of that bound.
    if (inspectedEntriesOut != nullptr) {
      *inspectedEntriesOut = 0;
    }
    if (pathOut != nullptr) {
      *pathOut = SnapshotWindowGatherPath::RecordOverlay;
    }
    return true;
  }

  gatherPublishedSnapshotMembership(*snapshot, out, windowStart, windowLength, inspectedEntriesOut);
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
