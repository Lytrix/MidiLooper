//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "LoopEventStore.h"

namespace RecordSourceStaging {

enum class Step : uint8_t { Pending, Complete, Empty, Denied };

enum class DenyReason : uint8_t {
  None,
  SourceUnavailable,
  OutputUnavailable,
  ZeroQuantum,
};

/// Copies one frozen Record source into a private prepared output, one row per
/// bounded unit, applying the existing per-row stop projection
/// (`LoopEventStore::projectRecordStopTick`): a NoteOn at or beyond the final
/// length is remapped into the loop, and any other row at or beyond it is
/// dropped. This is the bounded form of `mapNoteOnsAtOrBeyondTickIntoLoop`; the
/// held-note closure Offs are a separate consumer's job.
///
/// The source is an immutable sealed range and stays untouched. The output must
/// be a prewarmed private store (`reservedAppendReady`) whose credits are the
/// caller's; the cursor never allocates and appends only through
/// `appendReserved`. No whole-take copy, index or ordering array is created:
/// ordering is the preparation composition's later, separate grain.
class Cursor {
 public:
  Cursor() = default;
  Cursor(const Cursor&) = delete;
  Cursor& operator=(const Cursor&) = delete;

  /// Bind one frozen source and one private output. `loopLengthTicks == 0`
  /// copies every row unchanged, matching the legacy early return. Performs no
  /// content-sized work.
  bool begin(const LoopEventStore::CaptureRange& source, LoopEventStore& output,
             LoopEventStore::ChunkReservation& reservation, uint32_t loopLengthTicks);

  /// At most `quantum` fixed units (one source row or one chunk transition).
  Step step(size_t quantum);

  void discard();

  bool complete() const { return done_; }
  bool denied() const { return deny_ != DenyReason::None; }
  DenyReason denyReason() const { return deny_; }
  size_t lastStepUnits() const { return lastStepUnits_; }
  size_t copiedRows() const { return copiedRows_; }
  size_t projectedRows() const { return projectedRows_; }
  size_t droppedRows() const { return droppedRows_; }

 private:
  const LoopEventStore::CaptureRange* source_ = nullptr;
  LoopEventStore* output_ = nullptr;
  LoopEventStore::ChunkReservation* reservation_ = nullptr;
  uint64_t sourceGeneration_ = 0;
  size_t chunkCount_ = 0;
  size_t chunk_ = 0;
  uint16_t event_ = 0;
  size_t sourceRows_ = 0;
  uint32_t length_ = 0;
  size_t copiedRows_ = 0;
  size_t projectedRows_ = 0;
  size_t droppedRows_ = 0;
  size_t lastStepUnits_ = 0;
  DenyReason deny_ = DenyReason::None;
  bool begun_ = false;
  bool done_ = false;
  /// The staged rows are private: the cursor holds an ephemeral seal so they
  /// never schedule persistence, and the later preparation composition can then
  /// order/filter them under its own seal.
  bool holdsEphemeralSeal_ = false;
};

}  // namespace RecordSourceStaging
