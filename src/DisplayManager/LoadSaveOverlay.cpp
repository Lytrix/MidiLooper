//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Load/save set browser overlay: list caches, navigation, and draw views.

#include "DisplayManager.h"
#include "DisplayManagerInternal.h"

#include "Globals.h"
#include "LooperState.h"
#include "RevisionLoadPolicy.h"
#include "RtcTime.h"
#include "SSD1322_Config.h"
#include "SetBrowserOverlayPolicy.h"
#include "SetRevisionCatalog.h"
#include "StorageManager.h"
#include "Utils/DebugSessionCapture.h"
#include <Font5x7Fixed.h>
#include <Font5x7FixedMono.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

constexpr int kSaveStatusDotY = 47;

constexpr int kLoadSaveDetailLabelChars = 6;  // "Tracks" — fixed column for value alignment
constexpr int kLoadSaveDetailCharWidth = 6;
constexpr int kLoadSaveDetailColonWidth = 8;
constexpr uint8_t kLoadSaveDetailLabelBrightness = 3;  // matches drawInfoField label (5/3+2)
constexpr uint8_t kLoadSaveDetailColonBrightness = 4;
constexpr uint8_t kLoadSaveDetailValueBrightness = 5;
constexpr int kLoadSaveDetailRightMargin = 1;
constexpr int kLoadSaveDetailDateRightMargin = 0;
constexpr int kLoadSaveDetailBpmValueChars = 3;
constexpr int kLoadSaveDetailRightLabelChars = 4;  // "Bars"
constexpr int kLoadSaveLeftPadding = 2;
constexpr int kLoadSaveDetailLeftPadding = 6;  // px gap after centre divider column
constexpr int kLoadSaveTextLineStep = 9;       // 7px font + 1px descender + 1px below
constexpr int kLoadSaveDetailContentTopY = kLoadSaveTextLineStep;  // align with list rows; y=0 clipped
constexpr int kLoadSaveTrackRowGap = 1;
constexpr int kLoadSaveSlotHorizontalGap = 4;
constexpr int kLoadSaveDetailDateLineCount = 3;
constexpr int kLoadSaveDetailDateColumnChars = 5;  // "31 AUG", "13:30"
constexpr int kLoadSaveDetailDateColumnGap = 6;    // match kLoadSaveDetailLeftPadding
constexpr int kLoadSaveDetailUnsavedMarkerSize = 2;
constexpr int kLoadSaveDetailUnsavedMarkerGap = 2;
constexpr int kLoadSaveDetailFontHeight = 7;

void drawLoadSaveDetailUnsavedMarker(SSD1322& display, int labelX, int metricBottomY) {
    const int squareLeftX = labelX - kLoadSaveDetailUnsavedMarkerGap - kLoadSaveDetailUnsavedMarkerSize;
    // draw_text y is the font bottom row; center the marker on the cap height above it.
    const int textCenterY = metricBottomY - kLoadSaveDetailFontHeight / 2;
    const int squareTopY = textCenterY - kLoadSaveDetailUnsavedMarkerSize / 2;
    display.gfx.draw_rect_filled(display.api.getFrameBuffer(), squareLeftX, squareTopY,
                                 squareLeftX + kLoadSaveDetailUnsavedMarkerSize - 1,
                                 squareTopY + kLoadSaveDetailUnsavedMarkerSize - 1,
                                 kLoadSaveDetailValueBrightness);
}

constexpr int loadSaveDividerX() { return DISPLAY_WIDTH / 2; }

constexpr int loadSaveDetailContentX() {
    return loadSaveDividerX() + 1 + kLoadSaveDetailLeftPadding;
}

constexpr int loadSaveDetailLeftColonX(int detailX) {
    return detailX + kLoadSaveDetailLabelChars * kLoadSaveDetailCharWidth;
}

constexpr int loadSaveDetailRightColonX() {
    const int detailRightX = DISPLAY_WIDTH - kLoadSaveDetailRightMargin;
    const int valueX = detailRightX - kLoadSaveDetailBpmValueChars * kLoadSaveDetailCharWidth;
    return valueX - kLoadSaveDetailColonWidth;
}

constexpr int loadSaveDetailDateLineY(int lineIndex) {
    // Bottom line at DISPLAY_HEIGHT — same baseline as the main note info strip.
    return DISPLAY_HEIGHT -
           (kLoadSaveDetailDateLineCount - 1 - lineIndex) * kLoadSaveTextLineStep;
}

constexpr int loadSaveDetailDateTextLeftX() {
    const int detailRightX = DISPLAY_WIDTH - 1 - kLoadSaveDetailDateRightMargin;
    return detailRightX - kLoadSaveDetailDateColumnChars * kLoadSaveDetailCharWidth + 1;
}

constexpr int loadSaveDetailSlotBarWidth(int detailX) {
    const int slotBarRightX = loadSaveDetailDateTextLeftX() - kLoadSaveDetailDateColumnGap;
    return slotBarRightX - detailX;
}

constexpr int loadSaveBrowserMaxTextChars() {
    const int maxPixelX = loadSaveDividerX() - 1 - kLoadSaveDetailLeftPadding;
    const int availablePixels = maxPixelX - kLoadSaveLeftPadding + 1;
    return std::max(1, availablePixels / kLoadSaveDetailCharWidth);
}

void copyLoadSaveBrowserLabel(char* dest, size_t destSize, const char* source) {
    if (destSize == 0) {
        return;
    }
    const int maxChars = loadSaveBrowserMaxTextChars();
    if (source == nullptr) {
        dest[0] = '\0';
        return;
    }
    size_t copyLen = std::strlen(source);
    if (static_cast<int>(copyLen) > maxChars) {
        copyLen = static_cast<size_t>(maxChars);
    }
    std::memcpy(dest, source, copyLen);
    dest[copyLen] = '\0';
}

}  // namespace

void DisplayManager::drawLoadSaveRowLoadStatusDots(int labelLeftX, int labelCharCount, int rowY,
                                                   uint32_t nowMs, uint16_t setId,
                                                   uint16_t revisionId) {
    if (setId == 0) {
        return;
    }
    if (StorageManager::getRevisionLoadDisplayTargetSetId() != setId) {
        return;
    }
    if (revisionId != 0 &&
        StorageManager::getRevisionLoadDisplayTargetRevisionId() != revisionId) {
        return;
    }
    const DeferredSaveDisplayStatus loadStatus =
        StorageManager::getDeferredLoadDisplayStatus(nowMs);
    if (loadStatus.phase == DeferredSaveDisplayPhase::Idle) {
        return;
    }
    constexpr int kDotGapAfterLabel = 2;
    const int dotsStartX =
        labelLeftX + labelCharCount * kLoadSaveDetailCharWidth + kDotGapAfterLabel;
    const int dotY = rowY - 4;
    drawPersistenceStatusDots(dotsStartX, dotY, loadStatus);
}

void DisplayManager::refreshLoadSaveListCache() {
    if (!StorageManager::isOverlayCatalogReadAllowed()) {
        return;
    }
    loadSaveListCount_ = StorageManager::listSetRevisionBrowserEntries(loadSaveListEntries_,
                                                                       kLoadSaveListCapacity);
    loadSaveListCacheValid_ = true;
}

void DisplayManager::invalidateLoadSaveRevisionListCache() {
    loadSaveRevisionListCacheValid_ = false;
    loadSaveRevisionListCount_ = 0;
    loadSaveRevisionListSetId_ = 0;
}

void DisplayManager::refreshLoadSaveRevisionHistoryCache(uint16_t setId, bool forceCatalogRead) {
    if (!forceCatalogRead && !StorageManager::isOverlayCatalogReadAllowed()) {
        return;
    }
    loadSaveRevisionListSetId_ = setId;
    loadSaveRevisionListCount_ = StorageManager::listSetRevisionHistoryEntries(
        setId, loadSaveRevisionListEntries_, kLoadSaveListCapacity);
    loadSaveRevisionListCacheValid_ = true;
}

void DisplayManager::ensureLoadSaveRevisionListCache(uint16_t setId, bool forceCatalogRead) {
    if (setId == 0) {
        return;
    }
    if (forceCatalogRead || !loadSaveRevisionListCacheValid_ ||
        loadSaveRevisionListSetId_ != setId) {
        refreshLoadSaveRevisionHistoryCache(setId, forceCatalogRead);
    }
}

uint16_t DisplayManager::resolveFocusedRootSetId() const {
    const size_t savedIndex =
        SetBrowserOverlayPolicy::rootSetFolderListIndex(loadSaveListSelection_);
    if (savedIndex >= loadSaveListCount_) {
        return 0;
    }
    uint16_t setId = 0;
    if (!SetRevisionCatalog::parseSetIdFromFolderName(loadSaveListEntries_[savedIndex].folderName,
                                                      setId)) {
        return 0;
    }
    return setId;
}

uint16_t DisplayManager::resolveFocusedRevisionId() const {
    if (!loadSaveRevisionListCacheValid_ ||
        loadSaveListSelection_ >= loadSaveRevisionListCount_) {
        return 0;
    }
    return loadSaveRevisionListEntries_[loadSaveListSelection_].revisionId;
}

void DisplayManager::invalidateLoadSaveDetailCache() {
    loadSaveDetailCacheValid_ = false;
}

bool DisplayManager::resolveLoadSaveWorkspaceDetail(LoadSaveWorkspaceDetailParams& out) {
    const StorageManager::SetBrowserOverlayMode overlayMode =
        StorageManager::getSetBrowserOverlayMode();
    const SetBrowserOverlayPolicy::Mode policyMode =
        static_cast<SetBrowserOverlayPolicy::Mode>(overlayMode);

    LoadSaveDetailCacheKey key{};
    key.mode = policyMode;
    key.listSelection = loadSaveListSelection_;
    key.drilledSetId = StorageManager::getSetBrowserOverlayDrilledSetId();
    if (policyMode == SetBrowserOverlayPolicy::Mode::Root) {
        const size_t savedIndex =
            SetBrowserOverlayPolicy::rootSetFolderListIndex(loadSaveListSelection_);
        if (savedIndex < loadSaveListCount_) {
            std::snprintf(key.setFolderName, sizeof(key.setFolderName), "%s",
                          loadSaveListEntries_[savedIndex].folderName);
        }
    }

    if (loadSaveDetailCacheValid_ && loadSaveDetailCacheKey_.mode == key.mode &&
        loadSaveDetailCacheKey_.listSelection == key.listSelection &&
        loadSaveDetailCacheKey_.drilledSetId == key.drilledSetId &&
        std::strcmp(loadSaveDetailCacheKey_.setFolderName, key.setFolderName) == 0) {
        out = loadSaveDetailCache_;
        return loadSaveDetailCacheOk_;
    }

    const bool overlayCatalogReadAllowed = StorageManager::isOverlayCatalogReadAllowed();
    if (!overlayCatalogReadAllowed && loadSaveDetailCacheValid_) {
        out = loadSaveDetailCache_;
        return loadSaveDetailCacheOk_;
    }
    if (!overlayCatalogReadAllowed) {
        out = {};
        return false;
    }

    loadSaveDetailCacheKey_ = key;
    loadSaveDetailCache_ = {};
    bool detailOk = false;

    if (policyMode == SetBrowserOverlayPolicy::Mode::RevisionHistory) {
        const uint16_t setId = key.drilledSetId;
        const uint16_t revisionId = resolveFocusedRevisionId();
        loadSaveDetailCache_.setId = setId;
        loadSaveDetailCache_.revisionId = revisionId;
        if (setId != 0 && revisionId != 0) {
            detailOk = StorageManager::readSetRevisionHistoryBrowserMetadata(
                setId, revisionId, loadSaveDetailCache_.metrics,
                loadSaveDetailCache_.timestampUnix);
        }
    } else if (policyMode == SetBrowserOverlayPolicy::Mode::Root) {
        const bool isSaveRow = SetBrowserOverlayPolicy::isRootSaveRow(loadSaveListSelection_);
        const bool isCurrentRow =
            SetBrowserOverlayPolicy::isRootCurrentRow(loadSaveListSelection_);
        if (isSaveRow || isCurrentRow) {
            detailOk = StorageManager::readCurrentSetBrowserMetadata(loadSaveDetailCache_.metrics);
            loadSaveDetailCache_.timestampUnix = loadSaveDetailCache_.metrics.createdAtUnix;
            loadSaveDetailCache_.setId = StorageManager::getCurrentWorkspaceDerivedSetId();
            loadSaveDetailCache_.revisionId =
                StorageManager::getCurrentWorkspaceDerivedRevisionId();
            if (isCurrentRow && StorageManager::isCurrentWorkspaceDirty()) {
                loadSaveDetailCache_.markRevisionUnsaved = true;
            }
        } else if (key.setFolderName[0] != '\0') {
            detailOk = StorageManager::readSetRevisionCatalogBrowserMetadata(
                key.setFolderName, loadSaveDetailCache_.metrics, loadSaveDetailCache_.setId,
                loadSaveDetailCache_.revisionId, loadSaveDetailCache_.timestampUnix);
        }
    }

    loadSaveDetailCacheValid_ = true;
    loadSaveDetailCacheOk_ = detailOk;
    out = loadSaveDetailCache_;
    return detailOk;
}

void DisplayManager::handleLoadSaveOverlayPress(LoadSaveOverlayPressType pressType) {
    if (!looperState.isLoadSaveModeActive()) {
        return;
    }

    const StorageManager::SetBrowserOverlayMode overlayMode =
        StorageManager::getSetBrowserOverlayMode();
    if (overlayMode == StorageManager::SetBrowserOverlayMode::MinimalLoading) {
        return;
    }

    if (overlayMode == StorageManager::SetBrowserOverlayMode::DirtyPrompt) {
        if (pressType == LoadSaveOverlayPressType::Short) {
            confirmLoadSaveFocusedRow();
        } else if (pressType == LoadSaveOverlayPressType::Long) {
            StorageManager::cancelRevisionLoadRequest();
            looperState.exitLoadSaveMode();
        }
        return;
    }

    if (overlayMode == StorageManager::SetBrowserOverlayMode::RevisionHistory) {
        const uint16_t setId = StorageManager::getSetBrowserOverlayDrilledSetId();
        switch (pressType) {
            case LoadSaveOverlayPressType::Short: {
                ensureLoadSaveRevisionListCache(setId, true);
                const uint16_t revisionId = resolveFocusedRevisionId();
                if (revisionId != 0) {
                    StorageManager::requestLoadRevision(setId, revisionId);
                }
                break;
            }
            case LoadSaveOverlayPressType::Long: {
                uint8_t parentSelection = 0;
                uint8_t parentScroll = 0;
                if (StorageManager::navigateSetBrowserOverlayBack(parentSelection,
                                                                  parentScroll)) {
                    loadSaveListSelection_ = parentSelection;
                    loadSaveListScrollOffset_ = parentScroll;
                    invalidateLoadSaveRevisionListCache();
                    invalidateLoadSaveDetailCache();
                }
                break;
            }
            default:
                break;
        }
        return;
    }

    if (overlayMode != StorageManager::SetBrowserOverlayMode::Root) {
        return;
    }

    const bool isSaveRow = SetBrowserOverlayPolicy::isRootSaveRow(loadSaveListSelection_);
    const uint16_t focusedSetId = resolveFocusedRootSetId();
    const bool isSetRow = focusedSetId != 0;

    switch (pressType) {
        case LoadSaveOverlayPressType::Short:
            if (isSaveRow) {
                confirmLoadSaveFocusedRow();
            } else if (isSetRow) {
                StorageManager::requestLoadLatestRevisionForSet(focusedSetId);
            }
            break;
        case LoadSaveOverlayPressType::Double:
            if (isSetRow) {
                StorageManager::toggleSetRevisionCatalogFavorite(focusedSetId);
            }
            break;
        case LoadSaveOverlayPressType::Long:
            if (isSetRow) {
                StorageManager::openSetBrowserRevisionHistory(focusedSetId, loadSaveListSelection_,
                                                              loadSaveListScrollOffset_);
                loadSaveRevisionListCacheValid_ = false;
                invalidateLoadSaveDetailCache();
                loadSaveListSelection_ = 0;
                loadSaveListScrollOffset_ = 0;
                ensureLoadSaveRevisionListCache(focusedSetId, true);
#if defined(SESSION_CAPTURE)
                SC_OVERLAY_SEL(static_cast<int>(StorageManager::SetBrowserOverlayMode::RevisionHistory),
                               loadSaveListSelection_);
#endif
            } else {
                looperState.exitLoadSaveMode();
            }
            break;
    }
}

void DisplayManager::adjustLoadSaveListSelection(int delta) {
    if (delta == 0) {
        return;
    }
    if (StorageManager::getSetBrowserOverlayMode() ==
        StorageManager::SetBrowserOverlayMode::DirtyPrompt) {
        StorageManager::adjustRevisionLoadDirtyPromptSelection(delta);
        return;
    }
    if (StorageManager::getSetBrowserOverlayMode() ==
        StorageManager::SetBrowserOverlayMode::MinimalLoading) {
        return;
    }
    if (SetBrowserOverlayPolicy::isDrillMode(StorageManager::getSetBrowserOverlayMode())) {
        adjustLoadSaveListSelectionInDrillMode(delta);
        return;
    }
    const size_t totalRows =
        SetBrowserOverlayPolicy::rootWorkspaceListRowCount(loadSaveListCount_);
    if (totalRows == 0) {
        loadSaveListSelection_ = 0;
        return;
    }
    int next = static_cast<int>(loadSaveListSelection_) + delta;
    if (next < 0) {
        next = 0;
    } else if (static_cast<size_t>(next) >= totalRows) {
        next = static_cast<int>(totalRows) - 1;
    }
    loadSaveListSelection_ = static_cast<uint8_t>(next);
    invalidateLoadSaveDetailCache();

    constexpr int kListRowsStartY = kLoadSaveTextLineStep;
    constexpr int kListVisibleRows = (DISPLAY_HEIGHT - kListRowsStartY) / kLoadSaveTextLineStep;
    if (loadSaveListSelection_ < loadSaveListScrollOffset_) {
        loadSaveListScrollOffset_ = loadSaveListSelection_;
    } else if (loadSaveListSelection_ >= loadSaveListScrollOffset_ + kListVisibleRows) {
        loadSaveListScrollOffset_ =
            loadSaveListSelection_ - static_cast<uint8_t>(kListVisibleRows - 1);
    }
#if defined(SESSION_CAPTURE)
    SC_OVERLAY_SEL(0, loadSaveListSelection_);
#endif
}

void DisplayManager::adjustLoadSaveListSelectionInDrillMode(int delta) {
    if (delta == 0) {
        return;
    }
    const StorageManager::SetBrowserOverlayMode overlayMode =
        StorageManager::getSetBrowserOverlayMode();
    if (overlayMode != StorageManager::SetBrowserOverlayMode::RevisionHistory) {
        return;
    }

    const uint16_t setId = StorageManager::getSetBrowserOverlayDrilledSetId();
    if (!loadSaveRevisionListCacheValid_ || loadSaveRevisionListSetId_ != setId) {
        refreshLoadSaveRevisionHistoryCache(setId);
    }

    const size_t totalRows = loadSaveRevisionListCount_;
    if (totalRows == 0) {
        loadSaveListSelection_ = 0;
        return;
    }
    int next = static_cast<int>(loadSaveListSelection_) + delta;
    if (next < 0) {
        next = 0;
    } else if (static_cast<size_t>(next) >= totalRows) {
        next = static_cast<int>(totalRows) - 1;
    }
    loadSaveListSelection_ = static_cast<uint8_t>(next);
    invalidateLoadSaveDetailCache();

    constexpr int kListRowsStartY = kLoadSaveTextLineStep;
    constexpr int kListVisibleRows = (DISPLAY_HEIGHT - kListRowsStartY) / kLoadSaveTextLineStep;
    if (loadSaveListSelection_ < loadSaveListScrollOffset_) {
        loadSaveListScrollOffset_ = loadSaveListSelection_;
    } else if (loadSaveListSelection_ >= loadSaveListScrollOffset_ + kListVisibleRows) {
        loadSaveListScrollOffset_ =
            loadSaveListSelection_ - static_cast<uint8_t>(kListVisibleRows - 1);
    }
#if defined(SESSION_CAPTURE)
    SC_OVERLAY_SEL(static_cast<int>(StorageManager::SetBrowserOverlayMode::RevisionHistory),
                   loadSaveListSelection_);
#endif
}

void DisplayManager::confirmLoadSaveFocusedRow() {
    if (!looperState.isLoadSaveModeActive()) {
        return;
    }

    const StorageManager::SetBrowserOverlayMode overlayMode =
        StorageManager::getSetBrowserOverlayMode();
    if (overlayMode == StorageManager::SetBrowserOverlayMode::DirtyPrompt) {
        const uint8_t selection = StorageManager::getRevisionLoadDirtyPromptSelection();
#if defined(SESSION_CAPTURE)
        SC_OVERLAY_CONFIRM(1, selection);
#endif
        switch (selection) {
            case 0:
                StorageManager::confirmRevisionLoadAfterCommit();
                break;
            case 1:
                StorageManager::confirmRevisionLoadDiscardWorkspace();
                break;
            default:
                StorageManager::cancelRevisionLoadRequest();
                break;
        }
        return;
    }
    if (overlayMode == StorageManager::SetBrowserOverlayMode::RevisionHistory) {
        const uint16_t setId = StorageManager::getSetBrowserOverlayDrilledSetId();
        ensureLoadSaveRevisionListCache(setId, true);
        const uint16_t revisionId = resolveFocusedRevisionId();
        if (revisionId != 0) {
#if defined(SESSION_CAPTURE)
            SC_OVERLAY_CONFIRM(
                static_cast<int>(StorageManager::SetBrowserOverlayMode::RevisionHistory),
                loadSaveListSelection_);
#endif
            StorageManager::requestLoadRevision(setId, revisionId);
        } else {
#if defined(SESSION_CAPTURE)
            SC_PERSIST("rev_overlay_load_skip", 0, setId, loadSaveListSelection_,
                       loadSaveRevisionListCount_ > 0 ? "bad_selection" : "empty_list");
#endif
        }
        return;
    }
    if (overlayMode != StorageManager::SetBrowserOverlayMode::Root) {
        return;
    }

    if (SetBrowserOverlayPolicy::isRootSaveRow(loadSaveListSelection_)) {
#if defined(SESSION_CAPTURE)
        SC_OVERLAY_CONFIRM(0, loadSaveListSelection_);
#endif
        StorageManager::beginOverlaySaveRowCommit();
        looperState.exitLoadSaveMode();
#if defined(SESSION_CAPTURE)
        SC_PERSIST("rev_overlay_save", 0, 0, 0, "queued_exit");
#endif
        return;
    }

    const uint16_t setId = resolveFocusedRootSetId();
    if (setId != 0) {
#if defined(SESSION_CAPTURE)
        SC_OVERLAY_CONFIRM(0, loadSaveListSelection_);
#endif
        StorageManager::requestLoadLatestRevisionForSet(setId);
    }
}

#if defined(SESSION_CAPTURE)
void DisplayManager::openRevisionHistoryFromHitl(uint16_t setId) {
    if (!looperState.isLoadSaveModeActive() || setId == 0) {
        return;
    }
    StorageManager::openSetBrowserRevisionHistory(setId, loadSaveListSelection_,
                                                  loadSaveListScrollOffset_);
    invalidateLoadSaveRevisionListCache();
    loadSaveListSelection_ = 0;
    loadSaveListScrollOffset_ = 0;
    SC_OVERLAY_SEL(static_cast<int>(StorageManager::SetBrowserOverlayMode::RevisionHistory),
                   loadSaveListSelection_);
}

void DisplayManager::navigateLoadSaveOverlayBackFromHitl() {
    if (!looperState.isLoadSaveModeActive()) {
        return;
    }
    handleLoadSaveOverlayPress(LoadSaveOverlayPressType::Long);
}
#endif

void DisplayManager::drawLoadSaveTrackFilledBar(int x, int y, int barWidth, int barHeight,
                                                uint8_t filledSlots, uint8_t maxSlots,
                                                uint8_t brightness) {
    constexpr int kSegments = Config::MAX_LOOPS_PER_TRACK;
    if (maxSlots == 0 || barWidth < kSegments || barHeight < 1) {
        return;
    }
    const int totalGaps = kSegments - 1;
    const int fullSegmentWidth =
        (barWidth - totalGaps * kLoadSaveSlotHorizontalGap) / kSegments;
    const int segmentWidth = std::max(1, fullSegmentWidth / 2);
    const int segmentPitch = segmentWidth + kLoadSaveSlotHorizontalGap;
    for (int segment = 0; segment < kSegments; ++segment) {
        const int segmentX = x + segment * segmentPitch;
        if (segmentX + segmentWidth > x + barWidth) {
            break;
        }
        const bool filled = static_cast<uint32_t>(filledSlots) * kSegments >
                            static_cast<uint32_t>(segment) * maxSlots;
        const uint8_t segmentBrightness = filled ? brightness : 1;
        for (int py = 0; py < barHeight; ++py) {
            for (int px = 0; px < segmentWidth; ++px) {
                _display.gfx.draw_pixel(_display.api.getFrameBuffer(), segmentX + px, y + py,
                                        segmentBrightness);
            }
        }
    }
}

void DisplayManager::drawLoadSaveDetailMetricAtColon(int colonX, int y, const char* label,
                                                     const char* value, int labelCharCount,
                                                     bool showUnsavedMarker) {
    const int labelX = colonX - labelCharCount * kLoadSaveDetailCharWidth;
    const int valueX = colonX + kLoadSaveDetailColonWidth;
    _display.gfx.select_font(&Font5x7FixedMono);
    if (showUnsavedMarker) {
        drawLoadSaveDetailUnsavedMarker(_display, labelX, y);
    }
    _display.gfx.draw_text(_display.api.getFrameBuffer(), label, labelX, y,
                           kLoadSaveDetailLabelBrightness);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), ":", colonX, y,
                           kLoadSaveDetailColonBrightness);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), value, valueX, y,
                           kLoadSaveDetailValueBrightness);
}

void DisplayManager::drawLoadSaveDetailMetricLeft(int detailX, int y, const char* label,
                                                    const char* value) {
    drawLoadSaveDetailMetricAtColon(loadSaveDetailLeftColonX(detailX), y, label, value,
                                    kLoadSaveDetailLabelChars);
}

void DisplayManager::drawLoadSaveDetailMetricRight(int y, const char* label, const char* value,
                                                   int labelCharCount, bool showUnsavedMarker) {
    drawLoadSaveDetailMetricAtColon(loadSaveDetailRightColonX(), y, label, value, labelCharCount,
                                    showUnsavedMarker);
}

void DisplayManager::drawLoadSaveDetailDateColumn(uint32_t timestampUnix) {
    RtcTime::LoadSaveDetailDateParts parts{};
    RtcTime::formatLoadSaveDetailDateParts(timestampUnix, parts);
    const char* lines[kLoadSaveDetailDateLineCount] = {parts.year, parts.monthDay,
                                                       parts.timeOfDay};
    const int detailRightX = DISPLAY_WIDTH - 1 - kLoadSaveDetailDateRightMargin;
    _display.gfx.select_font(&Font5x7FixedMono);
    for (int line = 0; line < kLoadSaveDetailDateLineCount; ++line) {
        const int y = loadSaveDetailDateLineY(line);
        const int textWidth =
            static_cast<int>(std::strlen(lines[line])) * kLoadSaveDetailCharWidth;
        const int textX = detailRightX - textWidth + 1;
        _display.gfx.draw_text(_display.api.getFrameBuffer(), lines[line], textX, y,
                               kLoadSaveDetailValueBrightness);
    }
}

void DisplayManager::drawLoadSaveWorkspaceDetail(int detailX,
                                                 const LoadSaveWorkspaceDetailParams& params) {
    _display.gfx.select_font(&Font5x7FixedMono);
    int y = kLoadSaveDetailContentTopY;

    char valueStr[12];
    if (params.setId != 0) {
        std::snprintf(valueStr, sizeof(valueStr), "%2u", static_cast<unsigned>(params.setId));
    } else {
        std::snprintf(valueStr, sizeof(valueStr), "--");
    }
    drawLoadSaveDetailMetricLeft(detailX, y, "Set", valueStr);

    if (params.revisionId != 0) {
        std::snprintf(valueStr, sizeof(valueStr), "%3u", static_cast<unsigned>(params.revisionId));
    } else {
        std::snprintf(valueStr, sizeof(valueStr), "%3s", "--");
    }
    drawLoadSaveDetailMetricRight(y, "Rev", valueStr, 3, params.markRevisionUnsaved);
    y += kLoadSaveTextLineStep;

    std::snprintf(valueStr, sizeof(valueStr), "%2u", params.metrics.trackCount);
    drawLoadSaveDetailMetricLeft(detailX, y, "Tracks", valueStr);
    std::snprintf(valueStr, sizeof(valueStr), "%3u",
                  static_cast<unsigned>(std::min(bpm + 0.5f, 999.0f)));
    drawLoadSaveDetailMetricRight(y, "BPM", valueStr, 3);
    y += kLoadSaveTextLineStep;

    std::snprintf(valueStr, sizeof(valueStr), "%2u", params.metrics.filledSlotCount);
    drawLoadSaveDetailMetricLeft(detailX, y, "Loops ", valueStr);
    std::snprintf(valueStr, sizeof(valueStr), "%3u",
                  static_cast<unsigned>(params.metrics.masterLoopBars));
    drawLoadSaveDetailMetricRight(y, "Bars", valueStr, kLoadSaveDetailRightLabelChars);
    y += kLoadSaveTextLineStep;

    const int detailBarWidth = loadSaveDetailSlotBarWidth(detailX);
    const int slotAreaTop = y;
    const int slotAreaBottom = DISPLAY_HEIGHT;
    const int slotAreaHeight = slotAreaBottom - slotAreaTop;
    constexpr uint8_t kTrackRows = Config::NUM_TRACKS;
    const int baseRowHeight =
        slotAreaHeight > 0 && kTrackRows > 0
            ? (slotAreaHeight - static_cast<int>(kTrackRows - 1) * kLoadSaveTrackRowGap) /
                  static_cast<int>(kTrackRows)
            : 1;
    const int rowHeightRemainder =
        slotAreaHeight - static_cast<int>(kTrackRows) * baseRowHeight -
        static_cast<int>(kTrackRows - 1) * kLoadSaveTrackRowGap;

    int rowY = slotAreaTop;
    for (uint8_t trackIndex = 0; trackIndex < kTrackRows; ++trackIndex) {
        const int rowHeight =
            baseRowHeight + (static_cast<int>(trackIndex) < rowHeightRemainder ? 1 : 0);
        if (detailBarWidth > 0 && rowHeight > 0 && rowY < slotAreaBottom) {
            const int drawHeight = std::min(rowHeight, slotAreaBottom - rowY);
            drawLoadSaveTrackFilledBar(detailX, rowY, detailBarWidth, drawHeight,
                                       params.metrics.perTrackFilledSlots[trackIndex],
                                       Config::MAX_LOOPS_PER_TRACK, 8);
        }
        rowY += rowHeight;
        if (trackIndex + 1 < kTrackRows) {
            rowY += kLoadSaveTrackRowGap;
        }
    }

    drawLoadSaveDetailDateColumn(params.timestampUnix);
}

void DisplayManager::drawAutoSaveBeforeLoadToast(int detailX, uint32_t nowMs) {
    if (autoSaveBeforeLoadToastText_[0] == '\0' ||
        nowMs >= autoSaveBeforeLoadToastExpiresAtMs_) {
        return;
    }
    _display.gfx.select_font(&Font5x7FixedMono);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), autoSaveBeforeLoadToastText_, detailX,
                           DISPLAY_HEIGHT - kLoadSaveTextLineStep, 15);
}

void DisplayManager::drawLoadSaveDirtyPromptView(uint32_t nowMs) {
    constexpr int kLeftMargin = 2;
    constexpr int kRowsStartY = 8;
    _display.gfx.select_font(&Font5x7FixedMono);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), "Save Current?", kLeftMargin, 0, 15);

    static const char* kRows[] = {"Yes", "No", "Cancel"};
    const uint8_t selection = StorageManager::getRevisionLoadDirtyPromptSelection();
    for (uint8_t row = 0; row < RevisionLoadPolicy::kDirtyPromptRowCount; ++row) {
        const int rowY = kRowsStartY + static_cast<int>(row) * 8;
        const uint8_t brightness = row == selection ? 15 : 5;
        _display.gfx.draw_text(_display.api.getFrameBuffer(), kRows[row], kLeftMargin, rowY,
                               brightness);
    }

    const DeferredSaveDisplayStatus saveStatus =
        StorageManager::getDeferredSaveDisplayStatus(nowMs);
    if (saveStatus.phase != DeferredSaveDisplayPhase::Idle) {
        _display.gfx.draw_text(_display.api.getFrameBuffer(), "Saving", kLeftMargin, 32, 5);
        drawPersistenceStatusDots(kLeftMargin + 6 * 6 + 2, 32 - 4, saveStatus);
    }
    drawSaveStatusIndicator(nowMs, DISPLAY_WIDTH - 4);
}

void DisplayManager::drawLoadSaveMinimalLoadingView(uint32_t nowMs) {
    constexpr int kLeftMargin = 2;
    _display.gfx.select_font(&Font5x7FixedMono);
    if (StorageManager::hasRevisionCommitWork()) {
        _display.gfx.draw_text(_display.api.getFrameBuffer(), "Saving", kLeftMargin, 0, 15);
        const DeferredSaveDisplayStatus saveStatus =
            StorageManager::getDeferredSaveDisplayStatus(nowMs);
        drawPersistenceStatusDots(kLeftMargin + 6 * 6 + 2, kSaveStatusDotY, saveStatus);
    } else {
        const uint16_t setId = StorageManager::getRevisionLoadDisplayTargetSetId();
        if (setId != 0) {
            char label[16];
            std::snprintf(label, sizeof(label), "S%04u", static_cast<unsigned>(setId));
            _display.gfx.draw_text(_display.api.getFrameBuffer(), label, kLeftMargin, 0, 15);
            drawLoadSaveRowLoadStatusDots(kLeftMargin, static_cast<int>(std::strlen(label)), 0,
                                          nowMs, setId, 0);
        } else {
            _display.gfx.draw_text(_display.api.getFrameBuffer(), "Loading...", kLeftMargin, 0, 15);
        }
    }
    drawSaveStatusIndicator(nowMs, DISPLAY_WIDTH - 4);
}

void DisplayManager::drawLoadSaveRevisionHistoryView(uint32_t nowMs, uint16_t setId) {
    ensureLoadSaveRevisionListCache(setId);

    const int kLoadSaveDividerX = loadSaveDividerX();
    const int kLoadSaveDetailX = loadSaveDetailContentX();
    constexpr int kListRowsStartY = kLoadSaveTextLineStep;
    constexpr int kListVisibleRows = (DISPLAY_HEIGHT - kListRowsStartY) / kLoadSaveTextLineStep;

    char header[16];
    std::snprintf(header, sizeof(header), "S%04u", static_cast<unsigned>(setId));
    _display.gfx.select_font(&Font5x7FixedMono);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), header, kLoadSaveLeftPadding, 0, 15);

    for (int row = 0; row < DISPLAY_HEIGHT; ++row) {
        _display.gfx.draw_pixel(_display.api.getFrameBuffer(), kLoadSaveDividerX, row, 2);
    }

    for (int visibleRow = 0; visibleRow < kListVisibleRows; ++visibleRow) {
        const size_t listIndex = static_cast<size_t>(loadSaveListScrollOffset_) +
                                 static_cast<size_t>(visibleRow);
        if (listIndex >= loadSaveRevisionListCount_) {
            break;
        }
        const int rowY = kListRowsStartY + visibleRow * kLoadSaveTextLineStep;
        const bool selected = listIndex == loadSaveListSelection_;
        const uint8_t brightness = selected ? 15 : 5;
        char revisionLabel[8];
        std::snprintf(revisionLabel, sizeof(revisionLabel), "v%04u",
                      static_cast<unsigned>(loadSaveRevisionListEntries_[listIndex].revisionId));
        _display.gfx.draw_text(_display.api.getFrameBuffer(), revisionLabel, kLoadSaveLeftPadding,
                               rowY, brightness);
        drawLoadSaveRowLoadStatusDots(
            kLoadSaveLeftPadding, static_cast<int>(std::strlen(revisionLabel)), rowY, nowMs, setId,
            loadSaveRevisionListEntries_[listIndex].revisionId);
    }

    LoadSaveWorkspaceDetailParams detailParams{};
    if (resolveLoadSaveWorkspaceDetail(detailParams)) {
        drawLoadSaveWorkspaceDetail(kLoadSaveDetailX, detailParams);
    }
}

void DisplayManager::drawLoadSaveLoopPickView(uint16_t setId) {
    constexpr int kLeftMargin = 2;
    char header[16];
    std::snprintf(header, sizeof(header), "S%04u", static_cast<unsigned>(setId));
    _display.gfx.select_font(&Font5x7FixedMono);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), header, kLeftMargin, 0, 15);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), "Loops", kLeftMargin, 8, 5);
}

void DisplayManager::drawLoadSaveView(uint32_t nowMs) {
    const StorageManager::SetBrowserOverlayMode overlayMode =
        StorageManager::getSetBrowserOverlayMode();
    if (overlayMode == StorageManager::SetBrowserOverlayMode::DirtyPrompt) {
        drawLoadSaveDirtyPromptView(nowMs);
        return;
    }
    if (overlayMode == StorageManager::SetBrowserOverlayMode::MinimalLoading) {
        drawLoadSaveMinimalLoadingView(nowMs);
        return;
    }
    if (overlayMode == StorageManager::SetBrowserOverlayMode::RevisionHistory) {
        drawLoadSaveRevisionHistoryView(nowMs, StorageManager::getSetBrowserOverlayDrilledSetId());
        return;
    }
    if (overlayMode == StorageManager::SetBrowserOverlayMode::LoopPick) {
        drawLoadSaveLoopPickView(StorageManager::getSetBrowserOverlayDrilledSetId());
        return;
    }

    constexpr int kLoadSaveDividerX = loadSaveDividerX();
    const int kLoadSaveDetailX = loadSaveDetailContentX();
    constexpr int kListRowsStartY = kLoadSaveTextLineStep;
    constexpr int kListVisibleRows = (DISPLAY_HEIGHT - kListRowsStartY) / kLoadSaveTextLineStep;

    _display.gfx.select_font(&Font5x7FixedMono);
    _display.gfx.draw_text(_display.api.getFrameBuffer(), "Sets", kLoadSaveLeftPadding, 0, 5);

    for (int row = 0; row < DISPLAY_HEIGHT; ++row) {
        _display.gfx.draw_pixel(_display.api.getFrameBuffer(), kLoadSaveDividerX, row, 2);
    }

    const size_t totalRows =
        SetBrowserOverlayPolicy::rootWorkspaceListRowCount(loadSaveListCount_);
    for (int visibleRow = 0; visibleRow < kListVisibleRows; ++visibleRow) {
        const size_t listIndex = static_cast<size_t>(loadSaveListScrollOffset_) +
                                 static_cast<size_t>(visibleRow);
        if (listIndex >= totalRows) {
            break;
        }
        const int rowY = kListRowsStartY + visibleRow * kLoadSaveTextLineStep;
        const bool selected = listIndex == loadSaveListSelection_;
        const uint8_t brightness = selected ? 15 : 5;

        if (SetBrowserOverlayPolicy::isRootSaveRow(static_cast<uint8_t>(listIndex))) {
            _display.gfx.draw_text(_display.api.getFrameBuffer(), "SAVE", kLoadSaveLeftPadding,
                                   rowY, brightness);
            continue;
        }
        if (SetBrowserOverlayPolicy::isRootCurrentRow(static_cast<uint8_t>(listIndex))) {
            _display.gfx.draw_text(_display.api.getFrameBuffer(), "CURRENT", kLoadSaveLeftPadding,
                                   rowY, brightness);
            continue;
        }

        const size_t savedIndex =
            SetBrowserOverlayPolicy::rootSetFolderListIndex(static_cast<uint8_t>(listIndex));
        if (savedIndex < loadSaveListCount_) {
            char browserLabel[24];
            copyLoadSaveBrowserLabel(browserLabel, sizeof(browserLabel),
                                     loadSaveListEntries_[savedIndex].folderName);
            if (loadSaveListEntries_[savedIndex].favorite != 0) {
                const size_t labelLen = std::strlen(browserLabel);
                if (labelLen + 1 < sizeof(browserLabel)) {
                    browserLabel[labelLen] = '*';
                    browserLabel[labelLen + 1] = '\0';
                }
            }
            _display.gfx.draw_text(_display.api.getFrameBuffer(), browserLabel,
                                   kLoadSaveLeftPadding, rowY, brightness);
            uint16_t rowSetId = 0;
            if (SetRevisionCatalog::parseSetIdFromFolderName(
                    loadSaveListEntries_[savedIndex].folderName, rowSetId)) {
                drawLoadSaveRowLoadStatusDots(kLoadSaveLeftPadding,
                                              static_cast<int>(std::strlen(browserLabel)), rowY,
                                              nowMs, rowSetId, 0);
            }
        }
    }

    LoadSaveWorkspaceDetailParams detailParams{};
    drawAutoSaveBeforeLoadToast(kLoadSaveDetailX, nowMs);
    if (resolveLoadSaveWorkspaceDetail(detailParams)) {
        drawLoadSaveWorkspaceDetail(kLoadSaveDetailX, detailParams);
    }
    drawSaveStatusIndicator(nowMs, DISPLAY_WIDTH - 4);
}

void DisplayManager::refreshAutoSaveBeforeLoadToast(uint32_t nowMs) {
    char savedFolder[16];
    if (!StorageManager::consumeAutoSaveBeforeLoadFolder(savedFolder, sizeof(savedFolder))) {
        return;
    }
    const int written =
        std::snprintf(autoSaveBeforeLoadToastText_, sizeof(autoSaveBeforeLoadToastText_),
                      "Saved %s", savedFolder);
    if (written <= 0 ||
        static_cast<size_t>(written) >= sizeof(autoSaveBeforeLoadToastText_)) {
        autoSaveBeforeLoadToastText_[0] = '\0';
        autoSaveBeforeLoadToastExpiresAtMs_ = 0;
        return;
    }
    autoSaveBeforeLoadToastExpiresAtMs_ = nowMs + 1500;
}
