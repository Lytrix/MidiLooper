// Copyright (c) 2025 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "Utils/LoopMem.h"
#include "LoopSnapshotBoxes.h"

namespace {
template <typename Vector>
LOOP_COLD_MEM bool retirePendingValueRowOrBuffer(Vector& rows, bool retain = false) {
  if (!rows.empty()) { rows.pop_back(); return false; }
  if (!retain) Vector{}.swap(rows); // Empty allocation only; never populated clear.
  return true;
}
template <typename Vector>
LOOP_COLD_MEM bool retirePendingChangeRowOrBuffer(Vector& rows) {
  if (rows.empty()) { Vector{}.swap(rows); return true; }
  if (rows.back().previous) rows.back().previous.reset(); // Leaf edge, not a graph.
  else rows.pop_back(); // AFTER previous edge retirement, on a separate grant.
  return false;
}
}

LOOP_COLD_MEM PendingSnapshotPublishBox::PayloadReleaseResult
PendingSnapshotPublishBox::payloadReleaseResult(const PayloadReleaseCursor& cursor) const {
  if (!cursor.owner_) return PayloadReleaseResult::Idle;
  if (cursor.owner_ != this || payloadReleaseCursor_ != &cursor) return PayloadReleaseResult::Rejected;
  return cursor.phase_ == 12 ? PayloadReleaseResult::Complete : PayloadReleaseResult::Progress;
}

LOOP_COLD_MEM PendingSnapshotPublishBox::PayloadReleaseResult
PendingSnapshotPublishBox::beginPayloadRelease(PayloadReleaseCursor& cursor, CompanionStoragePolicy policy) {
  if (payloadReleaseCursor_ == &cursor && cursor.owner_ == this) {
    if (cursor.policy_ != policy) return PayloadReleaseResult::Rejected;
    return payloadReleaseResult(cursor);
  }
  if (payloadReleaseCursor_ || cursor.owner_) return PayloadReleaseResult::Rejected;
  cursor.owner_ = this;
  cursor.policy_ = policy;
  payloadReleaseCursor_ = &cursor;
  return PayloadReleaseResult::Progress;
}

LOOP_COLD_MEM PendingSnapshotPublishBox::PayloadReleaseResult
PendingSnapshotPublishBox::stepPayloadRelease(PayloadReleaseCursor& cursor, size_t maximumUnits) {
  if (cursor.owner_ != this || payloadReleaseCursor_ != &cursor) return PayloadReleaseResult::Rejected;
  for (size_t unit = 0; unit < maximumUnits && cursor.phase_ != 12; ++unit) {
    ++cursor.units_;
    bool done = false;
    // Construction admission, child grains and drained shell are independent.
    // Batches/IDs: back value then empty allocation (or explicit retained phase).
    // Journal: nested back rows/edges/buffers/parent, fragment shell, outer buffer.
    switch (cursor.phase_) {
      case 0:
        if (construction && construction->beginRelease(cursor.constructionCursor_) ==
            LoopSnapshotConstruction::ReleaseResult::Rejected) return PayloadReleaseResult::Rejected;
        done = true;
        break;
      case 1:
        done = !construction || construction->stepRelease(cursor.constructionCursor_, 1) ==
            LoopSnapshotConstruction::ReleaseResult::Complete;
        break;
      case 2: construction.reset(); done = true; break; // Drained child only.
      case 3: done = retirePendingValueRowOrBuffer(passChangeProgress.batch); break;
      case 4: done = retirePendingValueRowOrBuffer(fragmentProgress.batch); break;
      case 5: done = retirePendingValueRowOrBuffer(companionIds,
          cursor.policy_ == CompanionStoragePolicy::Retain); break;
      case 6: done = retirePendingValueRowOrBuffer(queuedPassIds); break;
      case 7: done = retirePendingValueRowOrBuffer(appliedPassIds); break;
      // The wrap producer's applied-companion IDs retire behind the journal IDs in the same phase: an empty list costs
      // no grant, a populated one one row per grant.
      case 8: done = retirePendingValueRowOrBuffer(journaledPassIds) &&
                     retirePendingValueRowOrBuffer(appliedCompanionIds); break;
      case 9:
        if (queuedWrapJournals.empty()) {
          decltype(queuedWrapJournals){}.swap(queuedWrapJournals);
          done = true;
        } else {
          auto& fragment = queuedWrapJournals.back();
          bool fragmentDone = false;
          switch (cursor.fragmentPhase_) {
            case 0: fragmentDone = retirePendingChangeRowOrBuffer(fragment.changes.notes); break;
            case 1: fragmentDone = retirePendingChangeRowOrBuffer(fragment.changes.events); break;
            case 2: fragmentDone = LoopSnapshot::releaseInPlaceUnit(
                fragment.parent, cursor.positions_, cursor.rootPhase_); break;
            case 3:
              queuedWrapJournals.pop_back(); // All nested owners empty.
              cursor.fragmentPhase_ = 0;
              cursor.rootPhase_ = 0;
              cursor.positions_.fill(0);
              break;
          }
          if (fragmentDone) ++cursor.fragmentPhase_;
        }
        break;
      case 10: done = LoopSnapshot::releaseInPlaceUnit(
          unpublishedCandidate, cursor.positions_, cursor.rootPhase_); break;
      case 11: done = true; break; // Observational terminal after this transition.
    }
    if (done) {
      ++cursor.phase_;
      cursor.rootPhase_ = 0;
      cursor.positions_.fill(0);
    }
  }
  return payloadReleaseResult(cursor);
}
