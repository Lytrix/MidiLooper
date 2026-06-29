//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "Utils/MidiButtonConfig.h"

namespace SetBrowserOverlayPolicy {

enum class Mode : uint8_t {
  Root = 0,
  DirtyPrompt,
  RevisionHistory,
  LoopPick,
  MinimalLoading,
};

enum class EntryKind : uint8_t {
  WorkspaceSetBrowser = 0,
  LoopManagement,
};

struct NavigationState {
  Mode drillMode = Mode::Root;
  Mode parentDrillMode = Mode::Root;
  uint16_t drilledSetId = 0;
  uint8_t parentListSelection = 0;
  uint8_t parentListScrollOffset = 0;
  EntryKind entryKind = EntryKind::WorkspaceSetBrowser;
};

enum class PersistencePhase : uint8_t {
  Idle = 0,
  AwaitingCommitThenLoad,
  LoadInProgress,
  CommitOnlyBackground,
};

/// Transient modes (dirty prompt, minimal loading) take priority over drill navigation.
Mode resolveActiveMode(const NavigationState& nav, bool dirtyPromptActive,
                       bool minimalLoadingActive);

/// True when overlay should show MINIMAL_LOADING (overlay must be open).
bool isMinimalLoadingOverlayActive(PersistencePhase phase, bool overlayOpen,
                                   bool commitPending, bool commitInProgress);

/// True when a new revision load request must be rejected.
bool isOverlayLoadRequestBlocked(PersistencePhase phase, bool loadPending,
                                 bool loadInProgress);

/// True when overlay enter should preserve drill navigation (pipeline still running).
bool shouldPreserveOverlayNavigationOnEnter(PersistencePhase phase);

void resetNavigation(NavigationState& nav);

void setEntryKind(NavigationState& nav, EntryKind kind);

/// Drill into revision list for one Set; preserves parent list focus for back navigation.
void openRevisionHistory(NavigationState& nav, uint16_t setId, uint8_t listSelection,
                         uint8_t listScrollOffset);

/// Drill into loop list for one Set; preserves parent list focus for back navigation.
void openLoopPick(NavigationState& nav, uint16_t setId, uint8_t listSelection,
                  uint8_t listScrollOffset);

/// Return one drill level; false when already at ROOT.
bool navigateBack(NavigationState& nav, uint8_t& outListSelection, uint8_t& outListScrollOffset);

bool isDrillMode(Mode mode);

/// Root workspace Set browser list: Save, Current, then Set rows.
constexpr uint8_t kRootSaveRowIndex = 0;
constexpr uint8_t kRootCurrentRowIndex = 1;
constexpr uint8_t kRootFirstSetRowIndex = 2;

size_t rootWorkspaceListRowCount(size_t setCount);
bool isRootSaveRow(uint8_t listSelection);
bool isRootCurrentRow(uint8_t listSelection);
/// Set folder list index when `listSelection` is a Set row; otherwise SIZE_MAX.
size_t rootSetFolderListIndex(uint8_t listSelection);

/// True when load/save overlay is active and global handlers must not run this MIDI action.
bool shouldSuppressGlobalMidiAction(MidiButtonConfig::ActionType actionType);

/// Load/save overlay remaps record / track short presses (scroll only; confirm is edit mode
/// short via MidiButtonManager or GPIO encoder short, not NOTELEN).
enum class LoadSaveOverlayInputAction : uint8_t {
  None = 0,
  ScrollDown,
  ScrollUp,
};

LoadSaveOverlayInputAction mapLoadSaveOverlayInputAction(
    MidiButtonConfig::ActionType actionType);

/// True when GPIO encoder rotation / pitch-edit hold must not drive note edit.
bool shouldSuppressNoteEditEncoderInput();

}  // namespace SetBrowserOverlayPolicy
