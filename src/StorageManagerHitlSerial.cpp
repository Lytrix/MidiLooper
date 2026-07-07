//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#if defined(SESSION_CAPTURE)

#include "StorageManager.h"

#include <Arduino.h>
#include <cstring>

#include "DisplayManager.h"
#include "Globals.h"
#include "HitlDisplayBridge.h"
#include "StorageManagerInternal.h"

#if defined(__IMXRT1062__)
#define CAPTURE_HITL_MEM FLASHMEM
#else
#define CAPTURE_HITL_MEM
#endif

namespace {

// Serial line accumulator must live in RAM: processHitlSerialCommands is FLASHMEM and cannot
// access DMAMEM on IMXRT1062.
char sHitlSerialLineBuffer[48];
size_t sHitlSerialLineLength = 0;

}  // namespace

CAPTURE_HITL_MEM void StorageManager::processHitlSerialCommands() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#else
    while (Serial.available() > 0) {
        const char ch = static_cast<char>(Serial.read());
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            sHitlSerialLineBuffer[sHitlSerialLineLength] = '\0';
            if (std::strcmp(sHitlSerialLineBuffer, "!REV_COMMIT") == 0) {
                requestCommitRevisionForHitl();
            } else if (std::strcmp(sHitlSerialLineBuffer, "!REV_CLEANUP") == 0) {
                cleanupHitlRevisionCommit();
            } else if (std::strcmp(sHitlSerialLineBuffer, "!REV_NUKE_SETS") == 0) {
                nukeHitlSetsCatalog();
            } else if (StorageManagerInternal::handleHitlQuarantineCommandLine(sHitlSerialLineBuffer)) {
                Serial.println("[StorageManager] Reboot Teensy to load empty workspace.");
            } else if (std::strcmp(sHitlSerialLineBuffer, "!REV_LOAD_DIRTY_YES") == 0) {
                confirmRevisionLoadAfterCommitForHitl();
            } else if (std::strcmp(sHitlSerialLineBuffer, "!REV_LOAD_DIRTY_NO") == 0) {
                confirmRevisionLoadDiscardWorkspaceForHitl();
            } else if (std::strcmp(sHitlSerialLineBuffer, "!REV_LOAD_DIRTY_CANCEL") == 0) {
                cancelRevisionLoadRequestForHitl();
            } else if (std::strcmp(sHitlSerialLineBuffer, "!OVERLAY_SAVE") == 0) {
                HitlDisplayBridge::confirmLoadSaveFocusedRow();
            } else if (std::strcmp(sHitlSerialLineBuffer, "!OVERLAY_CONFIRM") == 0) {
                HitlDisplayBridge::confirmLoadSaveFocusedRow();
            } else if (std::strcmp(sHitlSerialLineBuffer, "!OVERLAY_ENTER") == 0) {
                looperState.enterLoadSaveMode();
            } else if (std::strcmp(sHitlSerialLineBuffer, "!OVERLAY_EXIT") == 0) {
                looperState.exitLoadSaveMode();
            } else if (std::strncmp(sHitlSerialLineBuffer, "!OVERLAY_SCROLL ", 16) == 0) {
                const char* cursor = sHitlSerialLineBuffer + 16;
                while (*cursor == ' ') {
                    ++cursor;
                }
                int delta = 0;
                bool negative = false;
                if (*cursor == '-') {
                    negative = true;
                    ++cursor;
                } else if (*cursor == '+') {
                    ++cursor;
                }
                while (*cursor >= '0' && *cursor <= '9') {
                    delta = delta * 10 + (*cursor - '0');
                    ++cursor;
                }
                if (negative) {
                    delta = -delta;
                }
                if (delta != 0) {
                    HitlDisplayBridge::adjustLoadSaveListSelection(delta);
                }
            } else if (std::strncmp(sHitlSerialLineBuffer, "!OVERLAY_REV_HISTORY ", 21) == 0) {
                const char* cursor = sHitlSerialLineBuffer + 21;
                while (*cursor == ' ') {
                    ++cursor;
                }
                unsigned setId = 0;
                while (*cursor >= '0' && *cursor <= '9') {
                    setId = setId * 10U + static_cast<unsigned>(*cursor - '0');
                    ++cursor;
                }
                if (setId > 0U && setId <= 0xFFFFU) {
                    HitlDisplayBridge::openRevisionHistoryFromHitl(static_cast<uint16_t>(setId));
                }
            } else if (std::strcmp(sHitlSerialLineBuffer, "!OVERLAY_BACK") == 0) {
                HitlDisplayBridge::navigateLoadSaveOverlayBackFromHitl();
            } else if (std::strncmp(sHitlSerialLineBuffer, "!REV_LOAD ", 10) == 0) {
                const char* cursor = sHitlSerialLineBuffer + 10;
                unsigned setId = 0;
                unsigned revisionId = 0;
                while (*cursor == ' ') {
                    ++cursor;
                }
                while (*cursor >= '0' && *cursor <= '9') {
                    setId = setId * 10U + static_cast<unsigned>(*cursor - '0');
                    ++cursor;
                }
                while (*cursor == ' ') {
                    ++cursor;
                }
                while (*cursor >= '0' && *cursor <= '9') {
                    revisionId = revisionId * 10U + static_cast<unsigned>(*cursor - '0');
                    ++cursor;
                }
                if (setId > 0U && setId <= 0xFFFFU && revisionId > 0U && revisionId <= 0xFFFFU) {
                    requestLoadRevisionForHitl(static_cast<uint16_t>(setId),
                                               static_cast<uint16_t>(revisionId));
                }
            }
            sHitlSerialLineLength = 0;
            continue;
        }
        if (sHitlSerialLineLength + 1 < sizeof(sHitlSerialLineBuffer)) {
            sHitlSerialLineBuffer[sHitlSerialLineLength++] = ch;
        }
    }
#endif
}

#endif  // SESSION_CAPTURE
