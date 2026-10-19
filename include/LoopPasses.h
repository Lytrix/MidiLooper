//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <vector>
#include <type_traits>

#include "EditPass.h"
#include "LoopGeometry.h"
#include "LoopEventStore.h"
#include "Utils/ExternalMemoryFirstAllocator.h"
#include "Utils/InternalHeapFirstAllocator.h"

using LoopId = uint32_t;
constexpr LoopId kInvalidLoopId = UINT32_MAX;

/// Active capture phase for the unified record/overdub append buffer.
enum class CapturePhase : uint8_t { None, Record, Overdub };

/// Assignment-qualified Capture interval/phase identity. Public legacy enum
/// writes retain their meaning, but even same-phase/reopen assignment cannot
/// resurrect a borrowed receipt. Identity exhaustion never prevents the write.
class CapturePhaseState {
 public:
  operator CapturePhase() const { return value_; }
  template <typename Integer,
            typename std::enable_if<std::is_integral<Integer>::value, int>::type = 0>
  explicit operator Integer() const { return static_cast<Integer>(value_); }
  uint64_t generation() const { return generation_; }
  CapturePhaseState& operator=(CapturePhase value);
  CapturePhaseState& operator=(const CapturePhaseState& other) {
    return *this = static_cast<CapturePhase>(other);
  }
 private:
#if defined(PIO_UNIT_TEST_NATIVE)
  friend struct CapturePhaseStateTestAccess;
#endif
  uint64_t generation_ = 1;
  CapturePhase value_ = CapturePhase::None;
};

/// Raw Capture rows have separate destructive identity. This receipt freezes
/// semantic phase/interval and exact projection scalars, not physical lifetime.
struct CaptureProjectionIdentity {
  uint64_t phaseGeneration = 0;
  uint32_t startLoopTick = 0, loopStartTick = 0, loopLengthTicks = 0;
  CapturePhase phase = CapturePhase::None;
  bool operator==(const CaptureProjectionIdentity& other) const {
    return phaseGeneration == other.phaseGeneration && phase == other.phase &&
        startLoopTick == other.startLoopTick && loopStartTick == other.loopStartTick &&
        loopLengthTicks == other.loopLengthTicks;
  }
};

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
  OverdubWrap,
};

/// `OverdubPass.overdubSessionIndex` 0: legacy / missing OSI1 — do not group wraps.
constexpr uint8_t kUngroupedOverdubSessionIndex = 0;

namespace PassConfig {
/// Chunks held back so playback and admission retain headroom.
constexpr uint16_t CHUNK_RESERVE = 16;
/// Active overdub canonical-transfer horizon. Each admitted pass needs one
/// prewarmed pass row and at most this many committed chunk references. The
/// horizon is replenished only while STOPPED; active transport never grows
/// either canonical vector.
constexpr uint16_t ACTIVE_OVERDUB_TRANSFER_HORIZON = 16;
constexpr uint16_t ACTIVE_OVERDUB_TRANSFER_CHUNKS = 8;
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
  uint8_t overdubSessionIndex = kUngroupedOverdubSessionIndex;
};

struct PendingCapturePass {
  // Partial directory ownership is not a publishable or readable pass.
  bool preparationPending = false;
  // Runtime ownership receipt; never serialized into canonical passes.
  bool retiresLiveCapture = true;
  PassId id = kInvalidPassId;
  CapturePassPhase phase = CapturePassPhase::Record;
  uint32_t mergeSequence = 0;
  CommittedChunkIdList committedChunkIds;
  uint32_t sealedAtTick = 0;
  uint8_t overdubSessionIndex = kUngroupedOverdubSessionIndex;
};

/// Scalar receipt for the no-allocation canonical transfer. Chunk references move directly
/// into the canonical pass and are intentionally absent here, so collecting the receipt cannot
/// release, copy, or allocate event storage.
struct PreparedCapturePassTransfer {
  // Runtime ownership receipt; never serialized into canonical passes.
  bool retiresLiveCapture = true;
  PassId id = kInvalidPassId;
  CapturePassPhase phase = CapturePassPhase::Record;
  uint32_t mergeSequence = 0;
  uint32_t sealedAtTick = 0;
  uint8_t overdubSessionIndex = kUngroupedOverdubSessionIndex;
};

/// Mutable pre-commit MIDI writer (sole capture buffer for record/overdub).
struct Capture {
  LoopEventStore store;
  CapturePhaseState phase;
};

using CommittedOverdubPassVec =
    std::vector<OverdubPass, ExternalMemoryFirstAllocator<OverdubPass>>;

struct LoopPasses {
  RecordPass recordPass{};
  CommittedOverdubPassVec overdubPasses;
  EditPassVec editPasses;
  LoopGeometryVec loopGeometries;

  bool hasRecordPass() const { return recordPass.id != kInvalidPassId; }

  size_t capturePassCount() const {
    return (hasRecordPass() ? 1u : 0u) + overdubPasses.size();
  }

  void materialize(LoopEventStore& out, uint32_t loopLengthTicks = 0) const;
  /// Apply note edits on each capture pass, then merge. Hide of a this-session
  /// overdub Add must not LIFO-pair across later same-pitch offs (001517).
  void materializeToEventVector(MidiEventVec& out, uint32_t loopLengthTicks = 0) const;
  void materializeToEventVector(SessionMidiEventVec& out, uint32_t loopLengthTicks = 0) const;
  /// Pass replay with selected edit rows disabled (LoopPasses owner — tests + note-edit undo fallback).
  void materializeExcludingEditPassIds(const EditPassIdList& excludeIds, SessionMidiEventVec& out,
                                     uint32_t loopLengthTicks) const;
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
