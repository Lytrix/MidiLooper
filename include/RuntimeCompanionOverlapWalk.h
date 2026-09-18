// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once
#include "RuntimePlaybackView.h"
#include "Utils/NoteUtils.h"

enum class RuntimeCompanionOverlapResult : uint8_t { Pending, ActionReady, Complete, Denied };

/// Walks an immutable reconstructed-note snapshot in legacy Length action order:
/// all shortenings, then all deletions. The owner keeps the snapshot unchanged
/// while applying actions to separate unpublished events. Reconstruction and
/// publication are not performed here.
class RuntimeCompanionOverlapWalk {
 public:
  enum class ActionType : uint8_t { Shorten, Delete };
  struct Action {
    ActionType kind = ActionType::Shorten;
    NoteId noteId = kInvalidNoteId;
    uint32_t endTick = 0;
    /// Native-test contract probe (no production reader): the source index the
    /// action was matched from, asserted to survive a denied re-step.
    size_t sourceIndex = 0;
  };
  bool available() const { return phase_ == Phase::Idle || phase_ == Phase::Complete; }
  bool begin(const NoteUtils::DisplayNote* notes, size_t count, NoteId target,
             uint8_t pitch, uint32_t start, uint32_t newEnd, uint32_t loopLength);
  RuntimeCompanionOverlapResult step(size_t inputQuantum);
  /// Native-test contract probe (no production caller): asserts the bounded
  /// per-grain input ceiling.
  size_t lastStepInputCount() const { return lastStepInputCount_; }
  const Action& action() const { return action_; }
  bool consumeAction();
  void discard();

 private:
  enum class Phase : uint8_t { Idle, Shorten, Delete, Complete };
  const NoteUtils::DisplayNote* notes_ = nullptr;
  size_t count_ = 0;
  size_t cursor_ = 0;
  size_t lastStepInputCount_ = 0;
  NoteId target_ = kInvalidNoteId;
  uint8_t pitch_ = 0;
  uint32_t start_ = 0;
  uint32_t end_ = 0;
  uint32_t loopLength_ = 0;
  Phase phase_ = Phase::Idle;
  Action action_;
  bool actionReady_ = false;
};
