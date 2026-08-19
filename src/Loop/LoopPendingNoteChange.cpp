//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "EditSessionAction.h"
#include "EditSessionInteraction.h"
#include "Globals.h"
#include "LoopContentResolution.h"
#include "OverlapCandidateLookup.h"
#include "OverlapNoteIdObservation.h"
#include "ResolveConstrainedGeometry.h"
#include "Utils/IntervalProjection.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/LoopMem.h"
#include "Utils/NoteUtils.h"
#include "Utils/RuntimeTimingTelemetry.h"

#include <algorithm>
#include <cstdio>

#if defined(PIO_UNIT_TEST_NATIVE)
#include <cstdint>
inline uint32_t micros() { return 0; }
#else
#include <Arduino.h>
#endif

namespace {

bool linearSoundingSpan(uint32_t startTick, uint32_t endTick, uint32_t loopLength,
                        uint32_t& linearStart, uint32_t& linearEnd) {
  return OverlapNoteIdObservation::linearSoundingSpan(startTick, endTick, loopLength, linearStart,
                                                      linearEnd);
}

bool displayNotePresentAtHold(uint32_t startTick, uint32_t endTick, uint32_t holdStart,
                               uint32_t loopLength) {
  return OverlapNoteIdObservation::displayNotePresentAtHold(startTick, endTick, holdStart,
                                                            loopLength);
}

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

void unionSelectedNote(NoteUtils::DisplayNoteVec& selected, const NoteUtils::DisplayNote& note) {
  if (note.noteId == kInvalidNoteId) {
    return;
  }
  for (const NoteUtils::DisplayNote& picked : selected) {
    if (picked.noteId == note.noteId) {
      return;
    }
  }
  selected.push_back(note);
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

void applyPendingHideAndShortenToNotes(NoteUtils::DisplayNoteVec& notes,
                                       const PendingNoteChangeVec& pending) {
  for (const PendingNoteChange& change : pending) {
    if (change.kind != PendingNoteChangeKind::Shorten &&
        change.kind != PendingNoteChangeKind::Hide) {
      continue;
    }
    for (auto it = notes.begin(); it != notes.end();) {
      if (it->noteId != change.noteId) {
        ++it;
        continue;
      }
      if (change.kind == PendingNoteChangeKind::Hide) {
        it = notes.erase(it);
        continue;
      }
      it->startTick = change.startTick;
      it->endTick = change.endTick;
      ++it;
    }
  }
}

}  // namespace

LOOP_COLD_MEM void Loop::ensureOverdubSourceNotesForHold(uint32_t holdPhaseTick, uint8_t pitch,
                                                        NoteUtils::DisplayNoteVec* newlyMergedPitchNotes,
                                                        bool presentAtHoldOnly) {
  if (newlyMergedPitchNotes != nullptr) {
    newlyMergedPitchNotes->clear();
  }
  if (!overdubSourceViewEstablished_ || overdubSourceViewLoopLengthTicks_ == 0) {
    return;
  }
  const uint32_t loopLen = overdubSourceViewLoopLengthTicks_;
  uint32_t windowStart = 0;
  uint32_t windowLength = 0;
  resolveOverdubSourceWindow(holdPhaseTick, windowStart, windowLength);

  SessionMidiEventVec windowEvents;
  ResolutionCostCounters windowCounters;
  LoopContentResolution::resolveWindow(passes, loopLen, windowStart, windowLength, windowEvents,
                                       &windowCounters);
  if (windowEvents.empty()) {
    return;
  }
  const NoteUtils::DisplayNoteVec windowNotes =
      NoteUtils::reconstructDisplayNotes(windowEvents, loopLen, false);
  NoteUtils::DisplayNoteVec toMerge;
  for (const NoteUtils::DisplayNote& note : windowNotes) {
    if (note.note != pitch || note.noteId == kInvalidNoteId) {
      continue;
    }
    if (presentAtHoldOnly &&
        !displayNotePresentAtHold(note.startTick, note.endTick, holdPhaseTick, loopLen)) {
      continue;
    }
    if (!committedPlaybackNoteOnIdentityValid(note.noteId)) {
      continue;
    }
    bool already = false;
    for (const NoteUtils::DisplayNote& existing : overdubSourceViewNotes_) {
      if (existing.noteId == note.noteId) {
        already = true;
        break;
      }
    }
    if (!already) {
      toMerge.push_back(note);
    }
  }
  mergeDisplayNotesIntoOverdubSourceView(toMerge);
  if (newlyMergedPitchNotes != nullptr) {
    *newlyMergedPitchNotes = toMerge;
  }
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
  char line[224];
  snprintf(line, sizeof(line),
           "#CAP,%lu,DIAG,lcr,src,why=hold,from=win,pitch=%u,win=%lu,ev=%u,merged=%u,notes=%u,bars=%u",
           static_cast<unsigned long>(micros()), static_cast<unsigned>(pitch),
           static_cast<unsigned long>(windowCounters.elapsedMicros),
           static_cast<unsigned>(windowEvents.size()), static_cast<unsigned>(toMerge.size()),
           static_cast<unsigned>(overdubSourceViewNotes_.size()),
           static_cast<unsigned>(kOverdubSourceWindowBars));
  DebugSessionCapture::appendCaptureTextLine(line);
#else
  (void)windowCounters;
#endif
}

LOOP_COLD_MEM void Loop::collectOverdubSourceHoldParticipantIds(uint32_t holdPhaseTick, uint8_t pitch,
                                                               OverlapNoteIdSet& out) const {
  out.clear();
  const uint32_t loopLen = overdubSourceViewLoopLengthTicks_;
  if (!overdubSourceViewEstablished_ || loopLen == 0) {
    return;
  }
  for (const NoteUtils::DisplayNote& note : overdubSourceViewNotes_) {
    if (note.note != pitch || note.noteId == kInvalidNoteId) {
      continue;
    }
    if (!displayNotePresentAtHold(note.startTick, note.endTick, holdPhaseTick, loopLen)) {
      continue;
    }
    if (!committedPlaybackNoteOnIdentityValid(note.noteId)) {
      continue;
    }
    (void)out.insert(note.noteId);
  }
}

LOOP_COLD_MEM bool Loop::tryCollectPreparedPresentNoteIdsAtTick(uint32_t tick, uint8_t pitch,
                                                               OverlapNoteIdSet& out) const {
  if (!LoopContentResolution::tryCollectPreparedPresentNoteIdsAtTick(
          tick, pitch, playbackRevision, loopLengthTicks, out)) {
    return false;
  }
  if (committedPlaybackMergedForIdentity_ == nullptr) {
    return true;
  }
  OverlapNoteIdSet filtered;
  for (size_t i = 0; i < out.size(); ++i) {
    const NoteId id = out.at(i);
    if (committedPlaybackNoteOnIdentityValid(id)) {
      (void)filtered.insert(id);
    }
  }
  out = filtered;
  return true;
}

LOOP_COLD_MEM void Loop::collectOverdubNoteOnParticipantIds(uint8_t pitch, uint8_t channel,
                                                            const ActiveNoteLedger& ledger,
                                                            OverlapNoteIdSet& out) const {
  out.clear();
  ledger.forEachActive([&](uint8_t entryChannel, uint8_t entryNote,
                           const ActiveNoteLedger::Entry& entry) {
    if (entryChannel != channel || entryNote != pitch) {
      return;
    }
    (void)out.insert(entry.noteId);
  });
}

LOOP_COLD_MEM void Loop::accumulatePendingNoteChangesFromSourceNotes(
    const NoteUtils::DisplayNoteVec& sourceNotes,
                                                       uint8_t channel, uint8_t pitch,
                                                       uint8_t velocity, uint32_t startTick,
                                                       uint32_t endTick, NoteId incomingNoteId) {
  const NoteId causingId =
      (incomingNoteId != kInvalidNoteId) ? incomingNoteId : allocateNoteId();

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
}

LOOP_COLD_MEM bool Loop::accumulatePendingNoteChangesForIncomingNote(
    uint8_t channel, uint8_t pitch, uint8_t velocity, uint32_t startTick, uint32_t endTick,
    NoteId incomingNoteId, const OverlapNoteIdSet& overlapNoteIds) {
  if (!overdubSourceViewEstablished_ || overdubSourceViewLoopLengthTicks_ == 0) {
    return false;
  }
  const uint32_t loopLen = overdubSourceViewLoopLengthTicks_;
  if (endTick == startTick || loopLen == 0) {
    return false;
  }

  const bool wrapCrossing = endTick < startTick;
  if (!wrapCrossing && startTick >= endTick) {
    return false;
  }
  if (wrapCrossing && startTick >= loopLen) {
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

  auto collectConsumeWindow = [&](uint32_t windowStart, uint32_t windowEnd) {
    if (windowStart >= windowEnd) {
      return;
    }
    // Phase 4: skip 16-bar resolveWindow when enter/wrap already gathered the
    // whole loop. Empty occupy still walks source-view notes (122848 hide).
    // Loops longer than the source window still JIT-merge ahead notes.
    if (loopLen > overdubSourceWindowLengthTicks()) {
      NoteUtils::DisplayNoteVec jitHoldPitchNotes;
      ensureOverdubSourceNotesForHold(windowStart, pitch, &jitHoldPitchNotes);
      for (const NoteUtils::DisplayNote& note : jitHoldPitchNotes) {
        if (!existingNoteOverlapsIncomingHold(note.startTick, note.endTick, windowStart, windowEnd,
                                              loopLen)) {
          continue;
        }
        unionSelectedNote(selected, note);
      }
    }
    uint32_t sourceWindowStart = 0;
    uint32_t sourceWindowLength = 0;
    resolveOverdubSourceWindow(windowStart, sourceWindowStart, sourceWindowLength);
    for (const NoteUtils::DisplayNote& note : overdubSourceViewNotes_) {
      if (note.note != pitch || note.noteId == kInvalidNoteId) {
        continue;
      }
      if (!DisplayWindowUtils::noteIntersectsWindow(note.startTick, note.endTick, sourceWindowStart,
                                                    sourceWindowLength, loopLen)) {
        continue;
      }
      if (!existingNoteOverlapsIncomingHold(note.startTick, note.endTick, windowStart, windowEnd,
                                            loopLen)) {
        continue;
      }
      unionSelectedNote(selected, note);
    }
  };

  if (wrapCrossing) {
    collectConsumeWindow(startTick, loopLen);
    collectConsumeWindow(0, endTick);
  } else {
    collectConsumeWindow(startTick, endTick);
  }

  const NoteId causingId =
      (incomingNoteId != kInvalidNoteId) ? incomingNoteId : allocateNoteId();
  if (wrapCrossing) {
    accumulatePendingNoteChangesFromSourceNotes(selected, channel, pitch, velocity, startTick,
                                                loopLen, causingId);
    if (endTick > 0) {
      accumulatePendingNoteChangesFromSourceNotes(selected, channel, pitch, velocity, 0, endTick,
                                                  causingId);
    }
  } else {
    accumulatePendingNoteChangesFromSourceNotes(selected, channel, pitch, velocity, startTick,
                                                endTick, causingId);
  }

  PendingNoteChange addChange{};
  addChange.kind = PendingNoteChangeKind::Add;
  addChange.noteId = causingId;
  addChange.channel = channel;
  addChange.pitch = pitch;
  addChange.velocity = velocity;
  addChange.startTick = startTick;
  addChange.endTick = endTick;
  pendingNoteChanges_.push_back(addChange);

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

LOOP_COLD_MEM void Loop::applyPendingNoteChangesToOverdubSourceView() {
  if (!overdubSourceViewEstablished_) {
    return;
  }
  for (const PendingNoteChange& change : pendingNoteChanges_) {
    if (change.kind == PendingNoteChangeKind::Add) {
      if (change.noteId == kInvalidNoteId) {
        continue;
      }
      NoteUtils::DisplayNoteVec added;
      NoteUtils::DisplayNote note{};
      note.noteId = change.noteId;
      note.note = change.pitch;
      note.velocity = change.velocity;
      note.startTick = change.startTick;
      note.endTick = change.endTick;
      added.push_back(note);
      mergeDisplayNotesIntoOverdubSourceView(added);
    }
  }
  applyPendingHideAndShortenToNotes(overdubSourceViewNotes_, pendingNoteChanges_);
}

LOOP_COLD_MEM void Loop::applyPendingNoteChangesToDisplayNotes(NoteUtils::DisplayNoteVec& notes) const {
  applyPendingHideAndShortenToNotes(notes, pendingNoteChanges_);
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
