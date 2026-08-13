//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "EditSessionAction.h"
#include "EditSessionInteraction.h"
#include "Globals.h"
#include "ResolveConstrainedGeometry.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/NoteUtils.h"
#include "Utils/RuntimeTimingEnvelope.h"

#include <algorithm>

#if defined(PIO_UNIT_TEST_NATIVE)
#include <cstdint>
inline uint32_t micros() { return 0; }
#else
#include <Arduino.h>
#endif

namespace {

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

void Loop::accumulatePendingNoteChangesFromSourceNotes(const NoteUtils::DisplayNoteVec& sourceNotes,
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
  const uint32_t windowLength = (endTick > startTick) ? (endTick - startTick) : 1u;

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
    if (!DisplayWindowUtils::noteIntersectsWindow(note.startTick, note.endTick, startTick,
                                                  windowLength, loopLen)) {
      continue;
    }
    pairs.push_back(CausingTargetPair{causingId, note.noteId});
    baseline[note.noteId] = NoteBaseline{note.note, note.velocity, note.startTick, note.endTick};
  }
  RuntimeTimingEnvelope::addNotePair(micros() - pairStartUs);

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

bool Loop::accumulatePendingNoteChangesForIncomingNote(uint8_t channel, uint8_t pitch,
                                                       uint8_t velocity, uint32_t startTick,
                                                       uint32_t endTick, NoteId incomingNoteId) {
  if (!overdubSourceViewEstablished_ || overdubSourceViewLoopLengthTicks_ == 0) {
    return false;
  }
  if (endTick < startTick) {
    return false;
  }
  accumulatePendingNoteChangesFromSourceNotes(overdubSourceViewNotes_, channel, pitch, velocity,
                                              startTick, endTick, incomingNoteId);
  return true;
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
