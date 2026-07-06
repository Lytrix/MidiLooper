//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "Utils/DiagnosticsTypes.h"

namespace Diagnostics {

namespace Memory {
constexpr uint16_t HeapSnapshot = makeEventId(Category::Memory, 1);
constexpr uint16_t AllocationFailure = makeEventId(Category::Memory, 2);
constexpr uint16_t HeapReserveReached = makeEventId(Category::Memory, 3);
}  // namespace Memory

namespace Playback {
constexpr uint16_t MergeBegin = makeEventId(Category::Playback, 1);
constexpr uint16_t MergeComplete = makeEventId(Category::Playback, 2);
constexpr uint16_t PlaybackStart = makeEventId(Category::Playback, 3);
}  // namespace Playback

namespace Edit {
constexpr uint16_t NoteEditOpenEnter = makeEventId(Category::Edit, 1);
constexpr uint16_t AfterRematerializeEditView = makeEventId(Category::Edit, 2);
constexpr uint16_t AfterAssignNoteIds = makeEventId(Category::Edit, 3);
constexpr uint16_t AfterDiscardFlatCache = makeEventId(Category::Edit, 4);
constexpr uint16_t AfterEnterDefaultState = makeEventId(Category::Edit, 5);
constexpr uint16_t NoteEditOpenExit = makeEventId(Category::Edit, 6);
constexpr uint16_t UndoPush = makeEventId(Category::Edit, 7);
}  // namespace Edit

namespace Storage {
constexpr uint16_t SaveBegin = makeEventId(Category::Storage, 1);
constexpr uint16_t SaveComplete = makeEventId(Category::Storage, 2);
constexpr uint16_t LoadWorkspace = makeEventId(Category::Storage, 3);
}  // namespace Storage

namespace Display {
constexpr uint16_t VisualCacheRebuild = makeEventId(Category::Display, 1);
constexpr uint16_t DisplayFrame = makeEventId(Category::Display, 2);
}  // namespace Display

namespace Validation {
constexpr uint16_t InvariantPassed = makeEventId(Category::Validation, 1);
constexpr uint16_t InvariantFailed = makeEventId(Category::Validation, 2);
}  // namespace Validation

}  // namespace Diagnostics
