//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/RecordPreparation.h"

#include "Utils/LoopMem.h"

namespace RecordPreparation {

namespace {

constexpr size_t kMaxTransitions = 96;

/// Cold reset for the borrowed private-store cursors. Their in-class `discard()`
/// helpers are emitted into ITCM when ODR-used, which is not worth 64 bytes of
/// the measured RAM1 padding for a cold preparation role.
LOOP_COLD_MEM void resetOrderCursor(LoopEventStoreOrderCursor& cursor) {
  cursor = LoopEventStoreOrderCursor{};
}
LOOP_COLD_MEM void resetFilterCursor(LoopEventStoreFilterCursor& cursor) {
  cursor = LoopEventStoreFilterCursor{};
}

}  // namespace

LOOP_COLD_MEM void RecordPreparationCursor::enterPhase(Phase next) {
  phase_ = next;
  entered_ = false;
}

bool LOOP_COLD_MEM RecordPreparationCursor::appendPrivately(const MidiEvent& row) {
  // The prepared store is private: hold the ephemeral seal only for this append
  // so a chunk that fills here never schedules persistence, and release it before
  // returning so no pending cursor leaves the global scope active.
  LoopEventStore::enterEphemeralSeal();
  const bool appended = output_->appendReserved(row, *reservation_);
  LoopEventStore::leaveEphemeralSeal();
  return appended;
}

LOOP_COLD_MEM void RecordPreparationCursor::deny(DenyReason reason) {
  deny_ = reason;
  resetOrderCursor(order_);
  resetFilterCursor(filter_);
  tail_.discard();
  minLength_.discard();
  phase_ = Phase::Denied;
  entered_ = true;
}

LOOP_COLD_MEM void RecordPreparationCursor::finishComplete() {
  resetOrderCursor(order_);
  resetFilterCursor(filter_);
  tail_.discard();
  minLength_.discard();
  phase_ = Phase::Complete;
  entered_ = true;
}

bool LOOP_COLD_MEM RecordPreparationCursor::begin(
    LoopEventStore& output, Workspace& workspace, const uint64_t& generation,
    const Geometry& geometry, LoopEventStore::ChunkReservation* reservation,
    const NoteIdSource& ids) {
  if (phase_ != Phase::Idle) {
    return false;
  }
  output_ = &output;
  workspace_ = &workspace;
  generation_ = &generation;
  expectedGeneration_ = generation;
  geometry_ = geometry;
  reservation_ = reservation;
  ids_ = ids;
  inputCount_ = 0;
  ordinal_ = 0;
  syntheticOffs_ = 0;
  pairsRemoved_ = 0;
  assignedIds_ = 0;
  lastStepUnits_ = 0;
  deny_ = DenyReason::None;
  entered_ = false;
  empty_ = false;
  emptyReported_ = false;
  resetOrderCursor(order_);
  resetFilterCursor(filter_);
  tail_.discard();
  minLength_.discard();
  if (output.size() == 0) {
    empty_ = true;
    phase_ = Phase::Complete;
    return true;
  }
  enterPhase(Phase::Order);
  return true;
}

void LOOP_COLD_MEM RecordPreparationCursor::discard() {
  output_ = nullptr;
  workspace_ = nullptr;
  generation_ = nullptr;
  reservation_ = nullptr;
  ids_ = NoteIdSource{};
  resetOrderCursor(order_);
  resetFilterCursor(filter_);
  tail_.discard();
  minLength_.discard();
  phase_ = Phase::Idle;
  entered_ = false;
  deny_ = DenyReason::None;
  inputCount_ = ordinal_ = 0;
  assignedIds_ = 0;
}

Step LOOP_COLD_MEM RecordPreparationCursor::step(size_t quantum) {
  lastStepUnits_ = 0;
  if (phase_ == Phase::Idle) {
    return Step::Denied;
  }
  if (phase_ == Phase::Denied) {
    return Step::Denied;
  }
  if (phase_ == Phase::Complete) {
    if (empty_ && !emptyReported_) {
      emptyReported_ = true;
      return Step::Empty;
    }
    return Step::Complete;
  }
  if (!quantum) {
    deny(DenyReason::ZeroQuantum);
    return Step::Denied;
  }
  if (!current()) {
    deny(DenyReason::OutputUnavailable);
    return Step::Denied;
  }
  if (output_ == nullptr || workspace_ == nullptr) {
    deny(DenyReason::OutputUnavailable);
    return Step::Denied;
  }

  size_t units = 0;
  for (size_t iteration = 0; units < quantum; ++iteration) {
    if (iteration > quantum + kMaxTransitions) {
      deny(DenyReason::OrderDenied);
      lastStepUnits_ = units;
      return Step::Denied;
    }
    switch (phase_) {
      case Phase::Order:
      case Phase::Reorder: {
        if (!entered_) {
          if (!output_->beginPrivateCanonicalOrder(order_)) {
            deny(DenyReason::OutputUnavailable);
            lastStepUnits_ = units;
            return Step::Denied;
          }
          entered_ = true;
        }
        const auto result = output_->stepPrivateCanonicalOrder(order_, quantum - units);
        units += order_.lastStepUnits();
        if (result == LoopEventStoreRewriteResult::Denied) {
          deny(DenyReason::OrderDenied);
          lastStepUnits_ = units;
          return Step::Denied;
        }
        if (result == LoopEventStoreRewriteResult::Pending) {
          break;
        }
        resetOrderCursor(order_);
        if (phase_ == Phase::Order) {
          enterPhase(Phase::AssignIds);
        } else {
          enterPhase(Phase::FilterReset);
        }
        continue;
      }
      case Phase::AssignIds: {
        if (!entered_) {
          if (!output_->beginPrivateFilter(filter_)) {
            deny(DenyReason::OutputUnavailable);
            lastStepUnits_ = units;
            return Step::Denied;
          }
          entered_ = true;
        }
        if (inputCount_ == 0) {
          const auto result = output_->stepPrivateFilter(filter_, quantum - units);
          units += filter_.lastStepUnits();
          if (result == LoopEventStoreFilterResult::Denied) {
            deny(DenyReason::FilterDenied);
            lastStepUnits_ = units;
            return Step::Denied;
          }
          if (result != LoopEventStoreFilterResult::InputReady) {
            break;  // still preflighting; the store is non-empty here
          }
          inputCount_ = output_->size();
          if (inputCount_ == 0) {
            deny(DenyReason::FilterDenied);
            lastStepUnits_ = units;
            return Step::Denied;
          }
        }
        while (units < quantum && ordinal_ < inputCount_) {
          MidiEvent event{};
          if (!output_->readPrivateFilterRow(filter_, ordinal_, event)) {
            deny(DenyReason::FilterDenied);
            lastStepUnits_ = units;
            return Step::Denied;
          }
          if (event.isNoteOn() && event.noteId == kInvalidNoteId) {
            if (!ids_.valid()) {
              deny(DenyReason::IdentityIncomplete);
              lastStepUnits_ = units;
              return Step::Denied;
            }
            const NoteId assigned = ids_.take();
            if (assigned == kInvalidNoteId ||
                !output_->assignPrivateNoteId(filter_, ordinal_, assigned)) {
              deny(DenyReason::IdentityIncomplete);
              lastStepUnits_ = units;
              return Step::Denied;
            }
            ++assignedIds_;
          }
          ++ordinal_;
          ++units;
        }
        if (ordinal_ < inputCount_) {
          break;
        }
        resetFilterCursor(filter_);
        if (geometry_.loopLengthTicks == 0) {
          finishComplete();
        } else {
          enterPhase(Phase::RepairReset);
        }
        continue;
      }
      case Phase::RepairReset: {
        if (!entered_) {
          if (!tail_.begin(workspace_->tailRepair, *generation_, geometry_.loopLengthTicks,
                           geometry_.openTailCloseTick, geometry_.wrapWindow)) {
            deny(DenyReason::TailInputRejected);
            lastStepUnits_ = units;
            return Step::Denied;
          }
          entered_ = true;
        }
        const auto state = tail_.stepReset(quantum - units);
        units += tail_.lastStepUnits();
        if (state == LoopStopFinalize::TailRepairStep::Denied) {
          deny(DenyReason::TailInputRejected);
          lastStepUnits_ = units;
          return Step::Denied;
        }
        if (state == LoopStopFinalize::TailRepairStep::Pending) {
          break;
        }
        enterPhase(Phase::RepairPreflight);
        continue;
      }
      case Phase::RepairPreflight: {
        if (!entered_) {
          if (!output_->beginPrivateFilter(filter_)) {
            deny(DenyReason::OutputUnavailable);
            lastStepUnits_ = units;
            return Step::Denied;
          }
          entered_ = true;
        }
        const auto result = output_->stepPrivateFilter(filter_, quantum - units);
        units += filter_.lastStepUnits();
        if (result == LoopEventStoreFilterResult::Denied) {
          deny(DenyReason::FilterDenied);
          lastStepUnits_ = units;
          return Step::Denied;
        }
        if (result == LoopEventStoreFilterResult::Pending) {
          break;
        }
        inputCount_ = output_->size();
        ordinal_ = 0;
        enterPhase(Phase::RepairRead);
        continue;
      }
      case Phase::RepairRead: {
        while (units < quantum && ordinal_ < inputCount_) {
          MidiEvent event{};
          if (!output_->readPrivateFilterRow(filter_, ordinal_, event)) {
            deny(DenyReason::FilterDenied);
            lastStepUnits_ = units;
            return Step::Denied;
          }
          if (!tail_.consume(event)) {
            deny(DenyReason::TailInputRejected);
            lastStepUnits_ = units;
            return Step::Denied;
          }
          ++ordinal_;
          ++units;
        }
        if (ordinal_ < inputCount_) {
          break;
        }
        if (!tail_.finishInput()) {
          deny(DenyReason::TailInputRejected);
          lastStepUnits_ = units;
          return Step::Denied;
        }
        resetFilterCursor(filter_);
        enterPhase(Phase::RepairOutput);
        continue;
      }
      case Phase::RepairOutput: {
        if (units >= quantum) {
          break;
        }
        MidiEvent event{};
        const auto state = tail_.stepOutput(quantum - units, event);
        units += tail_.lastStepUnits();
        if (state == LoopStopFinalize::TailRepairStep::Denied) {
          deny(DenyReason::TailOutputRejected);
          lastStepUnits_ = units;
          return Step::Denied;
        }
        if (state == LoopStopFinalize::TailRepairStep::Pending) {
          break;
        }
        if (state == LoopStopFinalize::TailRepairStep::EventReady) {
          if (reservation_ == nullptr) {
            deny(DenyReason::ReservationMissing);
            lastStepUnits_ = units;
            return Step::Denied;
          }
          if (!appendPrivately(event)) {
            // Transient credit pressure. The tail cursor repeats this same
            // EventReady until acknowledged, and the removal marks are untouched,
            // so the caller resumes the same cursor after topping up credits.
            lastStepUnits_ = units;
            return Step::Pressure;
          }
          ++syntheticOffs_;
          if (!tail_.acknowledgeOutput()) {
            deny(DenyReason::TailOutputRejected);
            lastStepUnits_ = units;
            return Step::Denied;
          }
          continue;
        }
        // Complete.
        tail_.discard();
        enterPhase(Phase::Reorder);
        continue;
      }
      case Phase::FilterReset: {
        // The union Workspace reuses the repair lanes here: the tail cursor is
        // finished (discarded at the end of RepairOutput) and is re-discarded so
        // no repair state can alias the minimum-length lanes.
        tail_.discard();
        if (!geometry_.minLengthRemoveEnabled || geometry_.minLengthTicks == 0) {
          finishComplete();
          continue;
        }
        if (!entered_) {
          if (!minLength_.begin(workspace_->minLength, *generation_, geometry_.loopLengthTicks,
                                geometry_.minLengthTicks, true)) {
            deny(DenyReason::FilterDenied);
            lastStepUnits_ = units;
            return Step::Denied;
          }
          entered_ = true;
        }
        const auto state = minLength_.stepReset(quantum - units);
        units += minLength_.lastStepUnits();
        if (state == CaptureIncrementalSanity::MinimumLengthStep::Denied) {
          deny(DenyReason::FilterDenied);
          lastStepUnits_ = units;
          return Step::Denied;
        }
        if (state == CaptureIncrementalSanity::MinimumLengthStep::Pending) {
          break;
        }
        enterPhase(Phase::FilterPreflight);
        continue;
      }
      case Phase::FilterPreflight: {
        if (!entered_) {
          if (!output_->beginPrivateFilter(filter_)) {
            deny(DenyReason::OutputUnavailable);
            lastStepUnits_ = units;
            return Step::Denied;
          }
          entered_ = true;
        }
        const auto result = output_->stepPrivateFilter(filter_, quantum - units);
        units += filter_.lastStepUnits();
        if (result == LoopEventStoreFilterResult::Denied) {
          deny(DenyReason::FilterDenied);
          lastStepUnits_ = units;
          return Step::Denied;
        }
        if (result == LoopEventStoreFilterResult::Pending) {
          break;
        }
        inputCount_ = output_->size();
        ordinal_ = 0;
        enterPhase(Phase::FilterRead);
        continue;
      }
      case Phase::FilterRead: {
        while (units < quantum && ordinal_ < inputCount_) {
          MidiEvent event{};
          if (!output_->readPrivateFilterRow(filter_, ordinal_, event)) {
            deny(DenyReason::FilterDenied);
            lastStepUnits_ = units;
            return Step::Denied;
          }
          const auto state = minLength_.consume(event);
          if (state == CaptureIncrementalSanity::MinimumLengthStep::Denied) {
            deny(DenyReason::FilterDenied);
            lastStepUnits_ = units;
            return Step::Denied;
          }
          ++ordinal_;
          ++units;
          if (state == CaptureIncrementalSanity::MinimumLengthStep::PairReady) {
            size_t on = 0;
            size_t off = 0;
            if (!minLength_.pendingPair(on, off) ||
                !output_->markPrivateFilterPair(filter_, on, off) ||
                !minLength_.acknowledgePair()) {
              deny(DenyReason::FilterDenied);
              lastStepUnits_ = units;
              return Step::Denied;
            }
            ++pairsRemoved_;
          }
        }
        if (ordinal_ < inputCount_) {
          break;
        }
        if (!minLength_.finishInput() || !output_->finishPrivateFilter(filter_)) {
          deny(DenyReason::FilterDenied);
          lastStepUnits_ = units;
          return Step::Denied;
        }
        enterPhase(Phase::FilterCompact);
        continue;
      }
      case Phase::FilterCompact: {
        if (units >= quantum) {
          break;
        }
        const auto result = output_->stepPrivateFilter(filter_, quantum - units);
        units += filter_.lastStepUnits();
        if (result == LoopEventStoreFilterResult::Denied) {
          deny(DenyReason::FilterDenied);
          lastStepUnits_ = units;
          return Step::Denied;
        }
        if (result == LoopEventStoreFilterResult::Pending) {
          break;
        }
        resetFilterCursor(filter_);
        finishComplete();
        continue;
      }
      case Phase::Complete: {
        lastStepUnits_ = units;
        if (empty_ && !emptyReported_) {
          emptyReported_ = true;
          return Step::Empty;
        }
        return Step::Complete;
      }
      case Phase::Idle:
      case Phase::Denied:
      default: {
        lastStepUnits_ = units;
        return Step::Denied;
      }
    }
    // A `break` from any phase means its budget was consumed without a
    // transition; the outer for-loop condition exits.
  }
  lastStepUnits_ = units;
  return Step::Pending;
}

}  // namespace RecordPreparation
