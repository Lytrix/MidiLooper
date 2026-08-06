//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Boot screen drawing and early OLED init (Phase 5).

#include "DisplayManager.h"

#include "SSD1322_Config.h"
#include <Arduino.h>
#include <Font5x7FixedMono.h>
#include <algorithm>
#include <cstring>

namespace {

constexpr uint8_t kBootTitleBrightness = 12;
constexpr uint8_t kBootVersionBrightness = 5;
constexpr int kBootFixedCharWidth = 6;
constexpr int kBootFixedFontHeight = 7;
constexpr int kBootVersionRightMargin = 2;
constexpr unsigned long kBootScreenExtraHoldMs = 2000;

int bootTitleScaleX(const char* text) {
    const int charCount = static_cast<int>(std::strlen(text));
    if (charCount <= 0) {
        return 1;
    }
    return std::max(1, static_cast<int>(DISPLAY_WIDTH) / (charCount * kBootFixedCharWidth));
}

int bootTitleScaleY() {
    const int targetHeight = static_cast<int>(DISPLAY_HEIGHT) / 3;
    return std::max(1, targetHeight / kBootFixedFontHeight);
}

void drawScaledFixedMonoChar(SSD1322_GFX& gfx, uint8_t* frameBuffer, const GFXfont& font,
                             uint8_t charCode, int x, int y, int scaleX, int scaleY,
                             uint8_t brightness) {
    if (charCode < font.first || charCode > font.last) {
        return;
    }

    const GFXglyph& glyph = font.glyph[charCode - font.first];
    uint16_t bitmapOffset = glyph.bitmapOffset;
    const uint8_t width = glyph.width;
    const uint8_t height = glyph.height;
    const int8_t xOffset = glyph.xOffset;
    const int8_t yOffset = glyph.yOffset;

    uint8_t bit = 0;
    uint8_t bits = 0;
    for (uint8_t yPos = 0; yPos < height; ++yPos) {
        for (uint8_t xPos = 0; xPos < width; ++xPos) {
            if (!(bit++ & 7)) {
                bits = font.bitmap[bitmapOffset++];
            }
            if (bits & 0x80) {
                const int pixelX = x + (xOffset + static_cast<int>(xPos)) * scaleX;
                const int pixelY = y + (yOffset + static_cast<int>(yPos)) * scaleY;
                gfx.draw_rect_filled(frameBuffer, static_cast<uint16_t>(pixelX),
                                     static_cast<uint16_t>(pixelY),
                                     static_cast<uint16_t>(pixelX + scaleX - 1),
                                     static_cast<uint16_t>(pixelY + scaleY - 1), brightness);
            }
            bits <<= 1;
        }
    }
}

void drawScaledFixedMonoText(SSD1322_GFX& gfx, uint8_t* frameBuffer, const char* text, int x, int y,
                             int scaleX, int scaleY, uint8_t brightness) {
    const GFXfont& font = Font5x7FixedMono;
    int cursorX = x;
    while (*text != '\0') {
        const uint8_t charCode = static_cast<uint8_t>(*text++);
        drawScaledFixedMonoChar(gfx, frameBuffer, font, charCode, cursorX, y, scaleX, scaleY,
                                brightness);
        if (charCode >= font.first && charCode <= font.last) {
            cursorX += font.glyph[charCode - font.first].xAdvance * scaleX;
        }
    }
}

}  // namespace

void DisplayManager::drawBootScreen() {
    uint8_t* frameBuffer = _display.api.getFrameBuffer();
    _display.gfx.fill_buffer(frameBuffer, 0);

    constexpr const char* kTitle = "OSTINATIX";
    const int titleLen = static_cast<int>(std::strlen(kTitle));
    const int scaleX = bootTitleScaleX(kTitle);
    const int scaleY = bootTitleScaleY();
    const int scaledWidth = titleLen * kBootFixedCharWidth * scaleX;
    const int scaledHeight = kBootFixedFontHeight * scaleY;
    const int x = std::max(0, (static_cast<int>(DISPLAY_WIDTH) - scaledWidth) / 2);
    const int yTop = std::max(0, (static_cast<int>(DISPLAY_HEIGHT) - scaledHeight) / 2);
    const int baselineY = yTop + scaledHeight;

    drawScaledFixedMonoText(_display.gfx, frameBuffer, kTitle, x, baselineY, scaleX, scaleY,
                            kBootTitleBrightness);

    constexpr const char* kVersion = "v0.6";
    _display.gfx.select_font(&Font5x7FixedMono);
    const int versionWidth = static_cast<int>(std::strlen(kVersion)) * kBootFixedCharWidth;
    const int versionX =
        static_cast<int>(DISPLAY_WIDTH) - versionWidth - kBootVersionRightMargin;
    _display.gfx.draw_text(frameBuffer, kVersion, static_cast<uint16_t>(versionX),
                           static_cast<uint16_t>(DISPLAY_HEIGHT), kBootVersionBrightness);

    bootScreenVisible_ = true;
    bootSetupComplete_ = false;
    bootScreenHoldUntilMs_ = millis() + kBootScreenExtraHoldMs;
    _display.api.display();
}

void DisplayManager::beginBootOled() {
    Serial.println("DisplayManager: Early OLED init for boot load...");
    _display.begin();
    _display.gfx.set_buffer_size(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    clearDisplayBuffer();
}

void DisplayManager::finishBootSetup() {
    Serial.println("DisplayManager: Boot setup complete");
    bootSetupComplete_ = true;
}
