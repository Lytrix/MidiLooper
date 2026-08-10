//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "NoteEditFocusTypes.h"

template <typename NotesVec>
inline NoteId noteIdFromFilteredDisplayNote(const NotesVec& filtered, int filteredIndex) {
  if (filteredIndex < 0 || filteredIndex >= static_cast<int>(filtered.size())) {
    return kInvalidNoteId;
  }
  return filtered[static_cast<size_t>(filteredIndex)].noteId;
}

template <typename NotesVec>
inline int filteredDisplayNoteIndexForNoteId(const NotesVec& filtered, NoteId noteId) {
  if (noteId == kInvalidNoteId) {
    return -1;
  }
  for (int i = 0; i < static_cast<int>(filtered.size()); ++i) {
    if (filtered[static_cast<size_t>(i)].noteId == noteId) {
      return i;
    }
  }
  return -1;
}

inline uint32_t displayStartTickFromStorageNote(uint32_t storageStart, uint32_t loopStartTick,
                                                uint32_t loopLength) {
  if (loopLength == 0) {
    return storageStart;
  }
  const uint32_t displayStart = (storageStart >= loopStartTick)
                                    ? (storageStart - loopStartTick)
                                    : (storageStart + loopLength - loopStartTick);
  return displayStart % loopLength;
}

/// Resolve macro-commit select target when fader-1 bracket tick may hand off to a different
/// **NoteId** than the inventory row index (session_20260807_195514: moving=12 bracket=2881 → note 14).
template <typename NotesVec>
inline NoteId resolveMacroCommitSelectTargetNoteId(const NotesVec& filtered, int filteredIndex,
                                                   uint32_t selectBracketTick,
                                                   const NoteEditFocus& focus,
                                                   uint32_t loopStartTick, uint32_t loopLength) {
  const NoteId inventoryNoteId = noteIdFromFilteredDisplayNote(filtered, filteredIndex);
  if (!focus.active || focus.movingNoteId == kInvalidNoteId) {
    return inventoryNoteId;
  }
  if (inventoryNoteId != kInvalidNoteId && inventoryNoteId != focus.movingNoteId) {
    return inventoryNoteId;
  }
  if (inventoryNoteId == focus.movingNoteId &&
      isMacroCommitAlignedWithSelectTarget(inventoryNoteId, selectBracketTick, focus, loopStartTick,
                                           loopLength, false)) {
    return inventoryNoteId;
  }
  for (const auto& dn : filtered) {
    if (dn.noteId == kInvalidNoteId || dn.noteId == focus.movingNoteId) {
      continue;
    }
    if (displayStartTickFromStorageNote(dn.startTick, loopStartTick, loopLength) ==
        selectBracketTick) {
      return dn.noteId;
    }
  }
  return inventoryNoteId;
}

template <typename NotesVec>
inline int filteredDisplayNoteIndexForNoteIdAndStart(const NotesVec& filtered, NoteId noteId,
                                                     uint32_t bracketDisplayTick,
                                                     uint32_t loopStartTick = 0,
                                                     uint32_t loopLength = 0) {
  if (noteId == kInvalidNoteId) {
    return -1;
  }
  for (int i = 0; i < static_cast<int>(filtered.size()); ++i) {
    const NoteUtils::DisplayNote& dn = filtered[static_cast<size_t>(i)];
    if (dn.noteId != noteId) {
      continue;
    }
    const uint32_t displayStart =
        displayStartTickFromStorageNote(dn.startTick, loopStartTick, loopLength);
    if (displayStart == bracketDisplayTick) {
      return i;
    }
  }
  return -1;
}

template <typename NotesVec>
inline int filteredDisplayNoteIndexForNoteIdAndEnd(const NotesVec& filtered, NoteId noteId,
                                                   uint32_t bracketDisplayTick,
                                                   uint32_t loopStartTick = 0,
                                                   uint32_t loopLength = 0) {
  if (noteId == kInvalidNoteId) {
    return -1;
  }
  for (int i = 0; i < static_cast<int>(filtered.size()); ++i) {
    const NoteUtils::DisplayNote& dn = filtered[static_cast<size_t>(i)];
    if (dn.noteId != noteId) {
      continue;
    }
    const uint32_t displayEnd =
        displayStartTickFromStorageNote(dn.endTick, loopStartTick, loopLength);
    if (displayEnd == bracketDisplayTick) {
      return i;
    }
  }
  return -1;
}

template <typename NotesVec>
inline int filteredDisplayNoteIndexForMovingNote(const NotesVec& filtered, NoteId noteId,
                                                 uint32_t linearStartTick,
                                                 uint32_t loopStartTick = 0,
                                                 uint32_t loopLength = 0) {
  const uint32_t displayBracket =
      loopLength > 0 ? displayStartTickFromStorageNote(linearStartTick, loopStartTick, loopLength)
                     : linearStartTick;
  return filteredDisplayNoteIndexForNoteIdAndStart(filtered, noteId, displayBracket,
                                                   loopStartTick, loopLength);
}
