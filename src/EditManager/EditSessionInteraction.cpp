//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditSessionInteraction.h"

#include <algorithm>

#include "EditSessionLiveStoreSpan.h"
#include "NoteEditCurrentState.h"
#include "ParticipatingNoteSession.h"
#include "Utils/NoteEditMem.h"

namespace {

NOTE_EDIT_MEM const NoteBaseline* findCausingSpan(NoteId noteId, const EditedGeometry& editedGeometry) {
  for (const EditedNoteSpan& entry : editedGeometry.causingSpans) {
    if (entry.noteId == noteId) {
      return &entry.span;
    }
  }
  return nullptr;
}

NOTE_EDIT_MEM const NoteBaseline* findBaselineSpan(NoteId noteId, const BaselineMap& transactionBaseline) {
  const auto it = transactionBaseline.find(noteId);
  return it == transactionBaseline.end() ? nullptr : &it->second;
}

NOTE_EDIT_MEM bool baselineSpansEqual(const NoteBaseline& left, const NoteBaseline& right) {
  return left.pitch == right.pitch && left.velocity == right.velocity &&
         left.startTick == right.startTick && left.endTick == right.endTick;
}

NOTE_EDIT_MEM bool causingTargetPairBefore(const CausingTargetPair& left,
                                           const CausingTargetPair& right) {
  if (left.causingNoteId != right.causingNoteId) {
    return left.causingNoteId < right.causingNoteId;
  }
  return left.targetNoteId < right.targetNoteId;
}

NOTE_EDIT_MEM void sortCausingTargetPairs(
    std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>>& pairs) {
  for (size_t i = 1; i < pairs.size(); ++i) {
    const CausingTargetPair key = pairs[i];
    size_t j = i;
    while (j > 0 && causingTargetPairBefore(key, pairs[j - 1])) {
      pairs[j] = pairs[j - 1];
      --j;
    }
    pairs[j] = key;
  }
}

NOTE_EDIT_MEM bool editSessionInteractionBefore(const EditSessionInteraction& left,
                                                const EditSessionInteraction& right) {
  return left.causingNoteId < right.causingNoteId;
}

NOTE_EDIT_MEM void sortIncomingInteractions(
    std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>&
        incoming) {
  for (size_t i = 1; i < incoming.size(); ++i) {
    const EditSessionInteraction key = incoming[i];
    size_t j = i;
    while (j > 0 && editSessionInteractionBefore(key, incoming[j - 1])) {
      incoming[j] = incoming[j - 1];
      --j;
    }
    incoming[j] = key;
  }
}

NOTE_EDIT_MEM bool targetInteractionGroupBefore(const TargetNoteInteractionGroup& left,
                                                const TargetNoteInteractionGroup& right) {
  return left.targetNoteId < right.targetNoteId;
}

NOTE_EDIT_MEM void sortTargetInteractionGroups(
    std::vector<TargetNoteInteractionGroup,
                 InternalHeapFirstAllocator<TargetNoteInteractionGroup>>& groups) {
  for (size_t i = 1; i < groups.size(); ++i) {
    const TargetNoteInteractionGroup key = groups[i];
    size_t j = i;
    while (j > 0 && targetInteractionGroupBefore(key, groups[j - 1])) {
      groups[j] = groups[j - 1];
      --j;
    }
    groups[j] = key;
  }
}

}  // namespace

NOTE_EDIT_MEM bool isSelectedNote(NoteId noteId, const EditorSelection& selection) {
  if (noteId == kInvalidNoteId) {
    return false;
  }
  for (NoteId selectedId : selection.selectedNotes) {
    if (selectedId == noteId) {
      return true;
    }
  }
  return false;
}

NOTE_EDIT_MEM bool isIntraSelectionPair(NoteId causingNoteId, NoteId targetNoteId,
                            const EditorSelection& selection) {
  return isSelectedNote(causingNoteId, selection) && isSelectedNote(targetNoteId, selection);
}

NOTE_EDIT_MEM bool geometryChangedThisTick(NoteId causingNoteId, const NoteBaseline& priorLatch,
                             const NoteBaseline& currentSpan) {
  (void)causingNoteId;
  return !baselineSpansEqual(priorLatch, currentSpan);
}

NOTE_EDIT_MEM std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> determineChangedCausingNotes(
    const EditorSelection& selection, const EditedGeometry& editedGeometry,
    const std::unordered_map<NoteId, NoteBaseline, NoteIdHash>& priorLatchByNoteId) {
  std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> changed;
  for (const EditedNoteSpan& entry : editedGeometry.causingSpans) {
    if (!isSelectedNote(entry.noteId, selection)) {
      continue;
    }
    const auto priorIt = priorLatchByNoteId.find(entry.noteId);
    if (priorIt == priorLatchByNoteId.end() ||
        geometryChangedThisTick(entry.noteId, priorIt->second, entry.span)) {
      changed.push_back(entry.noteId);
    }
  }
  sortNoteIdVector(changed);
  return changed;
}

NOTE_EDIT_MEM NoteIdList collectEvaluationScopeNoteIds(const BaselineMap& transactionBaseline,
                                                      const MidiEventVec& liveStore,
                                                      const NoteIdList& changedOverlapNoteIds,
                                                      NoteId movingNoteId,
                                                      std::optional<uint8_t> overlapPitchLane,
                                                      const NoteEditCurrentState* currentState) {
  NoteIdList scope;
  const auto inLane = [&](uint8_t pitch) {
    return !overlapPitchLane.has_value() || pitch == overlapPitchLane.value();
  };
  const auto isSticky = [&](NoteId noteId) {
    return std::find(changedOverlapNoteIds.begin(), changedOverlapNoteIds.end(), noteId) !=
           changedOverlapNoteIds.end();
  };
  const auto addToScope = [&](NoteId noteId, uint8_t pitch) {
    if (noteId == kInvalidNoteId || noteId == movingNoteId) {
      return;
    }
    if (!inLane(pitch) && !isSticky(noteId)) {
      return;
    }
    if (std::find(scope.begin(), scope.end(), noteId) == scope.end()) {
      scope.push_back(noteId);
    }
  };

  for (const auto& [noteId, baseline] : transactionBaseline) {
    addToScope(noteId, baseline.pitch);
  }
  if (currentState != nullptr) {
    for (const auto& [noteId, row] : currentState->rows()) {
      addToScope(noteId, row.currentSpan.pitch);
    }
  } else {
    // Scope membership is NoteId + pitch lane only. The track's output channel is not an identity
    // key: materialized record/overdub passes carry the channel played at record time, so gating on
    // it hid every same-pitch overlap from analyze (session_20260805_030517: candidates=0).
    for (const MidiEvent& evt : liveStore) {
      if (!evt.isNoteOn() || evt.data.noteData.velocity == 0) {
        continue;
      }
      addToScope(evt.noteId, evt.data.noteData.note);
    }
  }
  sortNoteIdVector(scope);
  return scope;
}

NOTE_EDIT_MEM BaselineMap projectTransactionBaselineForEvaluationScope(
    const EditorSelection& selection, const BaselineMap& transactionBaseline,
    const NoteIdList& evaluationScope, NoteId movingNoteId, uint32_t loopLength,
    int32_t originTick) {
  BaselineMap projected;
  for (const auto& [noteId, baseline] : transactionBaseline) {
    if (noteId != movingNoteId &&
        std::find(evaluationScope.begin(), evaluationScope.end(), noteId) ==
            evaluationScope.end()) {
      continue;
    }
    projected[noteId] =
        projectNoteBaselineForEditAnalysis(selection, baseline, noteId, loopLength, originTick);
  }
  return projected;
}

NOTE_EDIT_MEM void ensureBaselineMapEntriesForEvaluationScope(NoteEditFocus& focus,
                                                              const NoteIdList& evaluationScope,
                                                              const MidiEventVec& liveStore,
                                                              uint8_t channel,
                                                              const NoteEditCurrentState* currentState) {
  if (!focus.active) {
    return;
  }
  const auto ensureNoteId = [&](NoteId noteId) {
    if (noteId == kInvalidNoteId) {
      return;
    }
    if (focus.baselineMap.find(noteId) != focus.baselineMap.end()) {
      return;
    }
    NoteBaseline span{};
    if (currentState != nullptr && currentState->readCurrentSpan(noteId, span)) {
      focus.baselineMap[noteId] = span;
      return;
    }
    if (readLiveLinearSpan(liveStore, noteId, channel, span)) {
      focus.baselineMap[noteId] = span;
    }
  };
  for (NoteId noteId : evaluationScope) {
    ensureNoteId(noteId);
  }
  if (focus.movingNoteId != kInvalidNoteId) {
    ensureNoteId(focus.movingNoteId);
  }
}

NOTE_EDIT_MEM std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>>
determineEligiblePairs(const EditorSelection& selection,
                       const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>>&
                           changedCausingNoteIds,
                       const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>>&
                           candidateTargetNoteIds) {
  std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>> pairs;
  for (NoteId causingNoteId : changedCausingNoteIds) {
    if (!isSelectedNote(causingNoteId, selection)) {
      continue;
    }
    for (NoteId targetNoteId : candidateTargetNoteIds) {
      if (targetNoteId == kInvalidNoteId || causingNoteId == targetNoteId) {
        continue;
      }
      if (isIntraSelectionPair(causingNoteId, targetNoteId, selection)) {
        // Co-moving selected siblings are not overlap targets; stationary selected notes still are
        // (session_20260807_111955: coarse fader moves focus.last only).
        const bool targetCoMoving =
            std::find(changedCausingNoteIds.begin(), changedCausingNoteIds.end(),
                      targetNoteId) != changedCausingNoteIds.end();
        if (targetCoMoving) {
          continue;
        }
      }
      pairs.push_back(CausingTargetPair{causingNoteId, targetNoteId});
    }
  }
  sortCausingTargetPairs(pairs);
  return pairs;
}

NOTE_EDIT_MEM bool linearSpansOverlapForAnalysis(uint32_t causingStart, uint32_t causingEnd,
                                   uint32_t targetStart, uint32_t targetEnd) {
  // Inclusive on both edges:
  // - causingEnd == targetStart → OverlapNoteOn (session_20260804_223208 packed end|start Hide)
  // - causingStart == targetEnd → OverlapNoteOff (session_20260804_225119: start-abut must keep
  //   shorten-to-causingStart-1; BoundaryTouch+full Restore jumped +2 ticks on a 1-tick leave)
  return causingStart <= targetEnd && targetStart <= causingEnd;
}

NOTE_EDIT_MEM bool isBoundaryTouchForAnalysis(uint32_t causingStart, uint32_t causingEnd, uint32_t targetStart,
                                uint32_t targetEnd) {
  if (linearSpansOverlapForAnalysis(causingStart, causingEnd, targetStart, targetEnd)) {
    return false;
  }
  // Linear adjacent notes are inclusive-overlap above. BoundaryTouch remains for non-overlapping
  // pairs that still share an edge after projection/wrap edge cases.
  return causingStart == targetEnd || causingEnd == targetStart;
}

NOTE_EDIT_MEM InteractionType classifyEditSessionInteraction(uint32_t causingStart, uint32_t causingEnd,
                                               uint32_t targetStart, uint32_t targetEnd) {
  if (!linearSpansOverlapForAnalysis(causingStart, causingEnd, targetStart, targetEnd)) {
    return InteractionType::BoundaryTouch;
  }

  if (targetStart >= causingStart && targetEnd <= causingEnd) {
    return InteractionType::CompleteCover;
  }

  if (targetStart >= causingStart) {
    return InteractionType::OverlapNoteOn;
  }

  // Left neighbor: includes start-abut (targetEnd == causingStart) so leave stays 1 tick/step
  // via Shorten to causingStart-1 instead of BoundaryTouch Restore to full baseline.
  if (targetStart < causingStart && targetEnd >= causingStart) {
    return InteractionType::OverlapNoteOff;
  }

  return InteractionType::OverlapNoteOff;
}

NOTE_EDIT_MEM BaselineMap overlayAnalysisBaselineForSessionMovedOverlaps(
    const BaselineMap& storageBaseline, NoteId movingNoteId, const MidiEventVec& liveStore,
    uint8_t channel, uint32_t loopLength, const NoteEditCurrentState* currentState) {
  BaselineMap analysis = storageBaseline;
  for (const auto& [noteId, baseline] : storageBaseline) {
    if (noteId == kInvalidNoteId || noteId == movingNoteId) {
      continue;
    }
    if (currentState != nullptr) {
      NoteBaseline current{};
      if (!currentState->readCurrentSpan(noteId, current)) {
        continue;
      }
      const NoteEditCurrentNoteState* row = currentState->find(noteId);
      if (row != nullptr) {
        const ParticipatingNoteState participant = buildParticipatingNoteState(*row);
        if (participatingNoteQualifiesForLeaveRestoreTarget(participant, movingNoteId)) {
          // Classify hide/shorten against committed overlap geometry while overlap remains
          // classified — shortened/hidden stubs must not turn L→R advance into BoundaryTouch-only
          // (session_20260807_181859: note 9 stub end 2207 vs mover start 2256).
          analysis[noteId] = row->committedSpan;
          continue;
        }
      }
      if (current.startTick != baseline.startTick || current.endTick != baseline.endTick ||
          current.pitch != baseline.pitch) {
        analysis[noteId] = current;
      }
      continue;
    }
    NoteBaseline live{};
    if (!readLiveLinearSpan(liveStore, noteId, channel, live)) {
      continue;
    }
    if (live.startTick != baseline.startTick && live.endTick == baseline.endTick &&
        live.startTick > baseline.startTick) {
      continue;
    }
    if (live.startTick != baseline.startTick || live.endTick != baseline.endTick ||
        live.pitch != baseline.pitch) {
      analysis[noteId] = live;
    }
  }
  return analysis;
}

NOTE_EDIT_MEM std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>
analyzeEditSessionInteractions(
    const std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>>&
        eligiblePairs,
    const EditedGeometry& editedGeometry, const BaselineMap& transactionBaseline) {
  std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>> out;
  for (const CausingTargetPair& pair : eligiblePairs) {
    const NoteBaseline* causingSpan = findCausingSpan(pair.causingNoteId, editedGeometry);
    const NoteBaseline* targetBaseline = findBaselineSpan(pair.targetNoteId, transactionBaseline);
    if (causingSpan == nullptr || targetBaseline == nullptr) {
      continue;
    }

    // D21 / Q14: Hide/Shorten only on the mover's current pitch lane. Edited causing span
    // pitch makes the lane follow pitch changes; cross-pitch time overlap is omitted.
    if (targetBaseline->pitch != causingSpan->pitch) {
      continue;
    }

    const uint32_t causingStart = causingSpan->startTick;
    const uint32_t causingEnd = causingSpan->endTick;
    const uint32_t targetStart = targetBaseline->startTick;
    const uint32_t targetEnd = targetBaseline->endTick;

    if (!linearSpansOverlapForAnalysis(causingStart, causingEnd, targetStart, targetEnd) &&
        !isBoundaryTouchForAnalysis(causingStart, causingEnd, targetStart, targetEnd)) {
      continue;
    }

    EditSessionInteraction interaction{};
    interaction.type =
        classifyEditSessionInteraction(causingStart, causingEnd, targetStart, targetEnd);
    interaction.causingNoteId = pair.causingNoteId;
    interaction.targetNoteId = pair.targetNoteId;
    interaction.causingSpan = *causingSpan;
    interaction.baselineSpan = *targetBaseline;
    out.push_back(interaction);
  }
  return out;
}

NOTE_EDIT_MEM EditSessionInteractionsByTarget groupEditSessionInteractionsByTarget(
    const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>&
        interactions) {
  EditSessionInteractionsByTarget grouped;
  for (const EditSessionInteraction& interaction : interactions) {
    auto groupIt = std::find_if(
        grouped.groups.begin(), grouped.groups.end(),
        [&](const TargetNoteInteractionGroup& group) {
          return group.targetNoteId == interaction.targetNoteId;
        });
    if (groupIt == grouped.groups.end()) {
      TargetNoteInteractionGroup group{};
      group.targetNoteId = interaction.targetNoteId;
      group.incoming.push_back(interaction);
      grouped.groups.push_back(group);
    } else {
      groupIt->incoming.push_back(interaction);
    }
  }

  for (TargetNoteInteractionGroup& group : grouped.groups) {
    sortIncomingInteractions(group.incoming);
  }

  sortTargetInteractionGroups(grouped.groups);
  return grouped;
}

NOTE_EDIT_MEM uint32_t computeShortenedEndTick(const EditSessionInteraction& interaction, uint32_t loopLength) {
  (void)interaction;
  const uint32_t causingStart = interaction.causingSpan.startTick;
  if (causingStart == 0) {
    return loopLength > 0 ? loopLength - 1 : 0;
  }
  return causingStart - 1;
}

NOTE_EDIT_MEM NoteBaseline projectNoteBaselineForEditAnalysis(const EditorSelection& selection,
                                                const NoteBaseline& baseline, NoteId noteId,
                                                uint32_t loopLength, int32_t originTick) {
  const TickInterval window = IntervalProjection::makeFullLoopEditAnalysisWindow(loopLength);
  const ProjectionContext context = IntervalProjection::buildEditProjectionContext(
      selection, loopLength, window, originTick);
  CanonicalNoteSpan span{};
  span.noteId = noteId;
  span.interval = TickInterval{static_cast<int32_t>(baseline.startTick),
                               static_cast<int32_t>(baseline.endTick)};
  span.pitch = baseline.pitch;
  span.velocity = baseline.velocity;
  const ProjectedNoteInterval projected =
      IntervalProjection::projectEditLinearSpan(span, context);
  NoteBaseline linear = baseline;
  if (projected.noteId != kInvalidNoteId) {
    linear.startTick = static_cast<uint32_t>(projected.interval.start);
    linear.endTick = static_cast<uint32_t>(projected.interval.end);
  }
  return linear;
}
