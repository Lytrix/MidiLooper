//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The engine firmware's screen (fixed-memory-engine E9.8c; layout E9.8k, owner 2026-11-06: mock-up "B2 round 6").
//
// The SSD1322 (256 × 64, 4-bit) through the `SSD1322_OLED` library: one frame every `kFrameMs`, drawn into the
// library's framebuffer and sent by DMA (the library copies the frame first, so the next one can be drawn at once).
// The first frame after `begin` goes over blocking SPI, which arms the panel's GRAM window (the library's resync
// path): measured 2026-11-06, the panel stayed blank when the first frame was a DMA one and drew once a blocking frame
// had been sent.
// Frames are written off-screen (`SSD1322_API::setPageFlip`; owner 2026-11-08): to the 64 GRAM rows not shown, the
// display start line moved there once complete, so the panel never scans a half-written frame (the playhead tore).
// `!ENGINE oled-flip 0` turns it off for a comparison.
// The layout (no sidebar; every field has a fixed place and shows `-` when it has nothing to show):
// * Left column (x 0–21): the 8 tracks' numbers. While the clock runs, each track's activity level (Cubase's activity
//   column): the last NoteOn's velocity, falling over 0.5 s (`ClockOutput::level`), dim on a muted or solo-hidden track.
//   While it does not, the legacy state letter (`trackStateToLetter`: R recording, O overdubbing, A armed, P playing,
//   S stopped with content in its selected slot, - its selected slot empty, M muted or hidden by another track's solo
//   — the selected track shows its real state); the selected track's letter pulses (1 Hz, brightness 7–13; legacy 4–10).
// * Piano roll (x 22–255, rows 0–45; four dark rows under it, no rule before the status band): the shown slot — the
//   whole loop up to 16 bars, else a 16-bar page following the playhead — with bar lines (and beat lines up to 4
//   bars), the engine's tiles and live overlay (`EngineRoot::drawRollBand`) and the playhead. No overview strip (owner
//   2026-11-06: too crowded; BARS and the position say where the page is).
// * Status band (rows 50–63, the values on the bottom row; 3 × 5 labels over 5 × 7 values, every number
//   right-aligned, no leading zeros):
//   - the current tick: the save dots (legacy, 4 dots: dim pending, one running while writing, bright 0.8 s after a
//     save, mid after a failure; `RO` before saving is approved) and the transport icon (stop, play, record, overdub,
//     a pulsing record circle when armed) over the position `bar.beat.16th.tick` (the bar bright); while no dots show,
//     the loop's progress runs in their row up to the icon. A queued slot or Record counts down to its start (`-1.3.2.24`). In NOTE_EDIT the block reads START over
//     the selected note's start (the bracket's in length mode), all bright.
//   - BARS: the loop's length in bars (a first take: its bars so far).
//   - the selection: TRACK, SLOT (1–8 within the track) and the port over its channel (`USB1 1`; `ALL` when the track
//     plays on every port); in NOTE_EDIT, NOTE (C3 = 60), VELOCITY (`M`: muted) and LENGTH (ticks); in LOOP_EDIT, START
//     (bar), LENGTH (bars) and BPM; their labels inverted.
//   - UNDO (the slot's undo steps), ODUB (the open overdub's) or EDIT (the open NOTE_EDIT session's).
//   - the tempo (legacy `120.0`) under the clock icon: dim on the internal clock; an arrow into it, bright, on an
//     external one.
// * NOTE_EDIT (E9.8b-2, `EditView`): the window frozen at the session's, the selected note highlighted, the bracket as a
//   dotted line.
// Reads the engine only (tiles, transport, history, output levels, persistence state): nothing here composes, allocates
// or touches pages. Drawing a frame is a non-engine block of the main loop (part of B_max).

#pragma once

#if defined(ENGINE_FIRMWARE)

#include <Arduino.h>
#include <SSD1322.h>  // before the font: it declares GFXfont
#include <Font5x7FixedMono.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "ContentEngine/EngineClock.h"
#include "ContentEngine/Browser.h"
#include "ContentEngine/EngineRoot.h"
#include "ContentEngine/StatusView.h"
#include "ContentEngine/WallClock.h"

namespace EngineFirmware {

/// What NOTE_EDIT shows (from the surface's `EditSurface`).
struct EditView {
  bool active = false, ready = false;  // a session gathering / open
  uint8_t slot = 0xFF;
  uint32_t first = 0, ticks = 0, bracket = 0;
  bool note = false, length = false;
  uint32_t noteTick = 0, noteLength = 0;
  uint8_t notePitch = 0, noteVelocity = 0;
  uint8_t steps = 0;  // the session's E: level
  bool velocity = false;  // NOTELEN held: F4 is the velocity
  bool loop = false;      // LOOP_EDIT (owner 2026-11-08)
  uint32_t loopTicks = 0;  // LOOP_EDIT's F2 moved, not yet applied: the pending length (ticks)
  uint32_t loopStart = 0;  // LOOP_EDIT's F1: the start, ticks from the content LOOP_EDIT found
  bool popup = false;      // the length popup (owner 2026-11-08)
  uint8_t popupSlot = 0;
  uint32_t popupLoop = 0;  // the length it shows (ticks)
};

/// What the sets browser shows beside its model (sets browser slice 3): the working set's save state ("SAVED 14:32"),
/// what return brings back ("TO SET 02 AS LEFT 14:57"), the time zone for dates.
struct BrowserInfo {
  char saved[16] = {};
  char returnTo[32] = {};
  int16_t tz = 0;
  uint32_t now = 0;  // the wall time (0: not set): the timeline's right end
};

class EngineDisplay {
 public:
  static constexpr uint32_t kFrameMs = 33;
  static constexpr int16_t kX0 = 22, kX1 = 255, kRollY0 = 0, kRollY1 = 45;
  static constexpr int16_t kLabY = 50, kValBase = 64;  // labels rows 50–54, values rows 57–63 (the bottom)
  static constexpr uint32_t kViewBars = 16;
  static constexpr uint8_t kRule = 3;  // the roll's bar lines and the status band's cell dividers (owner)

  void begin() {
    oled_.begin();
    oled_.gfx.set_buffer_size(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    oled_.gfx.select_font(&Font5x7FixedMono);
    oled_.gfx.fill_buffer(oled_.api.getFrameBuffer(), 0);
    sendBlocking();  // arms the GRAM window before the first DMA frame
    // Off-screen frames (owner 2026-11-08: the playhead tore while the panel scanned a frame being written; no tear
    // with them): each frame goes to the GRAM rows not shown, then the start line moves there. Both pages blank.
    oled_.api.setPageFlip(true);
    sendBlocking();
    for (uint8_t& s : shown_) s = 0xFF;
    focus_ = 0xFF;
    started_ = false;
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
  /// The last frame drawn as 64 lines `#FB,<row>,<256 hex digits>` (4-bit gray, two pixels a byte, high nibble first;
  /// the legacy `!FB_DUMP` format), for `scripts/render_display_dump.py`. Diagnostics: ~17 KB on the serial port.
  void dump() {
    static const char kHex[] = "0123456789abcdef";
    const uint8_t* f = fb();
    char line[DISPLAY_WIDTH + 1];
    for (uint8_t y = 0; y < DISPLAY_HEIGHT; ++y) {
      for (uint16_t i = 0; i < DISPLAY_WIDTH / 2; ++i) {
        const uint8_t b = f[y * (DISPLAY_WIDTH / 2) + i];
        line[2 * i] = kHex[b >> 4];
        line[2 * i + 1] = kHex[b & 15];
      }
      line[DISPLAY_WIDTH] = 0;
      Serial.printf("#FB,%u,%s\n", y, line);
    }
    // The NOTE_EDIT state the frame was drawn with.
    Serial.printf("#FB,edit,active=%u,ready=%u,slot=%u,note=%u,tick=%lu,pitch=%u,len=%lu,bracket=%lu,length=%u\n",
                  edit_.active, edit_.ready, edit_.slot, edit_.note, static_cast<unsigned long>(edit_.noteTick),
                  edit_.notePitch, static_cast<unsigned long>(edit_.noteLength), static_cast<unsigned long>(edit_.bracket),
                  edit_.length);
  }
  /// Holds the screen for `ms` (the next frame waits).
  void hold(uint32_t nowMs, uint32_t ms) { lastFrameMs_ = nowMs + ms - frameMs_; }
  /// Panel timing (diagnostics, owner 2026-11-08: the playhead tore part of the loop — the panel scanning rows while a
  /// frame is written): `reg` 0xB3 front clock (high nibble oscillator, low nibble divide − 1; boot 0x91) or 0xB1
  /// phase lengths (boot 0xE2), `value` its byte, sent once the frame in flight is done; 0 leaves it. `frameMs` the
  /// frame period (0 leaves it). Not kept over a reboot.
  /// `flip` 1 / 0: off-screen frames (`SSD1322_API::setPageFlip`), anything else leaves it.
  void tune(uint8_t clock, uint8_t phase, uint32_t frameMs, int flip = -1) {
    while (oled_.api.isDisplayTransferActive()) oled_.api.pollDisplayTransfer();
    if (flip == 0 || flip == 1) {
      oled_.api.setPageFlip(flip == 1);
      sendBlocking();  // both pages agree at once
    }
    if (clock != 0) {
      oled_.api.SSD1322_API_command(0xB3);
      oled_.api.SSD1322_API_data(clock);
      clock_ = clock;
    }
    if (phase != 0) {
      oled_.api.SSD1322_API_command(0xB1);
      oled_.api.SSD1322_API_data(phase);
      phase_ = phase;
    }
    if (frameMs != 0) frameMs_ = frameMs;
    Serial.printf("#ENGINE,oled,clock=0x%02X,phase=0x%02X,frame_ms=%lu,flip=%u\n", clock_, phase_,
                  static_cast<unsigned long>(frameMs_), oled_.api.pageFlip());
  }

  /// The sets browser to draw while it is open (slice 3), and what it shows beside its model.
  void setBrowser(const ContentEngine::ProductionBrowser* b) { browser_ = b; }
  void setBrowserInfo(const BrowserInfo& info) { browserInfo_ = info; }
  /// One line over the performance screen for `ms` (rule 6: "SAVED  SET 02  T04").
  void toast(const char* s, uint32_t nowMs, uint32_t ms = 2500) {
    snprintf(toast_, sizeof toast_, "%s", s);
    toastUntil_ = nowMs + ms;
  }

  /// The slot a track shows while it plays nothing (the last one launched or recorded on it).
  void show(uint8_t track, uint8_t slot) {
    if (track < ContentEngine::kTrackCount) shown_[track] = slot;
  }

  /// Once per main-loop pass: a frame when one is due and the previous transfer is done. Returns the frame's draw time
  /// in µs (0: no frame).
  uint32_t service(ContentEngine::EngineRoot& e, const ContentEngine::EngineClock& clock, uint32_t lastTick,
                   uint32_t nowMs, const EditView& edit = EditView{}) {
    edit_ = edit;
    oled_.api.pollDisplayTransfer();
    if (nowMs - lastFrameMs_ < frameMs_ || oled_.api.isDisplayTransferActive()) return 0;
    lastFrameMs_ = nowMs;
    const uint32_t t0 = micros();
    draw(e, clock, lastTick, nowMs);
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
  using Mode = ContentEngine::EngineRoot::Transport::Mode;
  static constexpr uint32_t kBar = ContentEngine::kTicksPerBar;
  static constexpr uint32_t kQuarter = ContentEngine::EngineClock::kTicksPerQuarter;
  static constexpr uint8_t kNone = 0xFF;
  static constexpr int16_t kGap = 5, kGroupGap = 10, kTickW = 54, kBarsW = 18;

  /// What the status band needs from the shown slot (`drawSlot`).
  struct SlotInfo {
    uint8_t slot = kNone;
    uint32_t loop = 0, pos = 0;
    bool moving = false;  // playing or writing: the position is live
  };

  uint8_t* fb() { return oled_.api.getFrameBuffer(); }
  void sendBlocking() { oled_.api.displayBlocking(); }
  void text(const char* s, int16_t x, int16_t y, uint8_t brightness) { oled_.gfx.draw_text(fb(), s, x, y, brightness); }
  void px(int16_t x, int16_t y, uint8_t v) {
    if (x >= 0 && x < DISPLAY_WIDTH && y >= 0 && y < DISPLAY_HEIGHT) oled_.gfx.draw_pixel(fb(), x, y, v);
  }
  /// 5 × 7 text right-aligned so its last column is `xr`.
  void rtext(const char* s, int16_t xr, uint8_t v) {
    text(s, static_cast<int16_t>(xr - static_cast<int16_t>(strlen(s)) * 6 + 2), kValBase, v);
  }
  /// A loop length for LOOP_EDIT's LENGTH: bars, or under a bar its fraction in 16ths reduced ("1/16", "3/4").
  static void lengthText(uint32_t loop, char* out, size_t n) {
    if (loop >= kBar || loop % (kBar / 16) != 0) {
      snprintf(out, n, "%lu", static_cast<unsigned long>((loop + kBar - 1) / kBar));
      return;
    }
    uint32_t num = loop / (kBar / 16), den = 16;
    while (num % 2 == 0 && den > 1) {
      num /= 2;
      den /= 2;
    }
    snprintf(out, n, "%lu/%lu", static_cast<unsigned long>(num), static_cast<unsigned long>(den));
  }
  /// `tenths` as whole.tenth right-aligned so its last column is `xr`, with the legacy thin dot (one pixel in the
  /// spacing column) — "120.5" in 24 columns.
  void rtenths(uint32_t tenths, int16_t xr, uint8_t v) {
    char buf[8];
    snprintf(buf, sizeof buf, "%lu", static_cast<unsigned long>(tenths / 10));
    const int16_t w = static_cast<int16_t>(strlen(buf)) * 6;
    const int16_t tx = static_cast<int16_t>(xr - w - 5);
    text(buf, tx, kValBase, v);
    px(static_cast<int16_t>(tx + w - 1), kValBase - 1, v);
    const char tenth[2] = {static_cast<char>('0' + tenths % 10), 0};
    text(tenth, static_cast<int16_t>(tx + w + 1), kValBase, v);
  }

  // ---- 3 × 5 labels and 5-row icons ----------------------------------------------------------------------------------
  /// Bit 14 is the top-left pixel, rows of 3 (row-major).
  static uint16_t micro(char c) {
    static const struct { char c; uint16_t bits; } kGlyphs[] = {
        {'-', 0x01C0}, {'+', 0x05D0}, {'/', 0x12A4}, {'0', 0x2B6A}, {'1', 0x2C97}, {'2', 0x62A7}, {'3', 0x628E}, {'4', 0x5BC9}, {'5', 0x798E},
        {'6', 0x39AA}, {'7', 0x7292}, {'8', 0x2AAA}, {'9', 0x2ACE}, {'A', 0x2BED}, {'B', 0x6BAE}, {'C', 0x3923},
        {'D', 0x6B6E}, {'E', 0x79A7}, {'F', 0x79A4}, {'G', 0x396B}, {'H', 0x5BED}, {'I', 0x7497}, {'J', 0x126A},
        {'K', 0x5BAD}, {'L', 0x4927}, {'M', 0x5F6D}, {'N', 0x6B6D}, {'O', 0x2B6A}, {'P', 0x6BA4}, {'Q', 0x2B6B},
        {'R', 0x6BAD}, {'S', 0x388E}, {'T', 0x7492}, {'U', 0x5B6F}, {'V', 0x5B6A}, {'W', 0x5BFD}, {'X', 0x5AAD},
        {'Y', 0x5A92}, {'Z', 0x72A7}, {':', 0x0410}, {'.', 0x0002}, {'!', 0x2482}, {'<', 0x1511}, {'>', 0x4454}};
    for (const auto& g : kGlyphs)
      if (g.c == c) return g.bits;
    return 0;
  }
  /// A 3 × 5 label at (`x`, `top`), 4 px a letter; returns the x after it.
  int16_t label(const char* s, int16_t x, int16_t top, uint8_t v) {
    for (; *s; ++s, x += 4) {
      const uint16_t b = micro(*s);
      for (uint8_t i = 0; i < 15; ++i)
        if (b & (1u << (14 - i))) px(static_cast<int16_t>(x + i % 3), static_cast<int16_t>(top + i / 3), v);
    }
    return x;
  }
  /// A 3 × 5 label `k` times its size (the browser's milestone id: 3).
  int16_t label(const char* s, int16_t x, int16_t top, uint8_t v, uint8_t k) {
    for (; *s; ++s, x = static_cast<int16_t>(x + 4 * k)) {
      const uint16_t b = micro(*s);
      for (uint8_t i = 0; i < 15; ++i)
        if (b & (1u << (14 - i)))
          oled_.gfx.draw_rect_filled(fb(), static_cast<int16_t>(x + (i % 3) * k), static_cast<int16_t>(top + (i / 3) * k),
                                     static_cast<int16_t>(x + (i % 3) * k + k - 1),
                                     static_cast<int16_t>(top + (i / 3) * k + k - 1), v);
    }
    return x;
  }
  /// A 3 × 5 label right-aligned so its last column is `xr`.
  void rlabel(const char* s, int16_t xr, int16_t top, uint8_t v) {
    label(s, static_cast<int16_t>(xr - static_cast<int16_t>(strlen(s)) * 4 + 2), top, v);
  }
  enum class Icon : uint8_t { Stop, Play, Rec, Dub, Clock, In };
  /// 5-row icons, one byte a row (bit 4 the left column); `width` columns.
  void icon(Icon i, int16_t x, int16_t top, uint8_t v) {
    static const uint8_t kRows[][5] = {
        {0x00, 0x1E, 0x1E, 0x1E, 0x1E},  // stop: a square (4 wide)
        {0x10, 0x18, 0x1C, 0x18, 0x10},  // play: a triangle (3 wide)
        {0x0E, 0x1F, 0x1F, 0x1F, 0x0E},  // record: a disc
        {0x0E, 0x11, 0x15, 0x11, 0x0E},  // overdub: a ring around a dot
        {0x0E, 0x15, 0x17, 0x11, 0x0E},  // clock
        {0x04, 0x06, 0x1F, 0x06, 0x04},  // an arrow in (the external clock)
    };
    const uint8_t* r = kRows[static_cast<uint8_t>(i)];
    for (uint8_t y = 0; y < 5; ++y)
      for (uint8_t c = 0; c < 5; ++c)
        if (r[y] & (0x10 >> c)) px(static_cast<int16_t>(x + c), static_cast<int16_t>(top + y), v);
  }
  static uint8_t iconWidth(Icon i) { return i == Icon::Stop ? 4 : i == Icon::Play ? 3 : 5; }
  void separator(int16_t x) { oled_.gfx.draw_vline(fb(), x, kLabY - 1, 63, kRule); }
  /// A row from `x0` to `x1` (the library's `draw_hline` takes the row first).
  void hline(int16_t x0, int16_t x1, int16_t y, uint8_t v) { oled_.gfx.draw_hline(fb(), y, x0, x1, v); }

  // ---- Frame ---------------------------------------------------------------------------------------------------------
  void draw(ContentEngine::EngineRoot& e, const ContentEngine::EngineClock& clock, uint32_t lastTick, uint32_t nowMs) {
    oled_.gfx.fill_buffer(fb(), 0);
    if (browser_ != nullptr && browser_->isOpen()) {
      drawBrowser(*browser_, e);
      return;
    }
    auto& x = e.transport();
    const uint8_t sel = e.control().selectedTrack();
    drawTracks(e, clock, sel, nowMs);
    // The shown slot: the selected track's playing (or writing) slot, else the one it last showed.
    uint8_t slot = x.playing(sel) ? x.playingSlot(sel) : shown_[sel];
    if (x.writingTrack() == sel) slot = x.writingSlot();
    SlotInfo info;
    if (slot < ContentEngine::Config::kSlotCount) {
      if (slot != focus_) {  // span tiles for the shown slot (swept first)
        e.setDisplayFocus(slot, slot);
        focus_ = slot;
      }
      info = drawSlot(e, sel, slot, lastTick);
    }
    drawStatus(e, clock, sel, info, lastTick, nowMs);
    if (edit_.popup) drawPopup();
    if (static_cast<int32_t>(toastUntil_ - nowMs) > 0) drawToast();
  }

  // ---- The sets browser (slice 3; layout: proposal §4, owner 2026-11-09: the id largest, the stage beneath, the date
  // third) ---------------------------------------------------------------------------------------------------------
  using Browser = ContentEngine::ProductionBrowser;
  using Row = Browser::Row;
  using RowKind = Browser::RowKind;
  static constexpr int16_t kListX = 24, kRuleX = 112, kDetailX = 116, kMosaicX = 223, kHintY = 58;
  // Slice 5: the timeline strip on rows 0–8; the list and the detail below it (proposal walkthroughs).
  static constexpr int16_t kTop = 11, kRowY = 12, kRowPitch = 7;
  static void idText(const ContentEngine::library::Record& r, char* out, size_t n) {
    snprintf(out, n, "%c%02u", ContentEngine::library::stageLetter(r.stage), r.stageNumber);
  }
  static uint32_t bars(uint32_t ticks) { return (ticks + kBar - 1) / kBar; }
  // Levels (owner 2026-11-09: the dimmest lines did not show on the panel): rules 4, dim text 7, an empty mosaic cell 3.
  static constexpr uint8_t kRuleLevel = 4, kDimText = 7;
  void drawBrowser(const Browser& b, ContentEngine::EngineRoot& e) {
    engine_ = &e;
    for (int16_t y = 10; y < 55; ++y) px(kRuleX, y, kRuleLevel);
    hline(0, 255, 55, kRuleLevel);
    drawStrip(b);
    drawLanes(b);
    const uint16_t n = b.rowCount();
    for (uint16_t i = b.first(); i < n && i < b.first() + Browser::kVisible; ++i)
      drawRow(b, b.row(i), i > 0 ? b.row(static_cast<uint16_t>(i - 1)) : b.row(i), rowY(b, i), i == b.selected(),
              i > 0, i);
    const Row sel = b.row(b.selected());
    char hint[48];
    if (b.view() == Browser::View::Failed) {
      drawTitle(b.failWhat());
      label(b.failWhy(), kDetailX, 24, 9);
      label(b.failChanged() ? "SET NOT AS IT WAS" : "NOTHING CHANGED", kDetailX, 31, 9);  // under the title at kTop
      snprintf(hint, sizeof hint, "TRACK: TRY AGAIN  HOLD: CLOSE");
    } else {
      drawDetail(b, sel, hint, sizeof hint);
    }
    label(hint, 2, kHintY, 9);
    if (b.view() == Browser::View::Popup) drawSavePopup(b);
  }
  /// A list row; a milestone's date only on the first of its day (the row above another day) and only where it fits.
  static int16_t rowY(const Browser& b, uint16_t i) { return static_cast<int16_t>(kRowY + (i - b.first()) * kRowPitch); }
  /// The lanes' x: one lane at 18; with a side branch the line at 20 and the side at 12 (walkthrough 5).
  static int16_t laneX(const Browser& b, uint16_t i) {
    if (!b.hasSide()) return 18;
    return b.onLine(i) ? 20 : 12;
  }
  bool setShown(const Browser& b) const {
    return b.view() == Browser::View::Set ||
           ((b.view() == Browser::View::Popup || b.view() == Browser::View::Failed) && b.listView() == Browser::View::Set);
  }
  /// Slice 5, the fork lanes: the line from the newest milestone down its parents, the side branch on its own lane
  /// joined where it grew from (drawn under the dots; clipped to the rows shown).
  void drawLanes(const Browser& b) {
    if (!setShown(b)) return;
    const uint16_t n = b.rowCount(), last = static_cast<uint16_t>(b.first() + Browser::kVisible - 1);
    int16_t lineTop = -1, lineBottom = -1, sideTop = -1, sideBottom = -1;
    for (uint16_t i = 1; i < n; ++i) {
      const int16_t y = static_cast<int16_t>(rowY(b, i) + 2);
      if (b.onLine(i)) {
        if (lineTop < 0) lineTop = y;
        lineBottom = y;
      } else {
        if (sideTop < 0) sideTop = y;
        sideBottom = y;
      }
    }
    const uint16_t join = b.joinRow();
    if (b.hasSide() && join != 0) sideBottom = static_cast<int16_t>(rowY(b, join) + 2);
    auto clip = [&](int16_t y) { return y < kRowY ? kRowY : y > rowY(b, last) + 4 ? static_cast<int16_t>(rowY(b, last) + 4) : y; };
    if (lineTop >= 0)
      for (int16_t y = clip(lineTop); y <= clip(lineBottom); ++y) px(b.hasSide() ? 20 : 18, y, kDimText);
    if (sideTop >= 0 && b.hasSide()) {
      for (int16_t y = clip(sideTop); y <= clip(sideBottom); ++y) px(12, y, kDimText);
      if (join != 0 && join >= b.first() && join <= last) hline(12, 20, sideBottom, kDimText);
    }
  }
  /// Slice 5, the timeline strip: months along the time from the first milestone shown to now (at least two weeks), a
  /// tick for each day with milestones (a count when more), the selected one between two bright marks.
  void drawStrip(const Browser& b) {
    hline(0, 255, 8, kRuleLevel);
    const bool inSet = setShown(b);
    uint32_t t0 = 0xFFFFFFFFu, t1 = ContentEngine::wallclock::valid(browserInfo_.now) ? browserInfo_.now : 0;
    for (uint16_t i = 0; i < b.count(); ++i) {
      const auto& r = b.record(i);
      if (!ContentEngine::wallclock::valid(r.wallTime) || (inSet && r.set != b.openSetNumber())) continue;
      if (r.wallTime < t0) t0 = r.wallTime;
      if (r.wallTime > t1) t1 = r.wallTime;
    }
    if (t0 == 0xFFFFFFFFu) return;  // nothing dated
    constexpr uint32_t kMinSpan = 14u * 86400u;
    if (t1 - t0 < kMinSpan) t0 = t1 - kMinSpan;
    const uint32_t span = t1 - t0 + 86400u;
    auto xOf = [&](uint32_t t) { return static_cast<int16_t>(4 + static_cast<uint64_t>(t - t0) * 247u / span); };
    // Months: a label at each month's first day in range (not crowding the one before).
    const auto first = ContentEngine::wallclock::local(t0, browserInfo_.tz);
    int16_t lastLabel = -100;
    for (int16_t y = first.year, m = first.month;; ) {
      const int64_t day = ContentEngine::wallclock::dayOf(y, m, 1);
      const int64_t at = day * 86400 - static_cast<int64_t>(browserInfo_.tz) * 60;
      if (at > static_cast<int64_t>(t1)) break;
      const int16_t x = at < static_cast<int64_t>(t0) ? 0 : xOf(static_cast<uint32_t>(at));
      if (x - lastLabel >= 16 && x <= 240) {
        label(ContentEngine::wallclock::monthName(static_cast<uint8_t>(m)), x, 0, kDimText);
        lastLabel = x;
      }
      if (++m > 12) {
        m = 1;
        ++y;
      }
    }
    // Days: milestones counted per column.
    uint8_t count[256] = {};
    for (uint16_t i = 0; i < b.count(); ++i) {
      const auto& r = b.record(i);
      if (!ContentEngine::wallclock::valid(r.wallTime) || r.wallTime < t0 || (inSet && r.set != b.openSetNumber())) continue;
      const int16_t x = xOf(r.wallTime);
      if (count[x] < 255) ++count[x];
    }
    for (int16_t x = 0; x < 256; ++x) {
      if (count[x] == 0) continue;
      if (count[x] == 1) {
        px(x, 6, 9);
        px(x, 7, 9);
        continue;
      }
      oled_.gfx.draw_rect_filled(fb(), x, 5, static_cast<int16_t>(x + 1), 7, 9);
      const char digit[2] = {static_cast<char>(count[x] > 9 ? '+' : '0' + count[x]), 0};
      label(digit, static_cast<int16_t>(x + 3), 3, 9);
    }
    const Row sel = b.row(b.selected());
    const uint16_t rec = sel.kind == RowKind::Milestone || sel.kind == RowKind::Set || sel.kind == RowKind::Working
                             ? sel.record : Browser::kNone;
    if (rec != Browser::kNone && ContentEngine::wallclock::valid(b.record(rec).wallTime) && b.record(rec).wallTime >= t0) {
      const int16_t x = xOf(b.record(rec).wallTime);
      for (int16_t y = 3; y <= 7; ++y) {
        px(static_cast<int16_t>(x - 2), y, 15);
        px(static_cast<int16_t>(x + 2), y, 15);
      }
    }
  }
  void drawRow(const Browser& b, const Row& r, const Row& above, int16_t y, bool on, bool hasAbove, uint16_t index) {
    if (on) oled_.gfx.draw_rect_filled(fb(), 0, static_cast<int16_t>(y - 1), kRuleX - 1, static_cast<int16_t>(y + 5), 3);
    const uint8_t v = on ? 15 : 9, nv = on ? 15 : kDimText;
    char text[28] = {}, note[16] = {};
    switch (r.kind) {
      case RowKind::Return: snprintf(text, sizeof text, "< RETURN TO PREVIOUS"); break;
      case RowKind::Save: snprintf(text, sizeof text, "+ SAVE MILESTONE"); break;
      case RowKind::SaveNewSet: snprintf(text, sizeof text, "+ SAVE AS NEW SET"); break;
      case RowKind::Back: snprintf(text, sizeof text, "< ALL SETS"); break;
      case RowKind::Working:
        snprintf(text, sizeof text, "SET %02u NOW", r.set);
        // The time alone ("14:32"; UNSAVED as it is): SAVED and the time do not fit beside the set.
        snprintf(note, sizeof note, "%s", strncmp(browserInfo_.saved, "SAVED ", 6) == 0 ? browserInfo_.saved + 6
                                                                                         : browserInfo_.saved);
        dotAt(18, static_cast<int16_t>(y + 2), 13, true);
        break;
      case RowKind::Set: {
        const auto& m = b.record(r.record);
        char id[8];
        idText(m, id, sizeof id);
        snprintf(text, sizeof text, "SET %02u  %s", r.set, id);
        ContentEngine::wallclock::formatDay(m.wallTime, browserInfo_.tz, note, sizeof note);
        break;
      }
      case RowKind::Milestone: {
        const auto& m = b.record(r.record);
        char id[8];
        idText(m, id, sizeof id);
        snprintf(text, sizeof text, "%s  %s", id, ContentEngine::library::stageName(m.stage));
        ContentEngine::wallclock::formatDay(m.wallTime, browserInfo_.tz, note, sizeof note);
        if (hasAbove && above.kind == RowKind::Milestone) {  // the same day as the row above, shown there: not again
          const auto& a = b.record(above.record);
          char day[16], aboveText[28];
          ContentEngine::wallclock::formatDay(a.wallTime, browserInfo_.tz, day, sizeof day);
          snprintf(aboveText, sizeof aboveText, "%c%02u  %s", ContentEngine::library::stageLetter(a.stage),
                   a.stageNumber, ContentEngine::library::stageName(a.stage));
          if (strcmp(day, note) == 0 && fits(aboveText, day)) note[0] = 0;
        }
        dotAt(laneX(b, index), static_cast<int16_t>(y + 2), 13, false);
        break;
      }
    }
    label(text, kListX, y, v);
    if (note[0] != 0 && strcmp(note, "--") != 0 && fits(text, note)) rlabel(note, kRuleX - 3, y, nv);
  }
  /// A row's right-hand note fits beside its text.
  static bool fits(const char* text, const char* note) {
    return static_cast<int16_t>(strlen(note)) * 4 <= kRuleX - 3 - kListX - static_cast<int16_t>(strlen(text)) * 4 - 6;
  }
  void dotAt(int16_t x, int16_t y, uint8_t v, bool ring) {
    for (int16_t dy = -1; dy <= 1; ++dy)
      for (int16_t dx = -1; dx <= 1; ++dx)
        if (!ring || dx != 0 || dy != 0) px(static_cast<int16_t>(x + dx), static_cast<int16_t>(y + dy), v);
  }
  /// An action's word, twice the label size (SAVE, RETURN, LOAD FAILED).
  void drawTitle(const char* s) { label(s, kDetailX, kTop, 15, 2); }
  /// A milestone: its id largest with the set beside it, the stage beneath, the date, loops and bars; the mosaic.
  void drawMilestone(const Browser& b, uint16_t i, const char* stageLine, const char* infoLine) {
    const auto& m = b.record(i);
    char id[8], set[12], date[24];
    idText(m, id, sizeof id);
    const int16_t x = label(id, kDetailX, kTop, 15, 3);
    snprintf(set, sizeof set, "SET %02u", m.set);
    label(set, static_cast<int16_t>(x + 3), kTop, 9);
    label(stageLine, kDetailX, 30, 13);
    ContentEngine::wallclock::formatStamp(m.wallTime, browserInfo_.tz, date, sizeof date);
    label(date, kDetailX, 38, 10);
    label(infoLine, kDetailX, 46, kDimText);
    drawMosaic(m);
  }
  /// The 8 × 8 slots (rows tracks, columns slots), brighter for more content: a milestone's, from its record.
  void drawMosaic(const ContentEngine::library::Record& m) {
    uint8_t lv[64];
    for (uint8_t k = 0; k < 64; ++k) lv[k] = static_cast<uint8_t>((m.mosaic[k / 4] >> ((k % 4) * 2)) & 3);
    drawCells(lv);
  }
  /// The working set's, live: each slot's rows against the fullest slot's, as a milestone's are stored.
  void drawLiveMosaic() {
    if (engine_ == nullptr) return;
    auto& e = *engine_;
    uint32_t rows[64] = {}, most = 0;
    for (uint8_t s = 0; s < 64; ++s) {
      const ContentEngine::ManifestId id = e.transport().contentOf(s);
      if (id == ContentEngine::kNoManifest) continue;
      const auto& m = e.manifests().manifest(id);
      for (uint8_t k = 0; k < m.runCount; ++k) rows[s] += e.runs().run(m.runs[k]).rowCount;
      if (rows[s] == 0) rows[s] = 1;  // an empty loop still shows
      if (rows[s] > most) most = rows[s];
    }
    uint8_t lv[64];
    for (uint8_t s = 0; s < 64; ++s) lv[s] = rows[s] == 0 ? 0 : static_cast<uint8_t>(1 + (rows[s] * 3 - 1) / most);
    drawCells(lv);
  }
  void drawCells(const uint8_t* lv) {
    static const uint8_t kLevel[4] = {0, 7, 11, 15};
    for (uint8_t t = 0; t < 8; ++t)
      for (uint8_t s = 0; s < 8; ++s) {
        const uint8_t l = lv[t * 8 + s];
        const int16_t x = static_cast<int16_t>(kMosaicX + s * 4), y = static_cast<int16_t>(kTop + t * 4);
        if (l == 0) px(static_cast<int16_t>(x + 1), static_cast<int16_t>(y + 1), 3);
        else oled_.gfx.draw_rect_filled(fb(), x, y, static_cast<int16_t>(x + 2), static_cast<int16_t>(y + 2), kLevel[l & 3]);
      }
  }
  void drawDetail(const Browser& b, const Row& r, char* hint, size_t n) {
    char a[40], c[40];
    switch (r.kind) {
      case RowKind::Return:
        drawTitle("RETURN");
        label(browserInfo_.returnTo, kDetailX, 24, 9);
        label("NOT A MILESTONE", kDetailX, 31, kDimText);
        snprintf(hint, n, "TRACK: RETURN  HOLD: CLOSE");
        return;
      case RowKind::Save:
      case RowKind::SaveNewSet: {
        const bool fresh = r.kind == RowKind::SaveNewSet;
        drawTitle(fresh ? "NEW SET" : "SAVE");
        snprintf(a, sizeof a, "INTO SET %02u", fresh ? b.newSetNumber() : b.workingSet());
        label(a, kDetailX, 24, 9);
        const uint16_t base = b.find(b.baseSequence());
        if (base != Browser::kNone) {
          char id[8];
          idText(b.record(base), id, sizeof id);
          snprintf(c, sizeof c, "FROM %s", id);
        } else {
          snprintf(c, sizeof c, "NEW WORK");
        }
        label(c, kDetailX, 31, 10);
        label(browserInfo_.saved, kDetailX, 38, kDimText);
        drawLiveMosaic();
        snprintf(hint, n, "TRACK: CHOOSE STAGE  HOLD: CLOSE");
        return;
      }
      case RowKind::Back:
        drawTitle("ALL SETS");
        drawLiveMosaic();
        snprintf(hint, n, "TRACK: ALL SETS  HOLD: CLOSE");
        return;
      case RowKind::Working:
      case RowKind::Set: {
        snprintf(hint, n, "TRACK: OPEN SET %02u  HOLD: CLOSE", r.set);
        if (r.record == Browser::kNone) {
          drawTitle("NOW");
          snprintf(a, sizeof a, "SET %02u", r.set);
          label(a, kDetailX, 24, 9);
          label("NO MILESTONES YET", kDetailX, 31, kDimText);
          drawLiveMosaic();
          return;
        }
        snprintf(a, sizeof a, "%s  LATEST", ContentEngine::library::stageName(b.record(r.record).stage));
        snprintf(c, sizeof c, "%u MILESTONES", b.milestonesIn(r.set));
        drawMilestone(b, r.record, a, c);
        return;
      }
      case RowKind::Milestone: {
        const auto& m = b.record(r.record);
        const uint16_t parent = m.parent != 0 ? b.find(m.parent) : Browser::kNone;
        if (parent != Browser::kNone) {
          char pid[8];
          idText(b.record(parent), pid, sizeof pid);
          snprintf(a, sizeof a, "%s  FROM %s", ContentEngine::library::stageName(m.stage), pid);
        } else {
          snprintf(a, sizeof a, "%s", ContentEngine::library::stageName(m.stage));
        }
        const uint32_t lo = bars(m.shortestLoop), hi = bars(m.longestLoop);
        if (lo == hi) snprintf(c, sizeof c, "%u LOOPS  %lu BARS", m.loops, static_cast<unsigned long>(hi));
        else snprintf(c, sizeof c, "%u LOOPS  %lu-%lu BARS", m.loops, static_cast<unsigned long>(lo), static_cast<unsigned long>(hi));
        drawMilestone(b, r.record, a, c);
        char id[8];
        idText(m, id, sizeof id);
        snprintf(hint, n, "TRACK: LOAD %s  HOLD: CLOSE", id);
        return;
      }
    }
  }
  /// The save popup (proposal §3): the six stages with the id each would get, one highlighted, one SUGGESTED.
  void drawSavePopup(const Browser& b) {
    constexpr int16_t x0 = 56, y0 = 1, x1 = 200, y1 = 56;
    oled_.gfx.draw_rect_filled(fb(), x0 + 3, y0 + 3, x1 + 3, y1 + 3, 2);
    oled_.gfx.draw_rect_filled(fb(), x0, y0, x1, y1, 0);
    for (int16_t x = x0; x <= x1; ++x) {
      px(x, y0, 10);
      px(x, static_cast<int16_t>(y0 + 9), 10);
      px(x, y1, 10);
    }
    for (int16_t y = y0; y <= y1; ++y) {
      px(x0, y, 10);
      px(x1, y, 10);
    }
    for (int16_t y = static_cast<int16_t>(y0 + 2); y < y0 + 9; y += 2) hline(x0 + 1, x1 - 1, y, 5);
    char title[24];
    snprintf(title, sizeof title, b.popupNewSet() ? "SAVE AS SET %02u" : "SAVE TO SET %02u", b.popupSet());
    const int16_t tw = static_cast<int16_t>(strlen(title)) * 4 + 6, tx = static_cast<int16_t>((x0 + x1 - tw) / 2);
    oled_.gfx.draw_rect_filled(fb(), tx, static_cast<int16_t>(y0 + 1), static_cast<int16_t>(tx + tw), y0 + 8, 0);
    label(title, static_cast<int16_t>(tx + 3), static_cast<int16_t>(y0 + 2), 13);
    for (uint8_t i = 0; i < Browser::kStages; ++i) {
      const auto stage = static_cast<ContentEngine::library::Stage>(i);
      const int16_t y = static_cast<int16_t>(y0 + 13 + i * 7);
      const bool on = i == b.popupSelected();
      if (on) oled_.gfx.draw_rect_filled(fb(), x0 + 2, static_cast<int16_t>(y - 1), x1 - 2, static_cast<int16_t>(y + 5), 13);
      label(ContentEngine::library::stageName(stage), x0 + 6, y, on ? 0 : 7);
      char id[8];
      snprintf(id, sizeof id, "%c%02u", ContentEngine::library::stageLetter(stage), b.nextNumber(b.popupSet(), stage));
      label(id, x0 + 58, y, on ? 0 : 9);
      if (stage == b.suggested())  // a Take inferred is only ever provisional (proposal §3)
        rlabel(b.suggestedProvisional() ? "SUGGESTED?" : "SUGGESTED", x1 - 4, y, on ? 0 : 5);
    }
    oled_.gfx.draw_rect_filled(fb(), 0, kHintY - 1, 255, 63, 0);
    label("REC/EDIT STAGE  TRACK SAVE  HOLD CANCEL", 2, kHintY, 9);
  }
  /// The one line after a save, load or return (rule 6), over the performance screen.
  void drawToast() {
    const int16_t tw = static_cast<int16_t>(strlen(toast_)) * 4 + 12, x0 = static_cast<int16_t>((256 - tw) / 2);
    oled_.gfx.draw_rect_filled(fb(), x0 + 2, 16, static_cast<int16_t>(x0 + tw + 2), 32, 2);
    oled_.gfx.draw_rect_filled(fb(), x0, 14, static_cast<int16_t>(x0 + tw), 30, 0);
    for (int16_t x = x0; x <= x0 + tw; ++x) {
      px(x, 14, 10);
      px(x, 30, 10);
    }
    for (int16_t y = 14; y <= 30; ++y) {
      px(x0, y, 10);
      px(static_cast<int16_t>(x0 + tw), y, 10);
    }
    label(toast_, static_cast<int16_t>(x0 + 6), 20, 15);
  }

  /// The length popup (owner 2026-11-08: "a shadow border and look/feel the same like cubase/atari", the main screen
  /// around it): a window over the roll — a dim drop shadow (black would vanish on the dark screen), a frame, a GEM
  /// title bar (hatched rows, the title in its own black box), the length large, the gestures in small letters. Kept
  /// light (owner 2026-11-08: "the screen gets all dimmed"): the panel dims as a whole as its total load rises — a
  /// filled bright title bar and grey interior took the frame's summed levels from ~5,000 to ~25,000.
  void drawPopup() {
    constexpr int16_t x0 = 72, y0 = 6, x1 = 183, y1 = 41, sh = 3;
    oled_.gfx.draw_rect_filled(fb(), x0 + sh, y0 + sh, x1 + sh, y1 + sh, 2);  // the shadow
    oled_.gfx.draw_rect_filled(fb(), x0, y0, x1, y1, 0);                      // the window: black
    for (int16_t y = static_cast<int16_t>(y0 + 2); y < y0 + 9; y += 2)        // the title bar: hatched
      for (int16_t x = static_cast<int16_t>(x0 + 1); x < x1; ++x) px(x, y, 5);
    for (int16_t x = x0; x <= x1; ++x) {                                      // the frame and the title rule
      px(x, y0, 10);
      px(x, static_cast<int16_t>(y0 + 9), 10);
      px(x, y1, 10);
    }
    for (int16_t y = y0; y <= y1; ++y) {
      px(x0, y, 10);
      px(x1, y, 10);
    }
    char title[24];
    snprintf(title, sizeof title, "LOOP LENGTH T%u S%u", edit_.popupSlot / 8 + 1, edit_.popupSlot % 8 + 1);
    const int16_t tw = static_cast<int16_t>(strlen(title)) * 4 + 3, tx = static_cast<int16_t>((x0 + x1 - tw) / 2);
    oled_.gfx.draw_rect_filled(fb(), tx, static_cast<int16_t>(y0 + 1), static_cast<int16_t>(tx + tw), y0 + 8, 0);
    label(title, static_cast<int16_t>(tx + 2), static_cast<int16_t>(y0 + 2), 13);
    char value[16], len[8];
    lengthText(edit_.popupLoop, len, sizeof len);
    snprintf(value, sizeof value, "%s %s", len, edit_.popupLoop > kBar ? "BARS" : "BAR");
    const int16_t w = static_cast<int16_t>(strlen(value)) * 6 - 1;
    text(value, static_cast<int16_t>((x0 + x1 - w) / 2), static_cast<int16_t>(y0 + 23), 15);
    static const char kHint[] = "PRESS +  2X -  HOLD CANCEL";  // next length, the one before, back as opened
    label(kHint, static_cast<int16_t>((x0 + x1 - static_cast<int16_t>(sizeof kHint - 1) * 4) / 2),
          static_cast<int16_t>(y1 - 7), 5);
  }

  /// The legacy track letter for track `t`.
  static char letter(ContentEngine::EngineRoot& e, uint8_t t, uint8_t sel, uint8_t shown) {
    auto& x = e.transport();
    auto& c = e.control();
    if (t != sel && hidden(e, t)) return 'M';
    if (x.writingTrack() == t) return x.mode() == Mode::Recording ? 'R' : 'O';
    if (c.queuedRecordSlot(t) != kNone) return 'A';
    if (x.playing(t)) return 'P';
    // Stopped: the selected slot's content (a cancelled arm on an empty slot is '-' again).
    return shown < ContentEngine::Config::kSlotCount && x.contentOf(shown) != ContentEngine::kNoManifest ? 'S' : '-';
  }
  /// Muted, or silent under another track's solo.
  static bool hidden(ContentEngine::EngineRoot& e, uint8_t t) {
    auto& c = e.control();
    bool anySolo = false;
    for (uint8_t i = 0; i < 8; ++i) anySolo |= c.soloed(i);
    return c.muted(t) || (anySolo && !c.soloed(t));
  }
  /// 1 Hz between 7 and 13 (legacy 4–10; owner 2026-11-08: readable at its darkest): the selected track's state letter,
  /// NOTE_EDIT's mode cell behind its dark (0) label.
  static uint8_t pulseAt(uint32_t nowMs) {
    const float phase = static_cast<float>(nowMs % 1000) / 1000.0f;
    return static_cast<uint8_t>(7.0f + 6.0f * (0.5f + 0.5f * sinf(phase * 2.0f * 3.1415926f)));
  }
  void drawTracks(ContentEngine::EngineRoot& e, const ContentEngine::EngineClock& clock, uint8_t sel, uint32_t nowMs) {
    char label[2] = {0, 0};
    const uint8_t pulse = pulseAt(nowMs);
    const uint32_t nowUs = micros();
    for (uint8_t t = 0; t < 8; ++t) {
      const int16_t y = static_cast<int16_t>(t * 8 + 7);
      label[0] = static_cast<char>('1' + t);
      text(label, 0, y, t == sel ? 15 : 4);
      if (clock.running()) {  // the activity level: 7 rows, one dark row between tracks
        const uint8_t n = static_cast<uint8_t>((e.output().level(t, nowUs) * 14u + 63u) / 127u);
        const bool dim = hidden(e, t);
        for (uint8_t i = 0; i < n; ++i) {
          const uint8_t v = dim ? 2 : i + 1 == n ? (t == sel ? 15 : 9) : (t == sel ? 10 : 6);
          oled_.gfx.draw_vline(fb(), static_cast<int16_t>(7 + i), static_cast<int16_t>(t * 8),
                               static_cast<int16_t>(t * 8 + 6), v);
        }
      } else {
        label[0] = letter(e, t, sel, shown_[t]);
        text(label, 10, y, t == sel ? pulse : 6);
      }
    }
  }

  SlotInfo drawSlot(ContentEngine::EngineRoot& e, uint8_t track, uint8_t slot, uint32_t lastTick) {
    auto& x = e.transport();
    SlotInfo info;
    info.slot = slot;
    const bool writing = x.writingSlot() == slot;
    const ContentEngine::ManifestId m = x.contentOf(slot);  // a cleared slot (an empty state) shows as empty
    uint32_t loop = m == ContentEngine::kNoManifest ? 0 : e.manifests().manifest(m).loopLengthTicks;
    uint32_t pos = 0;
    const bool playing = x.playing(track) && x.playingSlot(track) == slot;
    bool firstTake = false;
    if (writing) {
      pos = x.writePosition(lastTick);
      if (loop == 0 || loop > 256 * kBar) {  // a first take: the view grows with it (legacy)
        loop = pos == 0 ? 1 : pos;
        firstTake = true;
      }
    } else if (playing && loop != 0) {
      pos = (lastTick - x.origin(track)) % loop;
    }
    info.loop = loop;
    info.pos = pos;
    info.moving = (playing || writing) && loop != 0;
    if (loop == 0) return info;
    const uint32_t bars = (loop + kBar - 1) / kBar;
    const bool editing = edit_.active && edit_.slot == slot;
    uint32_t viewTicks = loop < kViewBars * kBar ? loop : kViewBars * kBar;
    // A first take's view ends at the current tick (the playhead at the right edge, as legacy); else the page holding
    // the playhead.
    uint32_t first = firstTake ? loop - viewTicks : (pos / viewTicks) * viewTicks;
    if (editing && edit_.ready && edit_.ticks != 0) {  // NOTE_EDIT: the session's window, frozen
      first = edit_.first;
      viewTicks = edit_.ticks;
    }
    // Pitch range: the view's bars, from density tiles (as the legacy auto-fit).
    uint8_t lo, hi;
    const uint8_t firstBar = static_cast<uint8_t>(first / kBar);
    uint8_t lastBar = static_cast<uint8_t>((first + viewTicks - 1) / kBar);
    if (lastBar >= bars) lastBar = static_cast<uint8_t>(bars - 1);
    if (!ContentEngine::pitchExtent(e.tiles(), slot, firstBar, lastBar, lo, hi)) {
      lo = 60;
      hi = 72;
    }
    if (editing && edit_.note) {  // the edited note stays in range
      if (edit_.notePitch < lo) lo = edit_.notePitch;
      if (edit_.notePitch > hi) hi = edit_.notePitch;
    }
    const int32_t w = kX1 - kX0;
    uint8_t olo, ohi;
    if (writing && e.overlay().extent(olo, ohi)) {  // the take's notes so far stay in range
      if (!ContentEngine::pitchExtent(e.tiles(), slot, firstBar, lastBar, lo, hi)) {
        lo = olo;
        hi = ohi;
      }
      if (olo < lo) lo = olo;
      if (ohi > hi) hi = ohi;
      if (hi == lo) hi = static_cast<uint8_t>(lo + 1);
    }
    if (hi == lo) hi = static_cast<uint8_t>(lo + 1);
    // Grid: bar lines, and beat lines dim up to 4 bars.
    const bool beats = viewTicks <= 4 * kBar;
    for (uint32_t b = 0; b * (kBar / 4) < viewTicks; ++b) {
      if (!beats && b % 4 != 0) continue;
      const int16_t gx = static_cast<int16_t>(kX0 + static_cast<int32_t>(b * (kBar / 4)) * w / static_cast<int32_t>(viewTicks));
      oled_.gfx.draw_vline(fb(), gx, kRollY0, kRollY1, b % 4 == 0 ? kRule : 1);
    }
    RollView v{slot, loop, kBar, first, viewTicks, lo, hi, kX0, kX1, kRollY0, kRollY1};
    if (editing && edit_.note) {
      v.highlightTick = edit_.noteTick;
      v.highlightLength = edit_.noteLength;
      v.highlightPitch = edit_.notePitch;
    }
    e.drawRollBand(v, fb(), DISPLAY_WIDTH, kRollY0, kRollY1);
    if (playing || writing) {
      const int16_t px = static_cast<int16_t>(kX0 + static_cast<int64_t>(pos - first) * w / viewTicks);
      oled_.gfx.draw_vline(fb(), px, kRollY0, kRollY1, 15);
    }
    if (editing && edit_.ready && edit_.bracket >= first && edit_.bracket < first + viewTicks) {  // the bracket, dotted
      const int16_t bx = static_cast<int16_t>(kX0 + static_cast<int64_t>(edit_.bracket - first) * w / viewTicks);
      for (int16_t y = kRollY0; y <= kRollY1; y += 2) oled_.gfx.draw_pixel(fb(), bx, y, 10);
    }
    return info;
  }

  // ---- Status band ---------------------------------------------------------------------------------------------------
  /// `bar.beat.16th.tick` right-aligned at `xr` with 1-px dots (the bar at `barV`, the rest at `v`; `sign`: a leading
  /// `-`). Unknown: `-.-.-.--`.
  void position(const ContentEngine::status::Position* p, int16_t xr, uint8_t barV, uint8_t v, bool sign) {
    char part[4][12];
    if (p != nullptr) {
      snprintf(part[0], sizeof part[0], "%lu", static_cast<unsigned long>(p->bar));
      snprintf(part[1], sizeof part[1], "%u", p->beat);
      snprintf(part[2], sizeof part[2], "%u", p->sixteenth);
      snprintf(part[3], sizeof part[3], "%02u", p->tick);
    } else {
      strcpy(part[0], "-");
      strcpy(part[1], "-");
      strcpy(part[2], "-");
      strcpy(part[3], "--");
    }
    int16_t w = -1;
    for (const auto& s : part) w = static_cast<int16_t>(w + static_cast<int16_t>(strlen(s)) * 6);
    w = static_cast<int16_t>(w + 3 * 2);
    int16_t x = static_cast<int16_t>(xr - w + 1);
    if (sign) text("-", static_cast<int16_t>(x - 6), kValBase, barV);
    for (uint8_t i = 0; i < 4; ++i) {
      text(part[i], x, kValBase, i == 0 ? barV : v);
      x = static_cast<int16_t>(x + static_cast<int16_t>(strlen(part[i])) * 6);
      if (i < 3) {
        px(x, kValBase - 1, v > 6 ? v / 2 : 3);
        x = static_cast<int16_t>(x + 2);
      }
    }
  }

  void drawStatus(ContentEngine::EngineRoot& e, const ContentEngine::EngineClock& clock, uint8_t sel,
                  const SlotInfo& info, uint32_t lastTick, uint32_t nowMs) {
    namespace st = ContentEngine::status;
    auto& x = e.transport();
    auto& c = e.control();
    const auto& p = e.persistence();
    const bool noteEdit = edit_.active && edit_.slot == info.slot;
    const bool loopEdit = edit_.loop && !edit_.active;
    char buf[12];
    // ---- The current tick ----
    int16_t x0 = kX0;
    const int16_t xr = static_cast<int16_t>(x0 + kTickW - 1);
    const uint32_t queued = c.ticksToQueued(sel, lastTick);
    if (noteEdit) {
      label("START", x0, kLabY, 12);
      if (edit_.ready && edit_.note) {
        const st::Position at = st::position(edit_.length ? edit_.bracket : edit_.noteTick, kBar, kQuarter);
        position(&at, xr, 15, 15, false);
      } else {
        position(nullptr, xr, 4, 4, false);
      }
    } else {
      // Save dots, or RO before saving is approved.
      if (!started_) {
        dots_.start(p.stats().saves, p.stats().saveFailed);
        started_ = true;
      }
      bool dots = true;  // the dots (or RO) take the row; else the loop's progress does
      if (!p.approved()) {
        label("RO", x0, kLabY, 4);
      } else {
        const st::SaveDots d = dots_.update(nowMs, p.writing(), p.unsaved(), p.stats().saves, p.stats().saveFailed);
        dots = d != st::SaveDots::Off;
        for (uint8_t i = 0; i < 4 && d != st::SaveDots::Off; ++i) {
          const uint8_t v = d == st::SaveDots::Pending ? 2
                            : d == st::SaveDots::Busy  ? (st::SaveIndicator::step(nowMs) == i ? 8 : 2)
                            : d == st::SaveDots::Done  ? 15
                                                       : 6;
          px(static_cast<int16_t>(x0 + i * 3), kLabY + 2, v);
        }
      }
      // The transport icon, right-aligned.
      Icon ic = Icon::Stop;
      uint8_t iv = 10;
      if (x.writingTrack() == sel) {
        ic = x.mode() == Mode::Recording ? Icon::Rec : Icon::Dub;
        iv = 13;
      } else if (c.queuedRecordSlot(sel) != kNone) {
        ic = Icon::Rec;  // armed: the record disc, pulsing (never full brightness)
        const float phase = static_cast<float>(nowMs % 1000) / 1000.0f;
        iv = static_cast<uint8_t>(3.0f + 6.0f * (0.5f + 0.5f * sinf(phase * 2.0f * 3.1415926f)));
      } else if (x.playing(sel)) {
        ic = Icon::Play;
        iv = queued != 0 ? 6 : 10;
      } else if (queued != 0) {
        ic = Icon::Play;
        iv = 6;
      }
      const int16_t ix = static_cast<int16_t>(xr - iconWidth(ic) + 1);
      icon(ic, ix, kLabY, iv);
      if (!dots && info.moving && queued == 0) {  // the loop's progress, from the left up to the icon
        const int16_t end = static_cast<int16_t>(ix - 3);
        hline(x0, end, kLabY + 2, 3);
        hline(x0, static_cast<int16_t>(x0 + static_cast<int64_t>(info.pos) * (end - x0) / info.loop), kLabY + 2, 13);
      }
      if (queued != 0) {
        const st::Position left = st::countdown(queued, kBar, kQuarter);
        position(&left, xr, 13, 8, true);
      } else if (info.moving) {
        const st::Position at = st::position(info.pos, kBar, kQuarter);
        position(&at, xr, 13, 8, false);
      } else {
        position(nullptr, xr, 4, 4, false);
      }
    }
    x0 = static_cast<int16_t>(x0 + kTickW + kGap);
    // ---- BARS ----
    separator(static_cast<int16_t>(x0 - kGap / 2 - 1));
    label("BARS", x0, kLabY, 5);
    if (info.loop != 0) {
      snprintf(buf, sizeof buf, "%lu", static_cast<unsigned long>((info.loop + kBar - 1) / kBar));
      rtext(buf, static_cast<int16_t>(x0 + kBarsW - 1), 9);
    } else {
      rtext("-", static_cast<int16_t>(x0 + kBarsW - 1), 4);
    }
    x0 = static_cast<int16_t>(x0 + kBarsW + kGroupGap);
    // ---- The selection: the slot, or the note in NOTE_EDIT ----
    struct Cell {
      const char* label;
      char value[8];
      int16_t width;
    } cells[3];
    if (noteEdit) {
      cells[0] = Cell{"NOTE", "-", 18};
      cells[1] = Cell{"VELOCITY", "-", 32};
      cells[2] = Cell{"LENGTH", "-", 24};
      if (edit_.ready && edit_.note) {
        st::noteName(edit_.notePitch, cells[0].value);
        if (ContentEngine::isMuted(edit_.noteVelocity))  // owner 2026-11-08: a muted note
          snprintf(cells[1].value, sizeof cells[1].value, "M");
        else
          snprintf(cells[1].value, sizeof cells[1].value, "%u", edit_.noteVelocity);
        snprintf(cells[2].value, sizeof cells[2].value, "%lu", static_cast<unsigned long>(edit_.noteLength));
      }
      const int16_t end = static_cast<int16_t>(x0 + 18 + 32 + 24 + 2 * kGap - 1);
      oled_.gfx.draw_rect_filled(fb(), static_cast<int16_t>(x0 - 1), kLabY - 1, static_cast<int16_t>(end + 1),
                                 kLabY + 5, 11);
      // The cell F2 / F3 or F4 edits pulses (owner 2026-11-08): LENGTH in length mode, VELOCITY while NOTELEN is held.
      // Its square spans from the divider on its left to the next divider (VELOCITY) or the bar's end (LENGTH), so no
      // column of the steady bar is left beside it; its darkest level keeps the label readable (owner 2026-11-08).
      if (edit_.ready && (edit_.length || edit_.velocity)) {
        const int16_t cx = static_cast<int16_t>(edit_.velocity ? x0 + 18 + kGap : x0 + 18 + 32 + 2 * kGap);
        const int16_t right = static_cast<int16_t>(edit_.velocity ? cx + 32 + kGap / 2 - 1 : end + 1);
        oled_.gfx.draw_rect_filled(fb(), static_cast<int16_t>(cx - kGap / 2), kLabY - 1, right, kLabY + 5,
                                   pulseAt(nowMs));
      }
    } else if (loopEdit) {  // LOOP_EDIT (owner 2026-11-08): the shown loop's start, length (bars) and the tempo
      cells[0] = Cell{"START", "-", 24};
      cells[1] = Cell{"LENGTH", "-", 32};
      cells[2] = Cell{"BPM", "", 24};
      if (info.loop != 0) {
        snprintf(cells[0].value, sizeof cells[0].value, "%lu.%lu",
                 static_cast<unsigned long>(edit_.loopStart / kBar + 1),
                 static_cast<unsigned long>(edit_.loopStart % kBar / (kBar / 16) + 1));
        lengthText(edit_.loopTicks != 0 ? edit_.loopTicks : info.loop, cells[1].value, sizeof cells[1].value);
      }
      // The tempo with its tenth (owner 2026-11-08: F3 sets tenths), drawn after the cells with the thin dot.
      oled_.gfx.draw_rect_filled(fb(), static_cast<int16_t>(x0 - 1), kLabY - 1,
                                 static_cast<int16_t>(x0 + 24 + 32 + 24 + 2 * kGap), kLabY + 5, 11);
    } else {
      cells[0] = Cell{"TRACK", "", 20};
      cells[1] = Cell{"SLOT", "-", 16};
      cells[2] = Cell{st::portLabel(e.output().ports(sel)), "", 16};
      snprintf(cells[0].value, sizeof cells[0].value, "%u", sel + 1);
      if (info.slot != kNone) snprintf(cells[1].value, sizeof cells[1].value, "%u", info.slot % 8 + 1);
      const uint8_t ch = e.output().channel(sel);
      if (ch == 0)
        strcpy(cells[2].value, "-");
      else
        snprintf(cells[2].value, sizeof cells[2].value, "%u", ch);
    }
    for (uint8_t i = 0; i < 3; ++i) {
      if (i) separator(static_cast<int16_t>(x0 - kGap / 2 - 1));
      label(cells[i].label, x0, kLabY, noteEdit || loopEdit ? 0 : 5);
      if (loopEdit && i == 2)
        rtenths(st::tempoTenths(clock.usPerTick(), kQuarter), static_cast<int16_t>(x0 + cells[i].width - 1), 13);
      else
        rtext(cells[i].value, static_cast<int16_t>(x0 + cells[i].width - 1), noteEdit || loopEdit || i < 2 ? 13 : 11);
      x0 = static_cast<int16_t>(x0 + cells[i].width + kGap);
    }
    // ---- Undo: the slot's steps, or the open overdub's / NOTE_EDIT session's ----
    const int16_t bx = static_cast<int16_t>(kX1 - 24);
    const int16_t ux = static_cast<int16_t>(bx - 16 - kGap);
    auto& h = e.history();
    const char* undoLabel = "UNDO";
    uint8_t steps = info.slot != kNone ? h.undoDepth(info.slot) : 0;
    if (info.slot != kNone && h.sessionSlot(ContentEngine::SessionKind::Overdub) == info.slot) {
      undoLabel = "ODUB";
      steps = h.sessionDepth(ContentEngine::SessionKind::Overdub);
    } else if (info.slot != kNone && h.sessionSlot(ContentEngine::SessionKind::Edit) == info.slot) {
      undoLabel = "EDIT";
      steps = edit_.steps;  // History holds the session as one state: the session counts its steps
    }
    const bool session = undoLabel[0] != 'U';
    label(undoLabel, ux, kLabY, session ? 12 : 5);
    if (steps != 0) {
      snprintf(buf, sizeof buf, "%u", steps);
      rtext(buf, static_cast<int16_t>(ux + 15), session ? 12 : 9);
    } else {
      rtext("-", static_cast<int16_t>(ux + 15), 4);
    }
    // ---- The tempo under the clock icon ----
    separator(static_cast<int16_t>(bx - kGap / 2 - 1));
    const bool ext = clock.external();
    if (ext) {
      icon(Icon::In, bx, kLabY, 12);
      icon(Icon::Clock, static_cast<int16_t>(bx + 6), kLabY, 12);
    } else {
      icon(Icon::Clock, bx, kLabY, 5);
    }
    rtenths(st::tempoTenths(clock.usPerTick(), kQuarter), kX1, ext ? 12 : 9);  // the legacy thin dot before the tenth
  }

  SSD1322 oled_;
  EditView edit_;
  const ContentEngine::ProductionBrowser* browser_ = nullptr;
  ContentEngine::EngineRoot* engine_ = nullptr;  // the browser's live mosaic reads the working set
  BrowserInfo browserInfo_{};
  char toast_[40] = {};
  uint32_t toastUntil_ = 0;
  ContentEngine::status::SaveIndicator dots_;
  uint8_t shown_[ContentEngine::kTrackCount] = {};
  uint8_t focus_ = 0xFF;
  bool started_ = false;
  uint32_t lastFrameMs_ = 0, frames_ = 0, worstUs_ = 0;
  uint32_t frameMs_ = kFrameMs;
  uint8_t clock_ = 0x91, phase_ = 0xE2;  // the library's boot values
};

}  // namespace EngineFirmware

#endif  // ENGINE_FIRMWARE
