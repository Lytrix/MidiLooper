//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "EditSessionAction.h"
#include "EditSessionInteraction.h"
#include "Globals.h"
#include "OverlapCandidateLookup.h"
#include "ResolveConstrainedGeometry.h"
#include "Utils/IntervalProjection.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/LoopMem.h"
#include "Utils/NoteUtils.h"
#include "Utils/RuntimeTimingTelemetry.h"

#include <algorithm>

#if defined(PIO_UNIT_TEST_NATIVE)
#include <cstdint>
inline uint32_t micros() { return 0; }
#else
#include <Arduino.h>
#endif

namespace {

bool linearSoundingSpan(uint32_t startTick, uint32_t endTick, uint32_t loopLength,
                        uint32_t& linearStart, uint32_t& linearEnd) {
  if (loopLength == 0) {
    return false;
  }
  linearStart = IntervalProjection::tickPhaseInLoop(startTick, 0, loopLength);
  linearEnd = IntervalProjection::tickPhaseInLoop(endTick, 0, loopLength);
  if (linearEnd == linearStart) {
    return false;
  }
  if (linearEnd < linearStart) {
    linearEnd += loopLength;
  }
  return linearStart < linearEnd;
}

/// Same rule as OverlapNoteIdObservation::existingNoteOverlapsIncomingHold.
/// Body stays in this TU — do not include the observation header (ITCM).
bool existingNoteOverlapsIncomingHold(uint32_t existingStart, uint32_t existingEnd,
                                      uint32_t incomingStart, uint32_t incomingEnd,
                                      uint32_t loopLength) {
  uint32_t existingLinearStart = 0;
  uint32_t existingLinearEnd = 0;
  uint32_t incomingLinearStart = 0;
  uint32_t incomingLinearEnd = 0;
  if (!linearSoundingSpan(existingStart, existingEnd, loopLength, existingLinearStart,
                          existingLinearEnd)) {
    return false;
  }
  if (!linearSoundingSpan(incomingStart, incomingEnd, loopLength, incomingLinearStart,
                          incomingLinearEnd)) {
    return false;
  }
  const bool direct =
      existingLinearStart < incomingLinearEnd && existingLinearEnd > incomingLinearStart;
  const bool existingShifted = existingLinearStart + loopLength < incomingLinearEnd &&
                               existingLinearEnd + loopLength > incomingLinearStart;
  const bool incomingShifted = existingLinearStart < incomingLinearEnd + loopLength &&
                               existingLinearEnd > incomingLinearStart + loopLength;
  return direct || existingShifted || incomingShifted;
}

void upsertSourceTransform(PendingNoteChangeVec& pending, const PendingNoteChange& change) {
  if (change.kind != PendingNoteChangeKind::Shorten && change.kind != PendingNoteChangeKind::Hide) {
    pending.push_back(change);
    return;
  }
  for (PendingNoteChange& existing : pending) {
    if ((existing.kind == PendingNoteChangeKind::Shorten ||
         existing.kind == PendingNoteChangeKind::Hide) &&
        existing.noteId == change.noteId) {
      existing = change;
      return;
    }
  }
  pending.push_back(change);
}

}  // namespace

LOOP_COLD_MEM void Loop::accumulatePendingNoteChangesFromSourceNotes(
    const NoteUtils::DisplayNoteVec& sourceNotes,
                                                       uint8_t channel, uint8_t pitch,
                                                       uint8_t velocity, uint32_t startTick,
                                                       uint32_t endTick, NoteId incomingNoteId) {
  const NoteId causingId =
      (incomingNoteId != kInvalidNoteId) ? incomingNoteId : allocateNoteId();

  PendingNoteChange addChange{};
  addChange.kind = PendingNoteChangeKind::Add;
  addChange.noteId = causingId;
  addChange.channel = channel;
  addChange.pitch = pitch;
  addChange.velocity = velocity;
  addChange.startTick = startTick;
  addChange.endTick = endTick;

  const uint32_t loopLen = overdubSourceViewLoopLengthTicks_ != 0 ? overdubSourceViewLoopLengthTicks_
                                                                 : loopLengthTicks;

  BaselineMap baseline;
  EditedGeometry edited{};
  EditedNoteSpan causingSpan{};
  causingSpan.noteId = causingId;
  causingSpan.span = NoteBaseline{pitch, velocity, startTick, endTick};
  edited.causingSpans.push_back(causingSpan);

  std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>> pairs;
  const uint32_t pairStartUs = micros();
  for (const NoteUtils::DisplayNote& note : sourceNotes) {
    if (note.note != pitch || note.noteId == kInvalidNoteId || note.noteId == causingId) {
      continue;
    }
    if (!existingNoteOverlapsIncomingHold(note.startTick, note.endTick, startTick, endTick,
                                          loopLen)) {
      continue;
    }
    pairs.push_back(CausingTargetPair{causingId, note.noteId});
    baseline[note.noteId] = NoteBaseline{note.note, note.velocity, note.startTick, note.endTick};
  }
  RuntimeTimingTelemetry::addNotePair(micros() - pairStartUs);

  if (!pairs.empty()) {
    const auto interactions = analyzeEditSessionInteractions(pairs, edited, baseline);
    const EditSessionInteractionsByTarget grouped =
        groupEditSessionInteractionsByTarget(interactions);
    for (const TargetNoteInteractionGroup& group : grouped.groups) {
      const auto baselineIt = baseline.find(group.targetNoteId);
      if (baselineIt == baseline.end()) {
        continue;
      }
      const NoteBaseline& sourceBaseline = baselineIt->second;
      // Shared NoteMinLength globals (same as NOTE_EDIT) — no overdub-specific floor.
      const ConstrainedNoteGeometry geometry = resolveConstrainedGeometry(
          group.targetNoteId, sourceBaseline, group.incoming, loopLen, noteMinLengthTicks,
          noteMinLengthRemoveEnabled);

      PendingNoteChange transform{};
      transform.noteId = group.targetNoteId;
      transform.channel = channel;
      transform.pitch = sourceBaseline.pitch;
      transform.velocity = sourceBaseline.velocity;
      transform.startTick = geometry.startTick;
      transform.endTick = geometry.endTick;

      if (!geometry.visible) {
        transform.kind = PendingNoteChangeKind::Hide;
        transform.startTick = sourceBaseline.startTick;
        transform.endTick = sourceBaseline.endTick;
        upsertSourceTransform(pendingNoteChanges_, transform);
        continue;
      }
      if (geometry.startTick != sourceBaseline.startTick ||
          geometry.endTick != sourceBaseline.endTick) {
        transform.kind = PendingNoteChangeKind::Shorten;
        upsertSourceTransform(pendingNoteChanges_, transform);
      }
    }
  }

  pendingNoteChanges_.push_back(addChange);
}

LOOP_COLD_MEM bool Loop::accumulatePendingNoteChangesForIncomingNote(
    uint8_t channel, uint8_t pitch, uint8_t velocity, uint32_t startTick, uint32_t endTick,
    NoteId incomingNoteId, const OverlapNoteIdSet& overlapNoteIds) {
  if (!overdubSourceViewEstablished_ || overdubSourceViewLoopLengthTicks_ == 0) {
    return false;
  }
  if (endTick < startTick) {
    return false;
  }
  ++overlapHoldTotals_.noteOffs;
  const uint32_t idCount = static_cast<uint32_t>(overlapNoteIds.size());
  if (idCount > overlapHoldTotals_.maxIds) {
    overlapHoldTotals_.maxIds = idCount;
  }
  if (overlapNoteIds.overflowed()) {
    ++overlapHoldTotals_.overflows;
  }
  NoteUtils::DisplayNoteVec selected;
  if (OverlapCandidateLookup::shouldLookupSpans(overlapNoteIds)) {
    size_t notesExamined = 0;
    const uint32_t lookupStartUs = micros();
    OverlapCandidateLookup::appendNotesForIds(overdubSourceViewNotes_, overlapNoteIds, selected,
                                              &notesExamined);
    const uint32_t lookupUs = micros() - lookupStartUs;
    ++overlapHoldTotals_.lookedUp;
    const uint32_t examined = static_cast<uint32_t>(notesExamined);
    overlapHoldTotals_.sumExamined += examined;
    if (examined > overlapHoldTotals_.maxExamined) {
      overlapHoldTotals_.maxExamined = examined;
    }
    overlapHoldTotals_.sumLookupUs += lookupUs;
    if (lookupUs > overlapHoldTotals_.maxLookupUs) {
      overlapHoldTotals_.maxLookupUs = lookupUs;
    }
  } else {
    ++overlapHoldTotals_.emptySets;
  }
  accumulatePendingNoteChangesFromSourceNotes(selected, channel, pitch, velocity, startTick,
                                              endTick, incomingNoteId);
  overlapHoldTotals_.add = 0;
  overlapHoldTotals_.shorten = 0;
  overlapHoldTotals_.hide = 0;
  for (const PendingNoteChange& change : pendingNoteChanges_) {
    if (change.kind == PendingNoteChangeKind::Add) {
      ++overlapHoldTotals_.add;
    } else if (change.kind == PendingNoteChangeKind::Shorten) {
      ++overlapHoldTotals_.shorten;
    } else if (change.kind == PendingNoteChangeKind::Hide) {
      ++overlapHoldTotals_.hide;
    }
  }
  return true;
}

LOOP_COLD_MEM __attribute__((noinline)) void Loop::emitOverlapHoldTotals() const {
  const OverlapHoldTotals& totals = overlapHoldTotals_;
  SC_OVERLAP_HOLD(totals.noteOffs, totals.emptySets, totals.maxIds, totals.overflows,
                  totals.lookedUp, totals.maxExamined, totals.sumExamined, totals.maxLookupUs,
                  totals.sumLookupUs, totals.add, totals.shorten, totals.hide);
}

EditPassIdList Loop::sealPendingNoteChangesToEditPasses() {
  EditPassIdList sealedIds;
  for (const PendingNoteChange& change : pendingNoteChanges_) {
    if (change.kind != PendingNoteChangeKind::Shorten &&
        change.kind != PendingNoteChangeKind::Hide) {
      continue;
    }
    EditPass row{};
    row.passType = EditPassType::Note;
    row.targetNoteId = change.noteId;
    row.pitch = change.pitch;
    row.velocity = change.velocity;
    row.startTick = change.startTick;
    row.endTick = change.endTick;
    if (change.kind == PendingNoteChangeKind::Hide) {
      row.actionType = EditActionType::Delete;
      row.propertyType = EditPropertyType::None;
    } else {
      row.actionType = EditActionType::Update;
      row.propertyType = EditPropertyType::Length;
    }
    const EditPassId id =
        saveNoteEditPass(kOverdubCompanionEditPassIndex, std::move(row), EditPassType::Note);
    if (id != kInvalidEditPassId) {
      sealedIds.push_back(id);
    }
  }
  clearPendingNoteChanges();
  return sealedIds;
}
