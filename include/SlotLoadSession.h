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

/// RAII session for one slot load. Enables SD-load staging on LoopEventStore and tracks
/// active-load count for scheduler observation. Phase 1: stack lifetime. Phase 3: may be
/// StorageManager-owned across cooperative slices — same lifecycle.
class SlotLoadSession {
 public:
  SlotLoadSession(uint8_t trackIndex, uint8_t slotIndex);
  ~SlotLoadSession();

  SlotLoadSession(const SlotLoadSession&) = delete;
  SlotLoadSession& operator=(const SlotLoadSession&) = delete;

  void setState(SlotLoadSessionState state);
  void fail();
  void complete();

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
