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

  void reset() { *this = PendingPassChangeProgress{}; }

  bool batchExhausted() const { return batchIndex >= batch.size(); }

  bool complete() const { return cursor.complete && batchExhausted(); }
};

struct PublishedSnapshotBox {
  ImmutableLoopSnapshotRef snapshot;
  uint32_t revision = 0;
};

struct PendingSnapshotPublishBox {
  bool sessionExitQueued = false;
  bool companionsApplied = false;
  bool applyingCompanions = false;
  bool wrapJournalInFlight = false;
  CommitReason sessionExitReason = CommitReason::RecordStop;
  EditPassIdList companionIds;
  std::vector<PassId> queuedPassIds;
  std::vector<PassId> appliedPassIds;
  std::vector<PassId> journaledPassIds;
  std::vector<SnapshotWrapJournalFragment> queuedWrapJournals;
  PendingPassChangeProgress passChangeProgress;
  PassId constructionPassId = kInvalidPassId;
  size_t appliedLiveRecordEventCount = 0;
  uint32_t appliedLiveRecordCompletedTicks = 0;
  std::unique_ptr<LoopSnapshotConstruction> construction;
  ImmutableLoopSnapshotRef unpublishedCandidate;
};
