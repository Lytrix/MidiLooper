//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "LoopEventStore.h"
#include "Utils/CaptureIncrementalSanity.h"
#include "Utils/LoopStopFinalize.h"

namespace RecordPreparation {

/// Job-owned Record stop policy inputs. The cursor never reads live producer
/// geometry, the selected slot or the current transport: every value here is a
/// receipt the stop admitted before preparation began.
struct Geometry {
  /// Final Record length. Zero skips wrap repair and minimum-length policy,
  /// matching `sealCapture` on a zero-length loop.
  uint32_t loopLengthTicks = 0;
  /// Absolute-in-loop phase of the stop tick, or UINT32_MAX when unknown.
  uint32_t openTailCloseTick = UINT32_MAX;
  /// Wrap-window size in ticks; the legacy stop path uses one bar.
  uint32_t wrapWindow = 768;
  uint32_t minLengthTicks = 0;
  bool minLengthRemoveEnabled = false;
};

/// Caller-owned, exclusively borrowed preparation scratch. It is never a
/// per-slot resident field; one admitted preparation role owns it.
///
/// Wrap repair and minimum-length filtering run in disjoint phases: the tail
/// cursor is discarded before the canonical re-order and the filter cursor
/// begins, so the two lane arrays are one borrowed role rather than two. This
/// halves the scratch a preparation role must own; the cursor still discards the
/// tail cursor before starting the filter phase.
struct Workspace {
  union {
    LoopStopFinalize::TailRepairWorkspace tailRepair;
    CaptureIncrementalSanity::MinimumLengthWorkspace minLength;
  };
};

static_assert(sizeof(Workspace) ==
                  (sizeof(LoopStopFinalize::TailRepairWorkspace) >
                           sizeof(CaptureIncrementalSanity::MinimumLengthWorkspace)
                       ? sizeof(LoopStopFinalize::TailRepairWorkspace)
                       : sizeof(CaptureIncrementalSanity::MinimumLengthWorkspace)),
              "union scratch is one role, not both lane arrays");

/// One synthetic repair Off per MIDI key lane bounds the wrap-output stage.
constexpr size_t kMaxSyntheticOffs = LoopStopFinalize::TailRepairWorkspace::kLanes;

/// Bounded-step outcome. `Pressure` is a retryable resource denial raised only
/// when a private append could not take another chunk from the caller's
/// reservation. The tail cursor repeats the same `EventReady` event until it is
/// acknowledged, and no mark, sort or compaction has been invalidated, so the
/// caller may top up credits and resume the same cursor without duplicating or
/// dropping a row. `Denied` is sticky and terminal for the binding: the private
/// output may already be partially ordered, repaired or marked, so the caller
/// must never discard the role and re-bind over that store.
enum class Step : uint8_t { Pending, Complete, Empty, Pressure, Denied };

/// Named denial for tests and telemetry. The cursor mutates only private output;
/// a denied cursor leaves ownership with the caller, which discards that output.
enum class DenyReason : uint8_t {
  None,
  OutputUnavailable,
  OrderDenied,
  IdentityIncomplete,
  TailInputRejected,
  ReservationMissing,
  TailOutputRejected,
  FilterDenied,
  ZeroQuantum,
};

/// Caller-provided identity allocator for NoteOns that carry none. `context` and
/// `next` must outlive the cursor. A default-constructed source is invalid and the
/// cursor then denies a missing identity instead of allocating one.
struct NoteIdSource {
  void* context = nullptr;
  NoteId (*next)(void* context) = nullptr;
  bool valid() const { return next != nullptr; }
  NoteId take() const { return next != nullptr ? next(context) : kInvalidNoteId; }
};

/// Composes the existing ordered-input Record stop policy over one private,
/// unpublished prepared store:
///   1. canonical (tick, NoteOff < NoteOn < other) private order;
///   2. bounded missing-NoteOn-identity completion in that canonical order;
///   3. wrap-window tail repair, appending synthetic Offs and re-ordering;
///   4. minimum-length pair removal through private filter marking/compaction.
/// The store must be a prewarmed private prepared output: dense (one chunk per
/// 256 rows), fully reserved directory capacity, and chunks not admitted to
/// persistence. Each private append (the synthetic repair rows) runs inside the
/// global ephemeral seal, entered and left within that append, so the seal is
/// always absent whenever a `step` returns and never spans a pending turn.
///
/// Record ingress normally assigns NoteOn identities at admission, and terminal
/// closure Offs carry their receipt. When a source is supplied, a NoteOn that has
/// none is completed in canonical store order (matching `sealCapture`); without
/// one the cursor denies a missing identity rather than inventing it.
class RecordPreparationCursor {
 public:
  RecordPreparationCursor() = default;
  RecordPreparationCursor(const RecordPreparationCursor&) = delete;
  RecordPreparationCursor& operator=(const RecordPreparationCursor&) = delete;

  /// Bind one private prepared store. `generation` must outlive the cursor and
  /// change when the preparation role is replaced. `reservation` supplies
  /// physical credits for the synthetic repair rows and may be null only while
  /// no repair row is emitted (an emitted row then denies with
  /// `ReservationMissing`). `ids` completes missing NoteOn identities. `begin`
  /// performs no content-sized work.
  bool begin(LoopEventStore& output, Workspace& workspace, const uint64_t& generation,
             const Geometry& geometry, LoopEventStore::ChunkReservation* reservation = nullptr,
             const NoteIdSource& ids = NoteIdSource{});

  /// Bounded work: at most `quantum` fixed units (row reads, lane visits,
  /// comparisons, marks). Returns Pending until the terminal state; Empty is
  /// reported once when the prepared store held no rows.
  Step step(size_t quantum);

  void discard();

  bool current() const { return output_ != nullptr && generation_ && *generation_ == expectedGeneration_; }
  bool complete() const { return phase_ == Phase::Complete; }
  bool denied() const { return phase_ == Phase::Denied; }
  DenyReason denyReason() const { return deny_; }
  size_t lastStepUnits() const { return lastStepUnits_; }
  size_t syntheticOffs() const { return syntheticOffs_; }
  size_t pairsRemoved() const { return pairsRemoved_; }
  size_t assignedIds() const { return assignedIds_; }
  bool reportedEmpty() const { return emptyReported_; }

 private:
  enum class Phase : uint8_t {
    Idle,
    Order,
    AssignIds,
    RepairReset,
    RepairPreflight,
    RepairRead,
    RepairOutput,
    Reorder,
    FilterReset,
    FilterPreflight,
    FilterRead,
    FilterCompact,
    Complete,
    Denied,
  };

  void enterPhase(Phase next);
  void deny(DenyReason reason);
  void finishComplete();
  /// Append one synthetic private row under a turn-local ephemeral seal. The
  /// prepared store is private, so a full chunk must not be admitted to
  /// persistence; the seal is released before this returns.
  bool appendPrivately(const MidiEvent& row);

  LoopEventStore* output_ = nullptr;
  Workspace* workspace_ = nullptr;
  const uint64_t* generation_ = nullptr;
  uint64_t expectedGeneration_ = 0;
  Geometry geometry_{};
  LoopEventStore::ChunkReservation* reservation_ = nullptr;
  NoteIdSource ids_{};
  size_t assignedIds_ = 0;
  LoopEventStoreOrderCursor order_{};
  LoopEventStoreFilterCursor filter_{};
  LoopStopFinalize::TailRepairCursor tail_{};
  CaptureIncrementalSanity::MinimumLengthCursor minLength_{};
  size_t inputCount_ = 0;
  size_t ordinal_ = 0;
  size_t syntheticOffs_ = 0;
  size_t pairsRemoved_ = 0;
  size_t lastStepUnits_ = 0;
  DenyReason deny_ = DenyReason::None;
  Phase phase_ = Phase::Idle;
  bool entered_ = false;
  bool empty_ = false;
  bool emptyReported_ = false;
};

}  // namespace RecordPreparation
