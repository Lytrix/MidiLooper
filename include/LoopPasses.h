//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <vector>

#include "EditPass.h"
#include "LoopEventStore.h"
#include "Utils/ExtMemAllocator.h"

using LoopId = uint32_t;
constexpr LoopId kInvalidLoopId = UINT32_MAX;

/// Active capture phase for the unified record/overdub append buffer.
enum class CapturePhase : uint8_t { None, Record, Overdub };

enum class CapturePassState : uint8_t { Active, Disabled };

enum class CapturePassPhase : uint8_t { Record, Overdub };

enum class SealOutcome : uint8_t {
  Ok,
  SkippedEmpty,
  FailedValidation,
  AlreadyPending,
  AtPassCap,
};

enum class CommitResult : uint8_t { Skipped, Published, SealFailed };

enum class CommitReason : uint8_t {
  RecordStop,
  RecordStopToStopped,
  OverdubStop,
  OverdubStopToStopped,
};

namespace PassConfig {
/// Matches Config::MAX_UNDO_HISTORY — cap on capture passes retained per loop.
constexpr uint8_t MAX_CAPTURE_PASSES_PER_LOOP = 25;
}  // namespace PassConfig

struct RecordPass {
  PassId id = kInvalidPassId;
  ChunkIdList chunkRefs;
  CapturePassState state = CapturePassState::Active;
  uint32_t sealedAtTick = 0;
};

struct OverdubPass {
  PassId id = kInvalidPassId;
  uint32_t mergeSequence = 0;
  ChunkIdList chunkRefs;
  CapturePassState state = CapturePassState::Active;
  uint32_t sealedAtTick = 0;
};

struct PendingCapturePass {
  PassId id = kInvalidPassId;
  CapturePassPhase phase = CapturePassPhase::Record;
  uint32_t mergeSequence = 0;
  ChunkIdList chunkRefs;
  uint32_t sealedAtTick = 0;
};

/// Mutable pre-commit MIDI writer (sole capture buffer for record/overdub).
struct Capture {
  LoopEventStore store;
  CapturePhase phase = CapturePhase::None;
};

using OverdubPassVec = std::vector<OverdubPass, ExtMemAllocator<OverdubPass>>;

struct LoopPasses {
  RecordPass recordPass{};
  OverdubPassVec overdubPasses;
  EditPassVec editPasses;

  bool hasRecordPass() const { return recordPass.id != kInvalidPassId; }

  size_t capturePassCount() const {
    return (hasRecordPass() ? 1u : 0u) + overdubPasses.size();
  }

  void materialize(LoopEventStore& out, uint32_t loopLengthTicks = 0) const;
  void materializeToFlat(MidiEventVec& out, uint32_t loopLengthTicks = 0) const;
};

inline CapturePassPhase capturePassPhaseForCapturePhase(CapturePhase phase) {
  return phase == CapturePhase::Overdub ? CapturePassPhase::Overdub : CapturePassPhase::Record;
}

/// At most one recordPass — route second record capture to overdub.
inline CapturePassPhase effectiveCapturePassPhase(CapturePhase phase, bool hasRecordPass) {
  if (phase == CapturePhase::Record && hasRecordPass) {
    return CapturePassPhase::Overdub;
  }
  return capturePassPhaseForCapturePhase(phase);
}
