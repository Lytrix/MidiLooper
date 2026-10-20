//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "CommittedEventRange.h"
#include "LoopSnapshot.h"
#include "MidiEvent.h"

#include <array>
#include <memory>
#include <vector>

struct PendingPassChangeProgress {
  struct OpenNote {
    NoteId noteId = kInvalidNoteId;
    uint8_t pitch = 0;
    uint8_t velocity = 0;
    uint32_t startTick = 0;
    uint32_t endTick = 0;
    bool hasStart = false;
    bool hasEnd = false;
  };

  PassId passId = kInvalidPassId;
  CommittedEventRange::WindowTraversalCursor cursor{};
  SessionMidiEventVec batch;
  size_t batchIndex = 0;
  EventId nextEventId = 1;
  uint32_t nextCanonicalOrder = 1;
  std::array<OpenNote, kSnapshotWrapJournalMaxChanges> openNotes{};
  uint8_t openNoteCount = 0;
  bool active = false;
  bool fromCaptureStore = false;
  bool storeWindowComplete = false;
  size_t nextStoreIndex = 0;
  uint32_t includeUntil = 0;
  /// The enumeration window for committed-pass chunks (DEC-113, slice 7.2). `windowLengthTicks == 0` means the whole loop
  /// (today's only behaviour); an interval-root build supplies its exact served range verbatim, with no rounding.
  uint32_t windowStartTick = 0;
  uint32_t windowLengthTicks = 0;

  bool freshForInventoryAdoption() const {
    if (passId != kInvalidPassId || cursor.listIndex || cursor.chunkIndex ||
        cursor.chunkEventOffset || cursor.complete || !batch.empty() || batchIndex ||
        nextEventId != 1 || nextCanonicalOrder != 1 || openNoteCount || active ||
        fromCaptureStore || storeWindowComplete || nextStoreIndex || includeUntil ||
        windowStartTick || windowLengthTicks) return false;
    // Fixed 32 slots, not a variable payload scan. Count zero alone is insufficient.
    for (const auto& note : openNotes) {
      if (note.noteId != kInvalidNoteId || note.pitch || note.velocity ||
          note.startTick || note.endTick || note.hasStart || note.hasEnd) return false;
    }
    return true;
  }

  void reset() { *this = PendingPassChangeProgress{}; }

  bool batchExhausted() const { return batchIndex >= batch.size(); }

  bool captureWindowConsumed() const {
    return fromCaptureStore && storeWindowComplete && batchExhausted();
  }

  size_t consumedStoreIndex() const {
    if (!fromCaptureStore) {
      return nextStoreIndex;
    }
    const size_t remaining = batchExhausted() ? 0 : (batch.size() - batchIndex);
    return nextStoreIndex - remaining;
  }

  bool complete() const {
    if (fromCaptureStore) {
      return captureWindowConsumed() && openNoteCount == 0;
    }
    return cursor.complete && batchExhausted();
  }
};

struct PublishedSnapshotBox {
  ImmutableLoopSnapshotRef snapshot;
  uint32_t revision = 0;
  /// DEC-073: the committed-pass revision this root's content was enumerated from. Unlike
  /// `revision` (a stamp for cache keys), this is evidence about the content and is what makes
  /// `Loop::publishedSnapshotIsCurrent()` true.
  uint32_t coveredPassRevision = kSnapshotCoverageUnset;
};

struct PendingSnapshotPublishBox {
  enum class PayloadReleaseResult : uint8_t { Idle, Progress, Complete, Rejected };
  enum class CompanionStoragePolicy : uint8_t { Retain, Release };
  /// Single-use non-owning coordinates. Caller keeps box, original construction,
  /// cursor and allocator alive/unmoved, excludes all writes/publisher/overlay,
  /// semantic borrows, weak resurrection and lifetime/address reuse. This does
  /// not cancel or settle obligations. Accepted boxes must never resume live work.
  class PayloadReleaseCursor {
   public:
    PayloadReleaseCursor() = default;
    PayloadReleaseCursor(const PayloadReleaseCursor&) = delete;
    PayloadReleaseCursor& operator=(const PayloadReleaseCursor&) = delete;
    PayloadReleaseCursor(PayloadReleaseCursor&&) = delete;
    PayloadReleaseCursor& operator=(PayloadReleaseCursor&&) = delete;
    size_t completedUnitCount() const { return units_; }
   private:
    friend struct PendingSnapshotPublishBox;
    const PendingSnapshotPublishBox* owner_ = nullptr;
    LoopSnapshotConstruction::ReleaseCursor constructionCursor_;
    std::array<size_t, 5> positions_{};
    size_t units_ = 0;
    uint8_t phase_ = 0;
    uint8_t fragmentPhase_ = 0;
    uint8_t rootPhase_ = 0;
    CompanionStoragePolicy policy_ = CompanionStoragePolicy::Release;
  };
  PayloadReleaseResult beginPayloadRelease(PayloadReleaseCursor&, CompanionStoragePolicy);
  PayloadReleaseResult stepPayloadRelease(PayloadReleaseCursor&, size_t maximumUnits);
  PayloadReleaseResult payloadReleaseResult(const PayloadReleaseCursor&) const;
  bool sessionExitQueued = false;
  /// Task 3.14c: this request enumerates the committed passes against an **empty** parent rather
  /// than the published root's diff (`resolvePendingParent`). A pass whose rows the published root
  /// already represents still yields a fragment, which is what an edit-pass undo/redo needs: the
  /// committed-pass inventory is unchanged, so the diff is empty, yet the overlay it carried may
  /// no longer be valid. The published root stays selected until the candidate publishes.
  bool rebuildFromPassInventory = false;
  /// DEC-073: the committed-pass revision the box's enumerated pass/companion inventory covers.
  /// It advances only when a request actually enumerates passes into this box, and the candidate
  /// finally published inherits it. A request that arrives later without being enumerated here
  /// leaves the box's coverage behind, so the published root cannot claim to satisfy it.
  uint32_t coveredPassRevision = kSnapshotCoverageUnset;
  /// Loaded adopt/restore: pass events are new to an empty-then-successor
  /// parent. Do not call `findNoteAtPitch` (005731 stack overflow).
  bool fromLoadedPasses = false;
  LoadedSnapshotHydrationPhase loadedHydrationPhase = LoadedSnapshotHydrationPhase::None;
  bool companionsApplied = false;
  bool applyingCompanions = false;
  bool wrapJournalInFlight = false;
  CommitReason sessionExitReason = CommitReason::RecordStop;
  /// Runtime-only GUS entry id for deferred after-root attach (OpenSpec 5.1
  /// Pin A). Zero means no pending attach. Not persisted.
  uint32_t sessionExitUndoAttachEntryId = 0;
  EditPassIdList companionIds;
  std::vector<PassId> queuedPassIds;
  std::vector<PassId> appliedPassIds;
  std::vector<PassId> journaledPassIds;
  std::vector<SnapshotWrapJournalFragment> queuedWrapJournals;
  PendingPassChangeProgress passChangeProgress;
  /// Transactional pass-change fragment (DEC-047 amendment 2026-09-16). A
  /// fragment is built on this working copy; authoritative `passChangeProgress`
  /// advances only after the fragment is successfully incorporated. On failure
  /// the working copy is dropped, so the exact fragment is rebuilt and never
  /// partially applied twice.
  PendingPassChangeProgress fragmentProgress;
  bool fragmentProgressValid = false;
  /// Consecutive failed incorporations of the current fragment. A persistent
  /// representability failure parks publication instead of retrying forever.
  uint16_t consecutiveFragmentFailures = 0;
  bool fragmentBlocked = false;
  /// DEC-073 RC-PUB-2: this park is an *allocation* park, not a representability one. Memory
  /// pressure clears, so a transient park is released once admission has observed the pressure
  /// and then admits again; a representability park never is.
  bool fragmentBlockTransient = false;
  /// DEC-073 RC-PUB-2: set when admission denied while transiently parked. The release requires
  /// it, so the same admission state that let the fragment fail cannot immediately re-arm it.
  bool fragmentParkSawDenial = false;
  PassId constructionPassId = kInvalidPassId;
  size_t appliedLiveRecordEventIndex = 0;
  uint32_t appliedLiveRecordCompletedTicks = 0;
  std::unique_ptr<LoopSnapshotConstruction> construction;
  ImmutableLoopSnapshotRef unpublishedCandidate;

  /// Default semantic shell only; retained EMPTY allocations are permitted.
  /// Retirement admission is irreversible, including after payload completion.
  bool freshForInventoryAdoption() const {
    return !payloadReleaseCursor_ && !sessionExitQueued &&
        coveredPassRevision == kSnapshotCoverageUnset && !fromLoadedPasses &&
        loadedHydrationPhase == LoadedSnapshotHydrationPhase::None &&
        !companionsApplied && !applyingCompanions && !wrapJournalInFlight &&
        sessionExitReason == CommitReason::RecordStop && !sessionExitUndoAttachEntryId &&
        companionIds.empty() && queuedPassIds.empty() && appliedPassIds.empty() &&
        journaledPassIds.empty() && queuedWrapJournals.empty() &&
        passChangeProgress.freshForInventoryAdoption() && fragmentProgress.freshForInventoryAdoption() &&
        !fragmentProgressValid && !consecutiveFragmentFailures && !fragmentBlocked &&
        !fragmentBlockTransient && !fragmentParkSawDenial &&
        constructionPassId == kInvalidPassId && !appliedLiveRecordEventIndex &&
        !appliedLiveRecordCompletedTicks && !construction && !unpublishedCandidate;
  }

  void persistLiveRecordCursor() {
    if (payloadReleaseCursor_) return;
    if (!passChangeProgress.fromCaptureStore) {
      return;
    }
    appliedLiveRecordEventIndex = passChangeProgress.consumedStoreIndex();
    if (passChangeProgress.captureWindowConsumed()) {
      appliedLiveRecordCompletedTicks = passChangeProgress.includeUntil;
    }
  }

 private:
  // Identity only, never dereferenced; permanently gates cursor persistence.
  PayloadReleaseCursor* payloadReleaseCursor_ = nullptr;
};

// Reserved before capture admission. Owns the session identities across close;
// no vector growth, traversal, or snapshot graph release at the stop boundary.
struct SessionExitSnapshotHandoff {
  bool pending = false;
  /// DEC-068: a receipt captured at its interval's stop admission is not consumable until that
  /// interval's final pass has committed and `lastPassId` is filled. The publisher skips a
  /// not-ready receipt in place, so FIFO order is preserved. Legacy handoffs set it on capture.
  bool ready = false;
  CommitReason reason = CommitReason::OverdubStop;
  PassId lastPassId = kInvalidPassId;
  size_t companionRowCount = 0;
  std::vector<PassId> passIds;
  std::vector<EditPassIdList> companionIds;
};
