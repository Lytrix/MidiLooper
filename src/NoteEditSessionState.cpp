//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteEditSessionState.h"

NoteEditKind advanceEncoderCycleKind(NoteEditKind current) {
  switch (current) {
    case NoteEditKind::Select:
      return NoteEditKind::Move;
    case NoteEditKind::Move:
      return NoteEditKind::Pitch;
    case NoteEditKind::Pitch:
      return NoteEditKind::Length;
    case NoteEditKind::Length:
    case NoteEditKind::Add:
    case NoteEditKind::Delete:
      return NoteEditKind::Select;
  }
  return NoteEditKind::Select;
}

NoteEditKind resolveEncoderCyclePress(NoteEditKind current, bool faderAnchorPending) {
  if (faderAnchorPending) {
    return NoteEditKind::Move;
  }
  return advanceEncoderCycleKind(current);
}
