//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/RecordSourceStaging.h"

#include "Utils/LoopMem.h"

namespace RecordSourceStaging {

namespace {
LOOP_COLD_MEM void releaseSeal(bool& held) {
  if (held) {
    LoopEventStore::leaveEphemeralSeal();
    held = false;
  }
}
}  // namespace

bool LOOP_COLD_MEM Cursor::begin(const LoopEventStore::CaptureRange& source,
                                 LoopEventStore& output,
                                 LoopEventStore::ChunkReservation& reservation,
                                 uint32_t loopLengthTicks) {
  if (begun_) {
    return false;
  }
  if (!source.ready()) {
    deny_ = DenyReason::SourceUnavailable;
    return false;
  }
  if (!output.reservedAppendReady()) {
    deny_ = DenyReason::OutputUnavailable;
    return false;
  }
  source_ = &source;
  output_ = &output;
  reservation_ = &reservation;
  sourceGeneration_ = source.generation();
  chunkCount_ = source.chunkCount();
  sourceRows_ = source.size();
  length_ = loopLengthTicks;
  chunk_ = 0;
  event_ = 0;
  copiedRows_ = projectedRows_ = droppedRows_ = 0;
  lastStepUnits_ = 0;
  deny_ = DenyReason::None;
  begun_ = true;
  done_ = chunkCount_ == 0;
  // The output is a private prepared store; hold an ephemeral seal while staged
  // rows are appended so none of them schedule persistence.
  LoopEventStore::enterEphemeralSeal();
  holdsEphemeralSeal_ = true;
  return true;
}

void LOOP_COLD_MEM Cursor::discard() {
  releaseSeal(holdsEphemeralSeal_);
  source_ = nullptr;
  output_ = nullptr;
  reservation_ = nullptr;
  begun_ = false;
  done_ = false;
  deny_ = DenyReason::None;
  chunk_ = event_ = 0;
  lastStepUnits_ = 0;
}

Step LOOP_COLD_MEM Cursor::step(size_t quantum) {
  lastStepUnits_ = 0;
  if (!begun_) {
    return Step::Denied;
  }
  if (deny_ != DenyReason::None) {
    return Step::Denied;
  }
  if (done_) {
    releaseSeal(holdsEphemeralSeal_);
    return copiedRows_ == 0 && sourceRows_ == 0 ? Step::Empty : Step::Complete;
  }
  if (!quantum) {
    deny_ = DenyReason::ZeroQuantum;
    releaseSeal(holdsEphemeralSeal_);
    return Step::Denied;
  }

  size_t units = 0;
  while (units < quantum && chunk_ < chunkCount_) {
    ++units;
    MidiEvent row{};
    bool atEnd = false;
    if (!source_->read(sourceGeneration_, chunk_, event_, row, atEnd)) {
      deny_ = DenyReason::SourceUnavailable;
      releaseSeal(holdsEphemeralSeal_);
      lastStepUnits_ = units;
      return Step::Denied;
    }
    if (atEnd) {
      ++chunk_;
      event_ = 0;
      continue;
    }
    if (length_ != 0) {
      const uint32_t before = row.tick;
      if (!LoopEventStore::projectRecordStopTick(row, length_)) {
        ++droppedRows_;
        ++event_;
        continue;
      }
      if (row.tick != before) {
        ++projectedRows_;
      }
    }
    if (!output_->appendReserved(row, *reservation_)) {
      deny_ = DenyReason::OutputUnavailable;
      releaseSeal(holdsEphemeralSeal_);
      lastStepUnits_ = units;
      return Step::Denied;
    }
    ++copiedRows_;
    ++event_;
  }
  lastStepUnits_ = units;
  if (chunk_ >= chunkCount_) {
    done_ = true;
    releaseSeal(holdsEphemeralSeal_);
    return copiedRows_ == 0 && sourceRows_ == 0 ? Step::Empty : Step::Complete;
  }
  return Step::Pending;
}

}  // namespace RecordSourceStaging
