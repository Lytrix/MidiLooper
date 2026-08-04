//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "RunEditSessionGeometryPipeline.h"

#include <algorithm>

#include "ApplyEditSessionActions.h"
#include "EditSessionActionBuilder.h"
#include "EditSessionInteraction.h"
#include "EditSessionLiveStoreSpan.h"
#include "Globals.h"
#include "ResolveConstrainedGeometry.h"
#include "Utils/NoteEditMem.h"

namespace {

NOTE_EDIT_MEM BaselineMap projectTransactionBaselineForAnalysis(const EditorSelection& selection,
                                                   const BaselineMap& transactionBaseline,
                                                   uint32_t loopLength) {
  BaselineMap projected;
  for (const auto& [noteId, baseline] : transactionBaseline) {
    projected[noteId] =
        projectNoteBaselineForEditAnalysis(selection, baseline, noteId, loopLength);
  }
  return projected;
}

NOTE_EDIT_MEM EditedGeometry projectEditedGeometryForAnalysis(const EditedGeometry& editedGeometry,
                                                uint32_t loopLength) {
  EditedGeometry projected = editedGeometry;
  for (EditedNoteSpan& causing : projected.causingSpans) {
    causing.span =
        projectNoteBaselineForEditAnalysis(editedGeometry.selection, causing.span, causing.noteId,
                                           loopLength);
  }
  return projected;
}

NOTE_EDIT_MEM std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> collectCandidateTargetNoteIds(
    const BaselineMap& transactionBaseline, NoteId movingNoteId,
    std::optional<uint8_t> overlapPitchLane) {
  std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> candidates;
  for (const auto& [noteId, baseline] : transactionBaseline) {
    if (noteId == kInvalidNoteId || noteId == movingNoteId) {
      continue;
    }
    if (overlapPitchLane.has_value() && baseline.pitch != overlapPitchLane.value()) {
      continue;
    }
    candidates.push_back(noteId);
  }
  sortNoteIdVector(candidates);
  return candidates;
}

}  // namespace

NOTE_EDIT_MEM bool runEditSessionGeometryPipeline(
    Track& track, EditManager& manager, const EditedGeometry& editedGeometry,
    const std::unordered_map<NoteId, NoteBaseline, NoteIdHash>& priorLatchByNoteId,
    std::optional<uint8_t> overlapPitchLane, bool refreshPlaybackPreview) {
  if (!manager.isNoteSessionStoreOpen()) {
    return false;
  }

  MidiEventVec& liveStore = track.editAwareMidiEvents();
  NoteEditFocus& focus = manager.getEditSession().focus;
  if (!focus.active || focus.movingNoteId == kInvalidNoteId) {
    return false;
  }

  const uint8_t channel = track.getMidiChannel();
  const uint32_t loopLength = manager.noteEditLoopLengthTicks(track);
  if (loopLength == 0) {
    return false;
  }

  // Capture any same-pitch live notes missing from baselineMap (select-time closure may
  // have been narrower before pitch-lane full closure; mid-driver discovery uses current
  // live span as the immutable baseline for the rest of this edit driver).
  if (overlapPitchLane.has_value()) {
    const uint8_t pitchLane = overlapPitchLane.value();
    for (const MidiEvent& evt : liveStore) {
      if (evt.channel != channel || !evt.isNoteOn() || evt.data.noteData.velocity == 0 ||
          evt.data.noteData.note != pitchLane || evt.noteId == kInvalidNoteId ||
          evt.noteId == focus.movingNoteId) {
        continue;
      }
      if (focus.baselineMap.find(evt.noteId) != focus.baselineMap.end()) {
        continue;
      }
      NoteBaseline span{};
      if (findLinearNoteSpanForNoteId(liveStore, evt.noteId, channel, span, evt.tick,
                                      loopLength)) {
        focus.baselineMap[evt.noteId] = span;
      }
    }
  }

  const BaselineMap& transactionBaseline = focus.baselineMap;
  const EditorSelection& selection = editedGeometry.selection;

  const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> changedCausingNotes =
      determineChangedCausingNotes(selection, editedGeometry, priorLatchByNoteId);
  if (changedCausingNotes.empty()) {
    return false;
  }

  const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> candidateTargetNoteIds =
      collectCandidateTargetNoteIds(transactionBaseline, focus.movingNoteId, overlapPitchLane);

  const std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>> eligiblePairs =
      determineEligiblePairs(selection, changedCausingNotes, candidateTargetNoteIds);

  const BaselineMap projectedBaseline =
      projectTransactionBaselineForAnalysis(selection, transactionBaseline, loopLength);
  const EditedGeometry projectedEdited =
      projectEditedGeometryForAnalysis(editedGeometry, loopLength);

  const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>
      interactions =
          analyzeEditSessionInteractions(eligiblePairs, projectedEdited, projectedBaseline);

  const EditSessionInteractionsByTarget grouped =
      groupEditSessionInteractionsByTarget(interactions);

  const std::vector<ConstrainedNoteGeometry,
                     InternalHeapFirstAllocator<ConstrainedNoteGeometry>> constrained =
      resolveAllConstrainedGeometry(grouped, transactionBaseline, liveStore, channel, loopLength,
                                    noteMinLengthTicks, noteMinLengthRemoveEnabled, selection,
                                    editedGeometry);

  const EditSessionActions actions =
      buildEditSessionActions(constrained, editedGeometry, transactionBaseline, liveStore,
                              channel, focus, loopLength);

  if (actions.empty()) {
    return false;
  }

#if defined(SESSION_CAPTURE)
  logEditSessionActions(actions);
#endif

  applyEditSessionActions(actions, liveStore, focus, channel, loopLength);
  track.invalidateCaches(refreshPlaybackPreview);
  return true;
}

/// Single-causing-note entry — latch map built in FLASHMEM (keeps std::unordered_map out of
/// NoteMovementUtils translation unit / RAM1 ITCM). Declared in RunEditSessionGeometryPipelineDriver.h.
NOTE_EDIT_MEM bool runEditSessionGeometryPipelineForCausingNote(
    Track& track, EditManager& manager, NoteId causingNoteId, const NoteBaseline& editedSpan,
    const NoteBaseline& priorLatch, std::optional<uint32_t> selectionTick,
    std::optional<uint8_t> overlapPitchLane, bool refreshPlaybackPreview) {
  if (causingNoteId == kInvalidNoteId) {
    return false;
  }

  std::unordered_map<NoteId, NoteBaseline, NoteIdHash> priorLatchByNoteId;
  priorLatchByNoteId[causingNoteId] = priorLatch;

  EditedGeometry editedGeometry{};
  editedGeometry.selection = manager.getNoteEditSessionState().selection;
  if (selectionTick.has_value()) {
    editedGeometry.selection.selectedTick = selectionTick.value();
  }

  EditedNoteSpan causingSpan{};
  causingSpan.noteId = causingNoteId;
  causingSpan.span = editedSpan;
  editedGeometry.causingSpans.push_back(causingSpan);

  return runEditSessionGeometryPipeline(track, manager, editedGeometry, priorLatchByNoteId,
                                        overlapPitchLane, refreshPlaybackPreview);
}
