//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <vector>

#include "EditPass.h"
#include "LoopEventStore.h"
#include "Utils/ExternalMemoryFirstAllocator.h"
#include "Utils/InternalHeapFirstAllocator.h"

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
  PoolExhausted,
};

enum class CommitResult : uint8_t { Skipped, Committed, SealFailed };

enum class CommitReason : uint8_t {
  RecordStop,
  RecordStopToStopped,
  OverdubStop,
  OverdubStopToStopped,
};

namespace PassConfig {
/// Chunks held back so playback and admission retain headroom.
constexpr uint16_t CHUNK_RESERVE = 16;
}  // namespace PassConfig

struct RecordPass {
  PassId id = kInvalidPassId;
  CommittedChunkIdList committedChunkIds;
  CapturePassState state = CapturePassState::Active;
  uint32_t sealedAtTick = 0;
};

struct OverdubPass {
  PassId id = kInvalidPassId;
  uint32_t mergeSequence = 0;
  CommittedChunkIdList committedChunkIds;
  CapturePassState state = CapturePassState::Active;
  uint32_t sealedAtTick = 0;
};

struct PendingCapturePass {
  PassId id = kInvalidPassId;
  CapturePassPhase phase = CapturePassPhase::Record;
  uint32_t mergeSequence = 0;
  CommittedChunkIdList committedChunkIds;
  uint32_t sealedAtTick = 0;
};

/// Mutable pre-commit MIDI writer (sole capture buffer for record/overdub).
struct Capture {
  LoopEventStore store;
  CapturePhase phase = CapturePhase::None;
};

using CommittedOverdubPassVec =
    std::vector<OverdubPass, ExternalMemoryFirstAllocator<OverdubPass>>;

struct LoopPasses {
  RecordPass recordPass{};
  CommittedOverdubPassVec overdubPasses;
  EditPassVec editPasses;

  bool hasRecordPass() const { return recordPass.id != kInvalidPassId; }

  size_t capturePassCount() const {
    return (hasRecordPass() ? 1u : 0u) + overdubPasses.size();
  }

  void materialize(LoopEventStore& out, uint32_t loopLengthTicks = 0) const;
  void materializeToEventVector(MidiEventVec& out, uint32_t loopLengthTicks = 0) const;
  void materializeToEventVector(SessionMidiEventVec& out, uint32_t loopLengthTicks = 0) const;
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
