//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "LoopEventStore.h"

namespace RecordSourceStaging {

/// Bounded-step outcome. `Pressure` is a retryable resource denial: the row that
/// could not be appended was not consumed, no output was mutated, and the cursor
/// keeps its exact source position, so the caller may top up credits and resume
/// without duplicating or dropping a row. `Denied` is fatal and the caller must
/// discard the cursor.
enum class Step : uint8_t { Pending, Complete, Empty, Pressure, Denied };

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
///
/// Each append runs inside the global ephemeral seal (see `appendPrivately`) so a
/// private row that fills its chunk never schedules persistence. The seal is
/// entered and left within the append itself, so it is always absent whenever a
/// `step` returns: a pending cursor must never leave another track's normal
/// capture sealing a chunk under it.
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
  /// Append one private row under a turn-local ephemeral seal. The output is a
  /// private prepared store, so a full chunk must not be admitted to persistence;
  /// the seal is released before this returns.
  bool appendPrivately(const MidiEvent& row);

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
};

}  // namespace RecordSourceStaging
