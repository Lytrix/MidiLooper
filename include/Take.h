//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <vector>

#include "LoopEventStore.h"
#include "Utils/ExtMemAllocator.h"

using LoopId = uint32_t;
using TakeId = uint32_t;

constexpr LoopId kInvalidLoopId = UINT32_MAX;
constexpr TakeId kInvalidTakeId = 0;

/// Active capture phase for the unified record/overdub append buffer.
enum class CapturePhase : uint8_t { None, Record, Overdub };

enum class TakeState : uint8_t { Pending, Active, Disabled };

enum class TakeType : uint8_t { Record, Overdub };

enum class SealOutcome : uint8_t {
  Ok,
  SkippedEmpty,
  FailedValidation,
  AlreadyPending,
  AtTakeCap,
};

enum class CommitResult : uint8_t { Skipped, Published, SealFailed };

enum class CommitReason : uint8_t {
  RecordStop,
  RecordStopToStopped,
  OverdubStop,
  OverdubStopToStopped,
};

namespace TakeConfig {
/// Matches Config::MAX_UNDO_HISTORY — cap on sealed takes retained per loop.
constexpr uint8_t MAX_TAKES_PER_LOOP = 25;
}  // namespace TakeConfig

struct Take {
  TakeId id = kInvalidTakeId;
  uint32_t mergeSequence = 0;
  ChunkIdList chunkRefs;
  TakeState state = TakeState::Pending;
  TakeType type = TakeType::Record;
  uint32_t sealedAtTick = 0;
};

/// Mutable pre-commit MIDI writer (sole capture buffer for record/overdub).
struct Capture {
  LoopEventStore store;
  CapturePhase phase = CapturePhase::None;
};

using TakeVec = std::vector<Take, ExtMemAllocator<Take>>;

inline TakeType takeTypeForCapturePhase(CapturePhase phase) {
  switch (phase) {
    case CapturePhase::Overdub:
      return TakeType::Overdub;
    case CapturePhase::Record:
      return TakeType::Record;
    case CapturePhase::None:
    default:
      return TakeType::Record;
  }
}
