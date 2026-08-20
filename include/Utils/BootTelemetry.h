//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <Arduino.h>

#include "Utils/DebugSessionCapture.h"

inline void emitBootMilestone(const char* stage, const char* detail) {
#if defined(SESSION_CAPTURE)
    Serial.print("BOOT,");
    Serial.print(stage);
    Serial.print(',');
    Serial.println(detail);
    char line[96];
    snprintf(line, sizeof(line), "#CAP,BOOT,%s,%s", stage, detail);
    DebugSessionCapture::appendCaptureTextLine(line);
#else
    (void)stage;
    (void)detail;
#endif
}
