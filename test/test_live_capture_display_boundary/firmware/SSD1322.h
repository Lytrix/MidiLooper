#pragma once
// Native raster/transport I/O only. Domain source selection stays production code.
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include "Font5x7FixedMono.h"
inline void (*nativeDisplayOnSubmit)() = nullptr;
inline void (*nativeDisplayOnComplete)() = nullptr;
struct NativeDisplayAPI {
  std::array<uint8_t, 8192> buffer{}, inFlight{}, completed{};
  bool active = false, completionRequested = false;
  uint32_t submitted = 0, completions = 0;
  uint8_t* getFrameBuffer() { return buffer.data(); }
  void display() {
    if (active) return;
    inFlight = buffer;
    active = true;
    ++submitted;
    if (nativeDisplayOnSubmit) nativeDisplayOnSubmit();
  }
  void pollDisplayTransfer() {
    if (active && completionRequested) {
      completed = inFlight;
      active = completionRequested = false;
      ++completions;
      if (nativeDisplayOnComplete) nativeDisplayOnComplete();
    }
  }
  bool isDisplayTransferActive() const { return active; }
  bool isDmaInitialized() const { return true; }
  bool isDmaDegraded() const { return false; }
  uint32_t dmaCompletedFrameCount() const { return completions; }
  uint32_t dmaBlockingFrameCount() const { return 0; }
  uint32_t dmaTimeoutCount() const { return 0; }
  uint32_t dmaRejectionCount() const { return 0; }
  uint32_t dmaRecoveryCount() const { return 0; }
};
struct NativeDisplayGFX {
  uint32_t rectangles = 0, pianoRows = 0;
  const GFXfont* font = nullptr;
  void select_font(const GFXfont* selected) { font = selected; }
  void draw_text(uint8_t* p, const char* text, uint16_t x, uint16_t y, uint8_t value) {
    if (!font) return;
    while (*text) {
      const uint8_t ch = static_cast<uint8_t>(*text++);
      if (ch < font->first || ch > font->last) continue;
      const auto& glyph = font->glyph[ch - font->first];
      for (unsigned bit = 0; bit < unsigned(glyph.width) * glyph.height; ++bit)
        if (font->bitmap[glyph.bitmapOffset + bit / 8] & (0x80 >> (bit % 8)))
          draw_pixel(p, x + glyph.xOffset + bit % glyph.width,
                     y + glyph.yOffset + bit / glyph.width, value);
      x += glyph.xAdvance;
    }
  }
  void set_buffer_size(int, int) {}
  void fill_buffer(uint8_t* p, uint8_t value) { rectangles = pianoRows = 0; std::memset(p, value, 8192); }
  void draw_pixel(uint8_t* p, int x, int y, uint8_t value) {
    if (x < 0 || x >= 256 || y < 0 || y >= 64) return;
    auto& cell = p[y * 128 + x / 2];
    cell = x % 2 ? ((cell & 0xf0) | (value & 15)) : ((cell & 15) | ((value & 15) << 4));
  }
  void draw_vline(uint8_t* p, int x, int y0, int y1, uint8_t value) {
    for (int y = std::min(y0,y1); y <= std::max(y0,y1); ++y) draw_pixel(p,x,y,value);
  }
  void draw_rect_filled(uint8_t* p, int x0, int y0, int x1, int y1, uint8_t value) {
    ++rectangles;
    if (y0 == y1 && y0 >= 0 && y0 < 32) ++pianoRows;
    for (int x = std::min(x0,x1); x <= std::max(x0,x1); ++x) draw_vline(p,x,y0,y1,value);
  }
};
using SSD1322_GFX = NativeDisplayGFX;
struct SSD1322 { NativeDisplayAPI api; NativeDisplayGFX gfx; void begin() {} };
