//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "EditPass.h"
#include "EditSessionAction.h"
#include "NoteEditFocus.h"

/// Record one applied action into the open noteEditPass batch (coalesce by targetNoteId).
void recordApplyOwnedEditPassRow(EditPassVec& rows, const EditSessionAction& action,
                                 const NoteEditFocus& focus);

/// Append mover focus diff rows not already represented in apply-owned rows.
void appendMoverFocusCommitRows(EditPassVec& rows, const NoteEditFocus& focus);

/// Build commit rows: apply-owned overlap/mover rows plus any pending mover focus diff.
EditPassVec buildCommitRowsFromApplyOwned(const EditPassVec& applyOwnedRows,
                                          const NoteEditFocus& focus);
