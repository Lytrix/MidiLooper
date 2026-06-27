//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

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

/// Transient modes (dirty prompt, minimal loading) take priority over drill navigation.
Mode resolveActiveMode(const NavigationState& nav, bool dirtyPromptActive,
                       bool minimalLoadingActive);

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

}  // namespace SetBrowserOverlayPolicy
