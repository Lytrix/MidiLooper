//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <vector>

#include "MidiEvent.h"
#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"
#include "Utils/InternalHeapFirstAllocator.h"

enum class InteractionType : uint8_t {
  OverlapNoteOn,
  OverlapNoteOff,
  CompleteCover,
  BoundaryTouch,
};

struct EditSessionInteraction {
  InteractionType type = InteractionType::BoundaryTouch;
  NoteId targetNoteId = kInvalidNoteId;
  NoteId causingNoteId = kInvalidNoteId;
  NoteBaseline baselineSpan{};
  NoteBaseline causingSpan{};
};

struct TargetNoteInteractionGroup {
  NoteId targetNoteId = kInvalidNoteId;
  std::vector<EditSessionInteraction, InternalHeapFirstAllocator<EditSessionInteraction>> incoming;
};

struct EditSessionInteractionsByTarget {
  std::vector<TargetNoteInteractionGroup, InternalHeapFirstAllocator<TargetNoteInteractionGroup>>
      groups;
};

struct ConstrainedNoteGeometry {
  NoteId noteId = kInvalidNoteId;
  bool visible = true;
  uint32_t startTick = 0;
  uint32_t endTick = 0;
  uint8_t pitch = 0;
};

struct EditedNoteSpan {
  NoteId noteId = kInvalidNoteId;
  NoteBaseline span{};
};

struct EditedGeometry {
  EditorSelection selection{};
  std::vector<EditedNoteSpan, InternalHeapFirstAllocator<EditedNoteSpan>> causingSpans;
};

struct CausingTargetPair {
  NoteId causingNoteId = kInvalidNoteId;
  NoteId targetNoteId = kInvalidNoteId;
};

enum class EditSessionActionType : uint8_t {
  RestoreNote,
  ShortenNote,
  HideNote,
  MoveNote,
  ChangeLength,
  ChangePitch,
};

struct EditSessionAction {
  EditSessionActionType type = EditSessionActionType::MoveNote;
  NoteId targetNoteId = kInvalidNoteId;
  uint32_t startTick = 0;
  uint32_t endTick = 0;
  uint8_t pitch = 0;
  uint8_t velocity = 64;
};

using EditSessionActions =
    std::vector<EditSessionAction, InternalHeapFirstAllocator<EditSessionAction>>;
