//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteEditFocus.h"
#include "NoteEditCurrentState.h"
#include "NoteEditSessionState.h"
#include "NoteEditFocusInternal.h"
#include "ParticipatingNoteSession.h"

#include <algorithm>

#include "Utils/NoteEditMem.h"

#if defined(SESSION_CAPTURE)
#include "Logger.h"
#endif

namespace {

NOTE_EDIT_MEM bool endTickIsLoopEnd(uint32_t endTick, uint32_t loopLength) {
  if (loopLength == 0) {
    return false;
  }
  return endTick == loopLength || endTick == loopLength - 1;
}

NOTE_EDIT_MEM bool committedDisplayNotesPaintLoopEnd(
    const NoteUtils::DisplayNoteVec* committedDisplayNotes, NoteId noteId, uint32_t loopLength) {
  if (committedDisplayNotes == nullptr) {
    return true;
  }
  for (const NoteUtils::DisplayNote& displayNote : *committedDisplayNotes) {
    if (displayNote.noteId == noteId && displayNote.endTick != displayNote.startTick &&
        endTickIsLoopEnd(displayNote.endTick, loopLength)) {
      return true;
    }
  }
  return false;
}

NOTE_EDIT_MEM bool shouldSkipOverlapDiffToLoopEnd(
    const NoteBaseline& live, uint32_t loopLength, NoteId noteId,
    const NoteUtils::DisplayNoteVec* committedDisplayNotes) {
  if (!endTickIsLoopEnd(live.endTick, loopLength)) {
    return false;
  }
  return !committedDisplayNotesPaintLoopEnd(committedDisplayNotes, noteId, loopLength);
}

}  // namespace

NOTE_EDIT_FOCUS_INTERNAL_MEM EditPass makeNoteEditRow(EditActionType actionType,
                                                      EditPropertyType propertyType) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = actionType;
  row.propertyType = propertyType;
  row.state = EditPassState::Active;
  return row;
}

NOTE_EDIT_MEM EditPassVec buildPreCommitBaselineLiveDiffOverlapPasses(
    const NoteEditFocus& focus, const MidiEventVec& sessionEvents, uint8_t channel,
    uint32_t loopLength, const NoteEditCurrentState* currentState,
    const NoteUtils::DisplayNoteVec* committedDisplayNotes) {
  EditPassVec rows;
  if (!focus.active) {
    return rows;
  }
  if (currentState == nullptr || currentState->empty()) {
    return rows;
  }

  for (const auto& [noteId, baseline] : focus.baselineMap) {
    if (noteId == kInvalidNoteId || noteId == focus.movingNoteId) {
      continue;
    }
    const NoteEditCurrentNoteState* stateRow = currentState->find(noteId);
    if (stateRow == nullptr || !currentStateRowIsOverlapParticipant(*stateRow)) {
      continue;
    }
    NoteBaseline live{};
    const bool hasLive =
        readLiveBaselineForOverlapDiff(sessionEvents, noteId, baseline, channel, loopLength,
                                       focus.movingNoteId, live);
    if (!hasLive) {
      // Delete authority: only an Active overlap participant may be removed. An unresolved
      // baseline entry (pass-materialize noteId vs session-store noteId) is preserved and
      // reported — a lookup miss must never destroy a note.
      EditPass row = makeNoteEditRow(EditActionType::Delete, EditPropertyType::None);
      row.targetNoteId = noteId;
      rows.push_back(row);
      continue;
    }
    if (live.pitch != baseline.pitch) {
      continue;
    }
    if (loopLength > 0 &&
        !isPlausibleStorageSpan(live.startTick, live.endTick, loopLength)) {
#if defined(SESSION_CAPTURE)
      logger.log(CAT_TRACK, LOG_WARNING,
                 "NOTE_EDIT pre-commit: overlap noteId=%lu live span start=%lu end=%lu implausible; "
                 "skipped",
                 static_cast<unsigned long>(noteId),
                 static_cast<unsigned long>(live.startTick),
                 static_cast<unsigned long>(live.endTick));
#endif
      continue;
    }
    if (shouldSkipOverlapDiffToLoopEnd(live, loopLength, noteId, committedDisplayNotes)) {
      continue;
    }
    if (live.startTick != baseline.startTick) {
      EditPass row = makeNoteEditRow(EditActionType::Update, EditPropertyType::NoteRange);
      row.targetNoteId = noteId;
      row.startTick = live.startTick;
      row.endTick = live.endTick;
      rows.push_back(row);
    } else if (live.endTick != baseline.endTick) {
      EditPass row = makeNoteEditRow(EditActionType::Update, EditPropertyType::Length);
      row.targetNoteId = noteId;
      row.startTick = baseline.startTick;
      row.endTick = live.endTick;
      rows.push_back(row);
    }
  }
  return rows;
}

namespace {

NOTE_EDIT_MEM bool isPlausibleMoverLinearSpan(uint32_t startTick, uint32_t endTick,
                                              uint32_t loopLength) {
  if (loopLength == 0) {
    return false;
  }
  if (startTick >= loopLength) {
    return false;
  }
  // Wrap (end < start) is valid storage. Rejecting it dropped 195050 wrap 2736–240
  // and the deselect row became the display tail to loopLength-1.
  return isPlausibleStorageSpan(startTick, endTick, loopLength);
}

NOTE_EDIT_MEM bool shouldRejectMoverPreCommitRow(
    const EditPass& row, const NoteEditFocus& focus, uint32_t loopLength,
    const NoteUtils::DisplayNoteVec* committedDisplayNotes) {
  if (!focus.active || row.targetNoteId != focus.movingNoteId) {
    return false;
  }
  if (row.actionType != EditActionType::Update) {
    return false;
  }
  if (row.propertyType == EditPropertyType::NoteRange) {
    if (row.startTick != focus.last.startTick || row.endTick != focus.last.endTick) {
      return true;
    }
    if (!isPlausibleMoverLinearSpan(row.startTick, row.endTick, loopLength)) {
      return true;
    }
    if (row.startTick == 0 && focus.commitBaseline.startTick != 0) {
      return true;
    }
    return false;
  }
  if (row.propertyType == EditPropertyType::Length) {
    if (row.startTick != focus.commitBaseline.startTick || row.endTick != focus.last.endTick) {
      return true;
    }
    if (focus.commitBaseline.endTick >= loopLength && focus.last.endTick < loopLength) {
      return true;
    }
    // 192140: wrap-head select 80–80, close wrote Length 80–2303 after normalizeAll
    // closeOpenTails. Same emit filter as overlap RC3 — skip unless paint is loop-end.
    if (endTickIsLoopEnd(row.endTick, loopLength) &&
        !endTickIsLoopEnd(focus.commitBaseline.endTick, loopLength) &&
        !committedDisplayNotesPaintLoopEnd(committedDisplayNotes, focus.movingNoteId,
                                           loopLength)) {
      return true;
    }
    return !isPlausibleMoverLinearSpan(row.startTick, row.endTick, loopLength);
  }
  return false;
}

NOTE_EDIT_MEM void removeInvalidMoverPreCommitRows(
    EditPassVec& rows, const NoteEditFocus& focus, uint32_t loopLength,
    const NoteUtils::DisplayNoteVec* committedDisplayNotes) {
  const auto invalid = [&](const EditPass& row) {
    return shouldRejectMoverPreCommitRow(row, focus, loopLength, committedDisplayNotes);
  };
  rows.erase(std::remove_if(rows.begin(), rows.end(), invalid), rows.end());
}

NOTE_EDIT_MEM bool commitRowsContainTarget(const EditPassVec& rows, NoteId noteId) {
  if (noteId == kInvalidNoteId) {
    return false;
  }
  for (const EditPass& row : rows) {
    if (row.targetNoteId == noteId) {
      return true;
    }
  }
  return false;
}

NOTE_EDIT_MEM void appendGeometryDiffRows(EditPassVec& rows, NoteId noteId,
                                          const NoteBaseline& committed,
                                          const NoteBaseline& live) {
  if (noteId == kInvalidNoteId) {
    return;
  }
  const bool startChanged = live.startTick != committed.startTick;
  const bool endChanged = live.endTick != committed.endTick;
  const bool pitchChanged = live.pitch != committed.pitch;
  if (startChanged) {
    EditPass row = makeNoteEditRow(EditActionType::Update, EditPropertyType::NoteRange);
    row.targetNoteId = noteId;
    row.startTick = live.startTick;
    row.endTick = live.endTick;
    rows.push_back(row);
  } else if (endChanged) {
    EditPass row = makeNoteEditRow(EditActionType::Update, EditPropertyType::Length);
    row.targetNoteId = noteId;
    row.startTick = committed.startTick;
    row.endTick = live.endTick;
    rows.push_back(row);
  }
  if (pitchChanged) {
    EditPass row = makeNoteEditRow(EditActionType::Update, EditPropertyType::Pitch);
    row.targetNoteId = noteId;
    row.pitch = live.pitch;
    rows.push_back(row);
  }
}

NOTE_EDIT_MEM void appendUncommittedVisibleCurrentStateRows(
    EditPassVec& rows, const NoteEditFocus& focus, const NoteEditCurrentState& currentState,
    uint32_t loopLength, const NoteUtils::DisplayNoteVec* committedDisplayNotes) {
  for (const auto& [noteId, row] : currentState.rows()) {
    if (noteId == kInvalidNoteId) {
      continue;
    }
    if (focus.active && noteId == focus.movingNoteId) {
      continue;
    }
    if (!currentStateRowIsExistingAndVisible(row) ||
        !currentStateRowGeometryDiffersFromCommitted(row)) {
      continue;
    }
    if (row.overlapParticipation == NoteEditOverlapParticipationType::Ended) {
      continue;
    }
    if (commitRowsContainTarget(rows, noteId)) {
      continue;
    }
    if (shouldSkipOverlapDiffToLoopEnd(row.currentSpan, loopLength, noteId,
                                       committedDisplayNotes)) {
      continue;
    }
    appendGeometryDiffRows(rows, noteId, row.committedSpan, row.currentSpan);
  }
}

}  // namespace

NOTE_EDIT_MEM EditPassVec buildCommitOverlapRowsFromCurrentState(
    const NoteEditFocus& focus, const NoteEditCurrentState& currentState, uint8_t channel,
    uint32_t loopLength, const NoteUtils::DisplayNoteVec* committedDisplayNotes) {
  EditPassVec rows;
  if (!focus.active) {
    return rows;
  }
  (void)channel;

  for (const auto& [noteId, baseline] : focus.baselineMap) {
    if (noteId == kInvalidNoteId || noteId == focus.movingNoteId) {
      continue;
    }

    const NoteEditCurrentNoteState* row = currentState.find(noteId);
    // §11 step 5.4: commit overlap authority from current-state participation, not Focus latch.
    if (row == nullptr || !currentStateRowIsOverlapParticipant(*row)) {
      continue;
    }
    if (currentStateRowLifecycleIsDeleted(*row) || !currentStateRowIsVisible(*row)) {
      EditPass deleteRow = makeNoteEditRow(EditActionType::Delete, EditPropertyType::None);
      deleteRow.targetNoteId = noteId;
      rows.push_back(std::move(deleteRow));
      continue;
    }

    const NoteBaseline& live = row->currentSpan;
    if (live.pitch != baseline.pitch) {
      continue;
    }

    // A parked mover always still covers the span it shortened, so deferring on active overlap
    // closure meant the shorten could never seal at the commit the user triggers. Closure defers
    // leave-restore (ResolveConstrainedGeometry), not persistence.
    if (loopLength > 0 && !isPlausibleStorageSpan(live.startTick, live.endTick, loopLength)) {
#if defined(SESSION_CAPTURE)
      logger.log(CAT_TRACK, LOG_WARNING,
                 "NOTE_EDIT commit: overlap noteId=%lu current span start=%lu end=%lu implausible; "
                 "skipped",
                 static_cast<unsigned long>(noteId),
                 static_cast<unsigned long>(live.startTick),
                 static_cast<unsigned long>(live.endTick));
#endif
      continue;
    }
    if (shouldSkipOverlapDiffToLoopEnd(live, loopLength, noteId, committedDisplayNotes)) {
      continue;
    }
    if (live.startTick != baseline.startTick) {
      EditPass updateRow = makeNoteEditRow(EditActionType::Update, EditPropertyType::NoteRange);
      updateRow.targetNoteId = noteId;
      updateRow.startTick = live.startTick;
      updateRow.endTick = live.endTick;
      rows.push_back(std::move(updateRow));
    } else if (live.endTick != baseline.endTick) {
      EditPass updateRow = makeNoteEditRow(EditActionType::Update, EditPropertyType::Length);
      updateRow.targetNoteId = noteId;
      updateRow.startTick = baseline.startTick;
      updateRow.endTick = live.endTick;
      rows.push_back(std::move(updateRow));
    }
  }

  for (const auto& [noteId, row] : currentState.rows()) {
    if (!currentStateRowLifecycleIsAdded(row)) {
      continue;
    }
    if (focus.baselineMap.find(noteId) != focus.baselineMap.end()) {
      continue;
    }
    const NoteBaseline& span = row.currentSpan;
    EditPass createRow = makeNoteEditRow(EditActionType::Create, EditPropertyType::None);
    MidiEvent noteOn = MidiEvent::NoteOn(span.startTick, channel, span.pitch, span.velocity);
    noteOn.noteId = noteId;
    createRow.addedEvents.push_back(noteOn);
    createRow.addedEvents.push_back(MidiEvent::NoteOff(span.endTick, channel, span.pitch, 0));
    rows.push_back(std::move(createRow));
  }

  return rows;
}

NOTE_EDIT_MEM EditPassVec buildCommitRowsFromCurrentState(
    const NoteEditFocus& focus, const NoteEditCurrentState& currentState, uint8_t channel,
    uint32_t loopLength, const NoteUtils::DisplayNoteVec* committedDisplayNotes) {
  EditPassVec rows;
  if (loopLength > 0 && !currentState.empty()) {
    rows = buildCommitOverlapRowsFromCurrentState(focus, currentState, channel, loopLength,
                                                  committedDisplayNotes);
  }
  if (focus.active) {
    (void)channel;
    appendGeometryDiffRows(rows, focus.movingNoteId, focus.commitBaseline, focus.last);
  }
  if (!currentState.empty()) {
    appendUncommittedVisibleCurrentStateRows(rows, focus, currentState, loopLength,
                                            committedDisplayNotes);
  }

  if (loopLength > 0) {
    removeInvalidMoverPreCommitRows(rows, focus, loopLength, committedDisplayNotes);
  }

  return rows;
}

NOTE_EDIT_MEM EditPassVec buildPreCommitEditPasses(
    const NoteEditFocus& focus, uint8_t channel, const MidiEventVec* sessionStoreEvents,
    uint32_t loopLength, const NoteEditCurrentState* currentState,
    const NoteUtils::DisplayNoteVec* committedDisplayNotes) {
  EditPassVec rows;
  if (sessionStoreEvents != nullptr && loopLength > 0) {
    rows = buildPreCommitBaselineLiveDiffOverlapPasses(focus, *sessionStoreEvents, channel,
                                                       loopLength, currentState,
                                                       committedDisplayNotes);
  }
  if (focus.active) {
    (void)channel;
    appendGeometryDiffRows(rows, focus.movingNoteId, focus.commitBaseline, focus.last);
  }
  if (currentState != nullptr && !currentState->empty()) {
    appendUncommittedVisibleCurrentStateRows(rows, focus, *currentState, loopLength,
                                            committedDisplayNotes);
  }

  if (loopLength > 0) {
    removeInvalidMoverPreCommitRows(rows, focus, loopLength, committedDisplayNotes);
  }

  return rows;
}
