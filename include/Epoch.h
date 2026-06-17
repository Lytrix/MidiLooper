//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <vector>

#include "LoopEventStore.h"
#include "Utils/ExtMemAllocator.h"

using LoopId = uint32_t;
using EpochId = uint32_t;

constexpr LoopId kInvalidLoopId = UINT32_MAX;
constexpr EpochId kInvalidEpochId = 0;

/// Active capture phase for the unified record/overdub append buffer.
enum class CapturePhase : uint8_t { None, Record, Overdub };

enum class EpochState : uint8_t { Pending, Active, Disabled };

enum class EpochKind : uint8_t { Record, Overdub, Edit };

enum class SealOutcome : uint8_t {
  Ok,
  SkippedEmpty,
  FailedValidation,
  AlreadyPending,
  AtEpochCap,
};

enum class CommitResult : uint8_t { Skipped, Published, SealFailed };

enum class CommitReason : uint8_t {
  RecordStop,
  RecordStopToStopped,
  OverdubStop,
  OverdubStopToStopped,
};

namespace EpochConfig {
/// Matches Config::MAX_UNDO_HISTORY — cap on sealed epochs retained per loop.
constexpr uint8_t MAX_EPOCHS_PER_LOOP = 25;
}  // namespace EpochConfig

struct Epoch {
  EpochId id = kInvalidEpochId;
  uint32_t mergeSequence = 0;
  ChunkIdList chunkRefs;
  EpochState state = EpochState::Pending;
  EpochKind kind = EpochKind::Record;
  uint32_t sealedAtTick = 0;
};

/// Mutable pre-Seal MIDI writer (sole capture buffer for record/overdub).
struct CaptureLayer {
  LoopEventStore store;
  CapturePhase phase = CapturePhase::None;
};

using EpochVec = std::vector<Epoch, ExtMemAllocator<Epoch>>;

inline EpochKind epochKindForCapturePhase(CapturePhase phase) {
  switch (phase) {
    case CapturePhase::Overdub:
      return EpochKind::Overdub;
    case CapturePhase::Record:
      return EpochKind::Record;
    case CapturePhase::None:
    default:
      return EpochKind::Record;
  }
}
