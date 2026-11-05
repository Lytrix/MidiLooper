//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The engine firmware's screen (fixed-memory-engine E9.8c; owner 2026-11-06: the display before the controls).
//
// The SSD1322 (256 × 64, 4-bit) through the `SSD1322_OLED` library: one frame every `kFrameMs`, drawn into the
// library's framebuffer and sent by DMA (the library copies the frame first, so the next one can be drawn at once).
// The first frame after `begin` goes over blocking SPI, which arms the panel's GRAM window (the library's resync
// path): measured 2026-11-06, the panel stayed blank when the first frame was a DMA one and drew once a blocking frame
// had been sent.
// The legacy layout:
// * Left column (x 0–21): the 8 tracks — a state letter (P playing, R writing, - idle) and the number; the selected
//   track bright.
// * Piano roll (x 22–225): the detailed view (rows 0–31) of the shown slot — up to 4 bars, paged with the playhead —
//   with beat and bar lines, the notes from the engine's tiles (`EngineRoot::drawRollBand`: span, dense and density
//   tiles, the live take's overlay) and the playhead; the overview strip (rows 33–40) of the whole loop from density
//   tiles, with the detailed view's range and the playhead marked — only when the loop is longer than the detailed view
//   (a loop the view already shows whole would only repeat as a slab).
// * Below it: the shown slot, its bars and undo depth. Sidebar (x 226–255): tempo, clock source, transport.
// Reads the engine only (tiles, transport, history): nothing here composes, allocates or touches pages. Drawing a frame
// is a non-engine block of the main loop (part of B_max).

#pragma once

#if defined(ENGINE_FIRMWARE)

#include <Arduino.h>
#include <SSD1322.h>  // before the font: it declares GFXfont
#include <Font5x7FixedMono.h>

#include <cstdio>

#include "ContentEngine/EngineClock.h"
#include "ContentEngine/EngineRoot.h"

namespace EngineFirmware {

class EngineDisplay {
 public:
  static constexpr uint32_t kFrameMs = 33;
  static constexpr int16_t kX0 = 22, kX1 = 225, kRollY0 = 0, kRollY1 = 31, kOverY0 = 33, kOverY1 = 40, kSideX = 227;
  static constexpr uint32_t kViewBars = 4;

  void begin() {
    oled_.begin();
    oled_.gfx.set_buffer_size(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    oled_.gfx.select_font(&Font5x7FixedMono);
    oled_.gfx.fill_buffer(oled_.api.getFrameBuffer(), 0);
    sendBlocking();  // arms the GRAM window before the first DMA frame
    for (uint8_t& s : shown_) s = 0xFF;
    focus_ = 0xFF;
  }
  /// Panel check (`!ENGINE oled-test`, and once at boot): `blocking` sends a full bright frame over plain SPI, else a
  /// striped frame by DMA. The serial line names which one, so what the panel shows can be matched to it.
  void selfTest(bool blocking) {
    uint8_t* f = fb();
    if (blocking) {
      oled_.gfx.fill_buffer(f, 15);
      sendBlocking();
    } else {
      oled_.gfx.fill_buffer(f, 0);
      for (int16_t y = 0; y < DISPLAY_HEIGHT; y += 8) oled_.gfx.draw_rect_filled(f, 0, y, DISPLAY_WIDTH - 1, y + 3, 12);
      text("ENGINE DMA", 90, 40, 15);
      oled_.api.display();
    }
    Serial.printf("#ENGINE,oled_test,%s,dma_init=%u,dma_degraded=%u,dma_frames=%lu,blocking_frames=%lu\n",
                  blocking ? "blocking_white" : "dma_stripes", oled_.api.isDmaInitialized(), oled_.api.isDmaDegraded(),
                  static_cast<unsigned long>(oled_.api.dmaCompletedFrameCount()),
                  static_cast<unsigned long>(oled_.api.dmaBlockingFrameCount()));
  }
  /// Holds the screen for `ms` (the next frame waits).
  void hold(uint32_t nowMs, uint32_t ms) { lastFrameMs_ = nowMs + ms - kFrameMs; }

  /// The slot a track shows while it plays nothing (the last one launched or recorded on it).
  void show(uint8_t track, uint8_t slot) {
    if (track < ContentEngine::kTrackCount) shown_[track] = slot;
  }

  /// Once per main-loop pass: a frame when one is due and the previous transfer is done. Returns the frame's draw time
  /// in µs (0: no frame).
  uint32_t service(ContentEngine::EngineRoot& e, const ContentEngine::EngineClock& clock, uint32_t lastTick,
                   uint32_t nowMs) {
    oled_.api.pollDisplayTransfer();
    if (nowMs - lastFrameMs_ < kFrameMs || oled_.api.isDisplayTransferActive()) return 0;
    lastFrameMs_ = nowMs;
    const uint32_t t0 = micros();
    draw(e, clock, lastTick);
    const uint32_t us = micros() - t0;
    oled_.api.display();
    ++frames_;
    if (us > worstUs_) worstUs_ = us;
    return us;
  }
  uint32_t frames() const { return frames_; }
  uint32_t worstDrawUs() const { return worstUs_; }

 private:
  using RollView = ContentEngine::RollView;
  static constexpr uint32_t kBar = ContentEngine::kTicksPerBar;

  uint8_t* fb() { return oled_.api.getFrameBuffer(); }
  void sendBlocking() {
    oled_.api.SSD1322_API_set_window(0, 63, 0, 63);
    oled_.api.SSD1322_API_command(SSD1322_WRITE_RAM);
    oled_.api.SSD1322_API_send_buffer(fb(), FRAMEBUFFER_SIZE);
  }
  void text(const char* s, int16_t x, int16_t y, uint8_t brightness) { oled_.gfx.draw_text(fb(), s, x, y, brightness); }

  void draw(ContentEngine::EngineRoot& e, const ContentEngine::EngineClock& clock, uint32_t lastTick) {
    oled_.gfx.fill_buffer(fb(), 0);
    auto& x = e.transport();
    const uint8_t sel = e.control().selectedTrack();
    drawTracks(e, sel);
    // The shown slot: the selected track's playing (or writing) slot, else the one it last showed.
    uint8_t slot = x.playing(sel) ? x.playingSlot(sel) : shown_[sel];
    if (x.writingTrack() == sel) slot = x.writingSlot();
    char line[40];
    if (slot == 0xFF || slot >= ContentEngine::Config::kSlotCount) {
      text("no slot", kX0 + 4, 20, 6);
    } else {
      if (slot != focus_) {  // span tiles for the shown slot (swept first)
        e.setDisplayFocus(slot, slot);
        focus_ = slot;
      }
      drawSlot(e, sel, slot, lastTick, line);
    }
    // Sidebar.
    oled_.gfx.draw_vline(fb(), kSideX - 2, 0, 63, 2);
    snprintf(line, sizeof line, "%3lu", static_cast<unsigned long>(60000000ul / (clock.usPerTick() * 192ul)));
    text(line, kSideX, 8, 12);
    text("BPM", kSideX, 17, 5);
    text(clock.external() ? "EXT" : "INT", kSideX, 30, clock.external() ? 12 : 5);
    text(clock.running() ? "RUN" : "STP", kSideX, 43, clock.running() ? 15 : 6);
    if (!e.persistence().approved()) text("RO", kSideX, 56, 4);  // read only: saving not approved
  }

  void drawTracks(ContentEngine::EngineRoot& e, uint8_t sel) {
    auto& x = e.transport();
    char label[3] = {0, 0, 0};
    for (uint8_t t = 0; t < 8; ++t) {
      const int16_t y = static_cast<int16_t>(t * 8 + 7);
      label[0] = x.writingTrack() == t ? 'R' : x.playing(t) ? 'P' : '-';
      label[1] = 0;
      text(label, 0, y, t == sel ? 15 : 7);
      label[0] = static_cast<char>('1' + t);
      text(label, 10, y, t == sel ? 15 : 4);
    }
  }

  void drawSlot(ContentEngine::EngineRoot& e, uint8_t track, uint8_t slot, uint32_t lastTick, char* line) {
    auto& x = e.transport();
    const bool writing = x.writingSlot() == slot;
    const ContentEngine::ManifestId m = e.history().current(slot);
    uint32_t loop = m == ContentEngine::kNoManifest ? 0 : e.manifests().manifest(m).loopLengthTicks;
    uint32_t pos = 0;
    const bool playing = x.playing(track) && x.playingSlot(track) == slot;
    if (writing) {
      pos = x.writePosition(lastTick);
      if (loop == 0 || loop > 256 * kBar) loop = (pos / (kViewBars * kBar) + 1) * kViewBars * kBar;  // a first take
    } else if (playing && loop != 0) {
      pos = (lastTick - x.origin(track)) % loop;
    }
    if (loop == 0) {
      snprintf(line, 40, "slot %u  empty", slot);
      text(line, kX0 + 4, 55, 8);
      return;
    }
    const uint32_t bars = (loop + kBar - 1) / kBar;
    const uint32_t viewTicks = loop < kViewBars * kBar ? loop : kViewBars * kBar;
    const uint32_t first = (pos / viewTicks) * viewTicks;
    // Pitch range: the view's bars, from density tiles (as the legacy auto-fit).
    uint8_t lo, hi;
    const uint8_t firstBar = static_cast<uint8_t>(first / kBar);
    uint8_t lastBar = static_cast<uint8_t>((first + viewTicks - 1) / kBar);
    if (lastBar >= bars) lastBar = static_cast<uint8_t>(bars - 1);
    if (!ContentEngine::pitchExtent(e.tiles(), slot, firstBar, lastBar, lo, hi)) {
      lo = 60;
      hi = 72;
    }
    if (hi == lo) hi = static_cast<uint8_t>(lo + 1);
    // Grid: beats dim, bars brighter.
    const int32_t w = kX1 - kX0;
    for (uint32_t b = 0; b * (kBar / 4) < viewTicks; ++b) {
      const int16_t gx = static_cast<int16_t>(kX0 + static_cast<int32_t>(b * (kBar / 4)) * w / static_cast<int32_t>(viewTicks));
      oled_.gfx.draw_vline(fb(), gx, kRollY0, kRollY1, b % 4 == 0 ? 3 : 1);
    }
    const RollView v{slot, loop, kBar, first, viewTicks, lo, hi, kX0, kX1, kRollY0, kRollY1};
    if (writing) e.setOverlayView(loop, first, viewTicks / static_cast<uint32_t>(w + 1) + 1, lo);
    e.drawRollBand(v, fb(), DISPLAY_WIDTH, kRollY0, kRollY1);
    if (playing || writing) {
      const int16_t px = static_cast<int16_t>(kX0 + static_cast<int64_t>(pos - first) * w / viewTicks);
      oled_.gfx.draw_vline(fb(), px, kRollY0, kRollY1, 15);
    }
    // Overview: the whole loop, the view's range and the playhead (when the view does not already show it whole).
    if (loop > viewTicks) drawOverview(e, slot, loop, bars, first, viewTicks, pos, playing || writing, lo, hi);
    // Status line.
    snprintf(line, 40, "S%02u %s %lub u%u", slot, writing ? "REC" : playing ? "PLAY" : "STOP",
             static_cast<unsigned long>(bars), e.history().undoDepth(slot));
    text(line, kX0 + 2, 54, 10);
    snprintf(line, 40, "bar %lu/%lu", static_cast<unsigned long>(pos / kBar + 1), static_cast<unsigned long>(bars));
    text(line, kX0 + 2, 63, 6);
  }

  void drawOverview(ContentEngine::EngineRoot& e, uint8_t slot, uint32_t loop, uint32_t bars, uint32_t first,
                    uint32_t viewTicks, uint32_t pos, bool moving, uint8_t lo, uint8_t hi) {
    const int32_t w = kX1 - kX0;
    uint8_t olo, ohi;
    if (!ContentEngine::pitchExtent(e.tiles(), slot, 0, static_cast<uint8_t>(bars > 64 ? 63 : bars - 1), olo, ohi)) {
      olo = lo;
      ohi = hi;
    }
    if (ohi == olo) ohi = static_cast<uint8_t>(olo + 1);
    ContentEngine::drawOverviewBand(e.tiles(), slot, loop, kBar, olo, ohi, kX0, kX1, kOverY0, kOverY1, fb(),
                                    DISPLAY_WIDTH, kOverY0, kOverY1);
    const int16_t vx0 = static_cast<int16_t>(kX0 + static_cast<int64_t>(first) * w / loop);
    const int16_t vx1 = static_cast<int16_t>(kX0 + static_cast<int64_t>(first + viewTicks) * w / loop);
    oled_.gfx.draw_hline(fb(), vx0, kOverY1 + 1, vx1 > kX1 ? kX1 : vx1, 5);
    if (moving)
      oled_.gfx.draw_vline(fb(), static_cast<int16_t>(kX0 + static_cast<int64_t>(pos) * w / loop), kOverY0, kOverY1, 12);
  }

  SSD1322 oled_;
  uint8_t shown_[ContentEngine::kTrackCount] = {};
  uint8_t focus_ = 0xFF;
  uint32_t lastFrameMs_ = 0, frames_ = 0, worstUs_ = 0;
};

}  // namespace EngineFirmware

#endif  // ENGINE_FIRMWARE
