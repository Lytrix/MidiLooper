//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "CommittedEventRange.h"
#include "LoopSnapshot.h"
#include "MidiEvent.h"

#include <array>
#include <memory>
#include <vector>

/// The id-list idioms the snapshot publisher and its playback view both need. One owner so the
/// "invalid never enters the list" and "append only if absent" rules cannot drift between them.
template <typename Container, typename Id>
bool containsId(const Container& ids, Id id) {
  for (const Id existing : ids) {
    if (existing == id) {
      return true;
    }
  }
  return false;
}

template <typename Container, typename Id>
void appendUniqueId(Container& ids, Id id, Id invalid) {
  if (id == invalid) {
    return;
  }
  if (!containsId(ids, id)) {
    ids.push_back(id);
  }
}

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
  bool sessionExitQueued = false;
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

  void persistLiveRecordCursor() {
    if (!passChangeProgress.fromCaptureStore) {
      return;
    }
    appliedLiveRecordEventIndex = passChangeProgress.consumedStoreIndex();
    if (passChangeProgress.captureWindowConsumed()) {
      appliedLiveRecordCompletedTicks = passChangeProgress.includeUntil;
    }
  }
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
