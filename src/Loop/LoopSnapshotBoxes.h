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
};

struct PendingSnapshotPublishBox {
  bool sessionExitQueued = false;
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
  CommitReason reason = CommitReason::OverdubStop;
  PassId lastPassId = kInvalidPassId;
  size_t companionRowCount = 0;
  std::vector<PassId> passIds;
  std::vector<EditPassIdList> companionIds;
};
