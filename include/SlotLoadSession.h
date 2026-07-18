//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

/// Lifecycle of one logical slot load (Queued lives on SlotLoadQueue, not here).
enum class SlotLoadSessionState : uint8_t {
  Dequeued = 0,
  Reading,
  Validating,
  Committing,
  Completed,
  Failed,
};

/// Result of one deferred LoadLoopJob / slot-load session advance.
enum class SlotLoadAdvanceResult : uint8_t {
  MoreWork = 0,
  Completed,
  Failed,
};

/// RAII session for one slot load. Enables SD-load staging on LoopEventStore and tracks
/// active-load count for scheduler observation.
///
/// Phase 1: stack lifetime around a sync load.
/// Phase 3: caller drives `advanceAfterPhaseWork()` between phase work units; scheduling
/// (when to call advance) stays outside the session. Boot may still drain to completion
/// in one call via a sync wrapper.
class SlotLoadSession {
 public:
  SlotLoadSession(uint8_t trackIndex, uint8_t slotIndex);
  ~SlotLoadSession();

  SlotLoadSession(const SlotLoadSession&) = delete;
  SlotLoadSession& operator=(const SlotLoadSession&) = delete;

  void setState(SlotLoadSessionState state);
  void fail();
  void complete();

  /// After the caller finishes work for the current non-terminal state, advance to the
  /// next phase: Dequeued→Reading→Validating→Committing→Completed.
  /// Returns MoreWork until Completed/Failed.
  SlotLoadAdvanceResult advanceAfterPhaseWork();

  bool isTerminal() const {
    return state_ == SlotLoadSessionState::Completed || state_ == SlotLoadSessionState::Failed;
  }

  SlotLoadSessionState state() const { return state_; }
  uint8_t trackIndex() const { return trackIndex_; }
  uint8_t slotIndex() const { return slotIndex_; }

  static bool isActive();
  static bool isActiveFor(uint8_t trackIndex, uint8_t slotIndex);
  static SlotLoadSessionState activeState();
  static uint8_t activeCount();

 private:
  uint8_t trackIndex_;
  uint8_t slotIndex_;
  SlotLoadSessionState state_;
  bool finished_;

  static uint8_t activeCount_;
  static uint8_t activeTrack_;
  static uint8_t activeSlot_;
  static SlotLoadSessionState activeState_;
};
