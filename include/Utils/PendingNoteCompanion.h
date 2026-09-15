// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once

#include "EditPass.h"
#include "PendingNoteChange.h"

/// Canonical payload shared by legacy sealing and frozen preparation. The
/// destination must be a fresh or unpublished prepared row with no added events.
inline bool preparePendingNoteCompanion(const PendingNoteChange& change, EditPass& row) {
  if (change.kind != PendingNoteChangeKind::Shorten && change.kind != PendingNoteChangeKind::Hide)
    return false;
  row = EditPass{};
  row.passType = EditPassType::Note;
  row.editPassIndex = kOverdubCompanionEditPassIndex;
  row.targetNoteId = change.noteId;
  row.pitch = change.pitch;
  row.velocity = change.velocity;
  row.startTick = change.startTick;
  row.endTick = change.endTick;
  row.actionType = change.kind == PendingNoteChangeKind::Hide ? EditActionType::Delete : EditActionType::Update;
  row.propertyType = change.kind == PendingNoteChangeKind::Hide ? EditPropertyType::None : EditPropertyType::Length;
  return true;
}
