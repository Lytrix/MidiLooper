//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "RunEditSessionGeometryPipeline.h"

#include <algorithm>
#include <unordered_set>

#include "ApplyEditSessionActions.h"
#include "EditManager.h"
#include "EditSessionActionBuilder.h"
#include "EditSessionInteraction.h"
#include "EditSessionLiveStoreSpan.h"
#include "Globals.h"
#include "ResolveConstrainedGeometry.h"
#include "Utils/NoteEditMem.h"

#if defined(SESSION_CAPTURE)
#include "Logger.h"
#endif

namespace {

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

  // noteIds are assigned once in openNoteEditSession — never mint mid-edit (Delete rows
  // must resolve against capture-pass materialize). Stamp offs only.
  stampNoteIdsOntoPairedNoteOffs(liveStore);
#if defined(SESSION_CAPTURE)
  size_t liveNoteOnCount = 0;
  for (const MidiEvent& evt : liveStore) {
    if (!evt.isNoteOn() || evt.data.noteData.velocity == 0) {
      continue;
    }
    ++liveNoteOnCount;
    if (evt.noteId == kInvalidNoteId) {
      logger.log(CAT_MIDI, LOG_WARNING,
                 "GeometryPipeline: note-on missing noteId tick=%lu pitch=%u (session open "
                 "should have assigned)",
                 static_cast<unsigned long>(evt.tick),
                 static_cast<unsigned>(evt.data.noteData.note));
    }
  }
#endif

  const BaselineMap& transactionBaseline = focus.baselineMap;
  const EditorSelection& selection = editedGeometry.selection;

  const std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> changedCausingNotes =
      determineChangedCausingNotes(selection, editedGeometry, priorLatchByNoteId);
  const bool overlapRestoreOnly =
      changedCausingNotes.empty() && !focus.changedOverlapNoteIds.empty();
  if (changedCausingNotes.empty() && !overlapRestoreOnly) {
    return false;
  }

  // Sorted candidate targets and the projected baseline come from one scope list, so candidate
  // pairing and projection can never disagree about what this tick evaluates.
  const NoteIdList evaluationScope =
      collectEvaluationScopeNoteIds(transactionBaseline, liveStore, focus.changedOverlapNoteIds,
                                    focus.movingNoteId, overlapPitchLane);

  const std::vector<CausingTargetPair, InternalHeapFirstAllocator<CausingTargetPair>> eligiblePairs =
      determineEligiblePairs(selection, changedCausingNotes, evaluationScope);

  const BaselineMap projectedBaseline = projectTransactionBaselineForEvaluationScope(
      selection, transactionBaseline, evaluationScope, focus.movingNoteId, loopLength);
  const EditedGeometry projectedEdited =
      projectEditedGeometryForAnalysis(editedGeometry, loopLength);

  const std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>>
      interactions =
          analyzeEditSessionInteractions(eligiblePairs, projectedEdited, projectedBaseline);

  const EditSessionInteractionsByTarget grouped =
      groupEditSessionInteractionsByTarget(interactions);

  // Resolve + build use the same projected baseline/edited spans as analyze so shorten
  // ends stay in the linear frame (avoids inverted off ticks / check=2).
  const std::vector<ConstrainedNoteGeometry,
                     InternalHeapFirstAllocator<ConstrainedNoteGeometry>> constrained =
      resolveAllConstrainedGeometry(grouped, projectedBaseline, liveStore, channel, loopLength,
                                    noteMinLengthTicks, noteMinLengthRemoveEnabled, selection,
                                    projectedEdited);

  const EditSessionActions actions =
      buildEditSessionActions(constrained, projectedEdited, projectedBaseline, liveStore,
                              channel, focus, loopLength);

  if (actions.empty()) {
    return false;
  }

#if defined(SESSION_CAPTURE)
  logger.log(CAT_MIDI, LOG_DEBUG,
             "GeometryPipeline: storeNoteOns=%u baselineMap=%u lane=%d changed=%u candidates=%u "
             "pairs=%u interactions=%u constrained=%u actions=%u",
             static_cast<unsigned>(liveNoteOnCount),
             static_cast<unsigned>(transactionBaseline.size()),
             overlapPitchLane.has_value() ? static_cast<int>(overlapPitchLane.value()) : -1,
             static_cast<unsigned>(focus.changedOverlapNoteIds.size()),
             static_cast<unsigned>(evaluationScope.size()),
             static_cast<unsigned>(eligiblePairs.size()),
             static_cast<unsigned>(interactions.size()),
             static_cast<unsigned>(constrained.size()), static_cast<unsigned>(actions.size()));
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
  // Geometry drivers edit focus.movingNoteId; fader-1 browse may leave selection on another note.
  editedGeometry.selection.primaryNote = causingNoteId;
  editedGeometry.selection.selectedNotes.clear();
  editedGeometry.selection.selectedNotes.push_back(causingNoteId);

  EditedNoteSpan causingSpan{};
  causingSpan.noteId = causingNoteId;
  causingSpan.span = editedSpan;
  editedGeometry.causingSpans.push_back(causingSpan);

  return runEditSessionGeometryPipeline(track, manager, editedGeometry, priorLatchByNoteId,
                                        overlapPitchLane, refreshPlaybackPreview);
}
