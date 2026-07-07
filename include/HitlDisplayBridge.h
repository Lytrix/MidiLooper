//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#if defined(SESSION_CAPTURE)

namespace HitlDisplayBridge {

void confirmLoadSaveFocusedRow();
void adjustLoadSaveListSelection(int delta);
void openRevisionHistoryFromHitl(uint16_t setId);
void navigateLoadSaveOverlayBackFromHitl();

}  // namespace HitlDisplayBridge

#endif
