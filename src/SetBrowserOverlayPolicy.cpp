//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "SetBrowserOverlayPolicy.h"

namespace SetBrowserOverlayPolicy {

Mode resolveActiveMode(const NavigationState& nav, bool dirtyPromptActive,
                       bool minimalLoadingActive) {
  if (dirtyPromptActive) {
    return Mode::DirtyPrompt;
  }
  if (minimalLoadingActive) {
    return Mode::MinimalLoading;
  }
  return nav.drillMode;
}

bool isMinimalLoadingOverlayActive(PersistencePhase phase, bool overlayOpen,
                                   bool commitPending, bool commitInProgress) {
  if (!overlayOpen) {
    return false;
  }
  if (phase == PersistencePhase::AwaitingCommitThenLoad ||
      phase == PersistencePhase::LoadInProgress) {
    return true;
  }
  if (phase == PersistencePhase::CommitOnlyBackground &&
      (commitPending || commitInProgress)) {
    return true;
  }
  return false;
}

bool isOverlayLoadRequestBlocked(PersistencePhase phase, bool loadPending,
                                 bool loadInProgress) {
  if (loadPending || loadInProgress) {
    return true;
  }
  return phase == PersistencePhase::AwaitingCommitThenLoad ||
         phase == PersistencePhase::LoadInProgress;
}

bool shouldPreserveOverlayNavigationOnEnter(PersistencePhase phase) {
  return phase != PersistencePhase::Idle;
}

void resetNavigation(NavigationState& nav) {
  nav.drillMode = Mode::Root;
  nav.parentDrillMode = Mode::Root;
  nav.drilledSetId = 0;
  nav.parentListSelection = 0;
  nav.parentListScrollOffset = 0;
  nav.entryKind = EntryKind::WorkspaceSetBrowser;
}

void setEntryKind(NavigationState& nav, EntryKind kind) {
  nav.entryKind = kind;
}

void openRevisionHistory(NavigationState& nav, uint16_t setId, uint8_t listSelection,
                         uint8_t listScrollOffset) {
  nav.parentDrillMode = nav.drillMode;
  nav.parentListSelection = listSelection;
  nav.parentListScrollOffset = listScrollOffset;
  nav.drilledSetId = setId;
  nav.drillMode = Mode::RevisionHistory;
}

void openLoopPick(NavigationState& nav, uint16_t setId, uint8_t listSelection,
                  uint8_t listScrollOffset) {
  nav.parentDrillMode = nav.drillMode;
  nav.parentListSelection = listSelection;
  nav.parentListScrollOffset = listScrollOffset;
  nav.drilledSetId = setId;
  nav.drillMode = Mode::LoopPick;
}

bool navigateBack(NavigationState& nav, uint8_t& outListSelection, uint8_t& outListScrollOffset) {
  if (nav.drillMode == Mode::Root) {
    return false;
  }
  outListSelection = nav.parentListSelection;
  outListScrollOffset = nav.parentListScrollOffset;
  nav.drillMode = nav.parentDrillMode;
  nav.parentDrillMode = Mode::Root;
  nav.drilledSetId = 0;
  return true;
}

bool isDrillMode(Mode mode) {
  return mode == Mode::RevisionHistory || mode == Mode::LoopPick;
}

size_t rootWorkspaceListRowCount(size_t setCount) {
  return kRootFirstSetRowIndex + setCount;
}

bool isRootSaveRow(uint8_t listSelection) {
  return listSelection == kRootSaveRowIndex;
}

bool isRootCurrentRow(uint8_t listSelection) {
  return listSelection == kRootCurrentRowIndex;
}

size_t rootSetFolderListIndex(uint8_t listSelection) {
  if (listSelection < kRootFirstSetRowIndex) {
    return static_cast<size_t>(-1);
  }
  return static_cast<size_t>(listSelection - kRootFirstSetRowIndex);
}

bool shouldSuppressGlobalMidiAction(MidiButtonConfig::ActionType actionType) {
  using ActionType = MidiButtonConfig::ActionType;
  switch (actionType) {
    case ActionType::NONE:
    case ActionType::TOGGLE_PLAY:
    case ActionType::TOGGLE_LOAD_SAVE_MODE:
    case ActionType::TOGGLE_TRANSPORT:
    case ActionType::RESET_TO_LOOP_START:
    case ActionType::MUTE_TRACK:
    case ActionType::SOLO_TRACK:
    case ActionType::CENTER_DETAILED_WINDOW_ON_PLAYHEAD:
      return false;
    default:
      return true;
  }
}

LoadSaveOverlayInputAction mapLoadSaveOverlayInputAction(
    MidiButtonConfig::ActionType actionType) {
  using ActionType = MidiButtonConfig::ActionType;
  switch (actionType) {
    case ActionType::TOGGLE_RECORD:
      return LoadSaveOverlayInputAction::ScrollDown;
    case ActionType::SELECT_TRACK:
      return LoadSaveOverlayInputAction::ScrollUp;
    default:
      return LoadSaveOverlayInputAction::None;
  }
}

bool shouldSuppressNoteEditEncoderInput() {
  return true;
}

}  // namespace SetBrowserOverlayPolicy
