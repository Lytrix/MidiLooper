//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "ApplyOwnedEditPassRows.h"

#include <algorithm>

#include "Utils/NoteEditMem.h"

namespace {

NOTE_EDIT_MEM EditPass makeOwnedEditPassRowShell(EditActionType actionType,
                                                 EditPropertyType propertyType) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = actionType;
  row.propertyType = propertyType;
  row.state = EditPassState::Active;
  return row;
}

NOTE_EDIT_MEM auto findRowForNoteId(EditPassVec& rows, NoteId noteId) {
  return std::find_if(rows.begin(), rows.end(), [noteId](const EditPass& row) {
    return row.targetNoteId == noteId;
  });
}

NOTE_EDIT_MEM void eraseRowForNoteId(EditPassVec& rows, NoteId noteId) {
  const auto it = findRowForNoteId(rows, noteId);
  if (it != rows.end()) {
    rows.erase(it);
  }
}

}  // namespace

NOTE_EDIT_MEM void recordApplyOwnedEditPassRow(EditPassVec& rows, const EditSessionAction& action,
                                               const NoteEditFocus& focus) {
  if (action.targetNoteId == kInvalidNoteId) {
    return;
  }

  switch (action.type) {
    case EditSessionActionType::RestoreNote:
      eraseRowForNoteId(rows, action.targetNoteId);
      return;
    case EditSessionActionType::HideNote: {
      eraseRowForNoteId(rows, action.targetNoteId);
      EditPass row = makeOwnedEditPassRowShell(EditActionType::Delete, EditPropertyType::None);
      row.targetNoteId = action.targetNoteId;
      rows.push_back(row);
      return;
    }
    case EditSessionActionType::ShortenNote: {
      eraseRowForNoteId(rows, action.targetNoteId);
      EditPass row = makeOwnedEditPassRowShell(EditActionType::Update, EditPropertyType::Length);
      row.targetNoteId = action.targetNoteId;
      const auto baselineIt = focus.baselineMap.find(action.targetNoteId);
      row.startTick =
          baselineIt != focus.baselineMap.end() ? baselineIt->second.startTick : action.startTick;
      row.endTick = action.endTick;
      rows.push_back(row);
      return;
    }
    case EditSessionActionType::MoveNote: {
      eraseRowForNoteId(rows, action.targetNoteId);
      const auto baselineIt = focus.baselineMap.find(action.targetNoteId);
      const uint32_t baselineStart =
          baselineIt != focus.baselineMap.end() ? baselineIt->second.startTick : action.startTick;
      EditPass row;
      if (action.startTick != baselineStart) {
        row = makeOwnedEditPassRowShell(EditActionType::Update, EditPropertyType::NoteRange);
        row.startTick = action.startTick;
        row.endTick = action.endTick;
      } else {
        row = makeOwnedEditPassRowShell(EditActionType::Update, EditPropertyType::Length);
        row.startTick = baselineStart;
        row.endTick = action.endTick;
      }
      row.targetNoteId = action.targetNoteId;
      rows.push_back(row);
      return;
    }
    case EditSessionActionType::ChangeLength: {
      eraseRowForNoteId(rows, action.targetNoteId);
      EditPass row = makeOwnedEditPassRowShell(EditActionType::Update, EditPropertyType::Length);
      row.targetNoteId = action.targetNoteId;
      row.startTick = action.startTick;
      row.endTick = action.endTick;
      rows.push_back(row);
      return;
    }
    case EditSessionActionType::ChangePitch: {
      eraseRowForNoteId(rows, action.targetNoteId);
      EditPass row = makeOwnedEditPassRowShell(EditActionType::Update, EditPropertyType::Pitch);
      row.targetNoteId = action.targetNoteId;
      row.pitch = action.pitch;
      rows.push_back(row);
      return;
    }
  }
}

NOTE_EDIT_MEM void appendMoverFocusCommitRows(EditPassVec& rows, const NoteEditFocus& focus) {
  if (!focus.active || focus.movingNoteId == kInvalidNoteId) {
    return;
  }

  const bool pitchChanged = focus.last.pitch != focus.commitBaseline.pitch;
  const auto moverRowIt = findRowForNoteId(rows, focus.movingNoteId);
  if (moverRowIt != rows.end()) {
    // Apply-owned geometry row already recorded; still emit ChangePitch when focus differs
    // (session_20260805_170218: move+overlap commit dropped pitch on deselect).
    if (pitchChanged && moverRowIt->propertyType != EditPropertyType::Pitch) {
      EditPass row = makeOwnedEditPassRowShell(EditActionType::Update, EditPropertyType::Pitch);
      row.targetNoteId = focus.movingNoteId;
      row.pitch = focus.last.pitch;
      rows.push_back(row);
    }
    return;
  }

  const bool startChanged = focus.last.startTick != focus.commitBaseline.startTick;
  const bool endChanged = focus.last.endTick != focus.commitBaseline.endTick;

  if (startChanged) {
    EditPass row = makeOwnedEditPassRowShell(EditActionType::Update, EditPropertyType::NoteRange);
    row.targetNoteId = focus.movingNoteId;
    row.startTick = focus.last.startTick;
    row.endTick = focus.last.endTick;
    rows.push_back(row);
  } else if (endChanged) {
    EditPass row = makeOwnedEditPassRowShell(EditActionType::Update, EditPropertyType::Length);
    row.targetNoteId = focus.movingNoteId;
    row.startTick = focus.commitBaseline.startTick;
    row.endTick = focus.last.endTick;
    rows.push_back(row);
  }

  if (pitchChanged) {
    EditPass row = makeOwnedEditPassRowShell(EditActionType::Update, EditPropertyType::Pitch);
    row.targetNoteId = focus.movingNoteId;
    row.pitch = focus.last.pitch;
    rows.push_back(row);
  }
}

NOTE_EDIT_MEM EditPassVec buildCommitRowsFromApplyOwned(const EditPassVec& applyOwnedRows,
                                                        const NoteEditFocus& focus) {
  EditPassVec rows = applyOwnedRows;
  appendMoverFocusCommitRows(rows, focus);
  return rows;
}
