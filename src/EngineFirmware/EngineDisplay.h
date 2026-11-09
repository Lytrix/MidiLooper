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
    if (browser_ != nullptr && browser_->isOpen()) {  // a window over the performance screen
      drawBrowser(*browser_, e);
      return;
    }
    if (edit_.popup) drawPopup();
    if (static_cast<int32_t>(toastUntil_ - nowMs) > 0) drawToast();
  }

  // ---- The sets browser (owner 2026-11-09: a window over the performance screen, as the length popup; time down the
  // list — year and month sticky on the top row, the day once, the time — on a spine gapped between months; saving and
  // loading split; the save popup as the history with every stage beside it) ----------------------------------------
  using Browser = ContentEngine::ProductionBrowser;
  using Row = Browser::Row;
  using RowKind = Browser::RowKind;
  using Record = ContentEngine::library::Record;
  // The window, the list's columns (year, month, day, the spine, id / name, time / right note) and the detail. Owner
  // 2026-11-09: the day reads with the month (`NOV 9`, the gap of `2026 NOV`), the window narrower and centred.
  static constexpr int16_t kWX0 = 27, kWY0 = 1, kWX1 = 228, kWY1 = 56, kDiv = 137;
  static constexpr int16_t kYearX = 31, kMonX = 49, kDayXR = 71, kSpineX = 77, kIdX = 83, kNoteXR = 133;
  static constexpr int16_t kDetailX = 142, kMosaicX = 192, kMosaicY = 14, kRow0 = 13, kPitch = 9, kHintY = 59;
  static constexpr uint8_t kRuleLevel = 4, kDimText = 7;
  static void idText(const Record& r, char* out, size_t n) {
    snprintf(out, n, "%c%02u", ContentEngine::library::stageLetter(r.stage), r.stageNumber);
  }
  static int16_t rowY(const Browser& b, uint16_t i) { return static_cast<int16_t>(kRow0 + (i - b.first()) * kPitch); }
  /// A GEM window: a dim drop shadow, black, a frame, a hatched title bar with the title in its own black box.
  void window(int16_t x0, int16_t y0, int16_t x1, int16_t y1, const char* title) {
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
    const int16_t tw = static_cast<int16_t>(strlen(title)) * 4 + 6, tx = static_cast<int16_t>((x0 + x1 - tw) / 2);
    oled_.gfx.draw_rect_filled(fb(), tx, static_cast<int16_t>(y0 + 1), static_cast<int16_t>(tx + tw), y0 + 8, 0);
    label(title, static_cast<int16_t>(tx + 3), static_cast<int16_t>(y0 + 2), 13);
  }
  void hint(const char* s) {
    oled_.gfx.draw_rect_filled(fb(), 0, 57, 255, 63, 0);
    label(s, 2, kHintY, 9);
  }
  /// The record a row stands for (its date on the spine), or kNone.
  static uint16_t recordOf(const Row& r) {
    return r.kind == RowKind::Milestone || r.kind == RowKind::Set || r.kind == RowKind::Working ? r.record : Browser::kNone;
  }
  ContentEngine::wallclock::Local when(const Record& m) const {
    return ContentEngine::wallclock::local(m.wallTime, browserInfo_.tz);
  }
  void drawBrowser(const Browser& b, ContentEngine::EngineRoot& e) {
    engine_ = &e;
    if (b.view() == Browser::View::Popup) {
      drawSavePopup(b);
      return;
    }
    const bool inSet = b.view() == Browser::View::Set ||
                       (b.view() == Browser::View::Failed && b.listView() == Browser::View::Set);
    char title[16];
    if (inSet) snprintf(title, sizeof title, "SET %02u", b.openSetNumber());
    else snprintf(title, sizeof title, "SETS");
    window(kWX0, kWY0, kWX1, kWY1, title);
    for (int16_t y = kWY0 + 10; y < kWY1; ++y) px(kDiv, y, kRuleLevel);
    drawSpine(b);
    const uint16_t n = b.rowCount();
    for (uint16_t i = b.first(); i < n && i < b.first() + Browser::kVisible; ++i) drawRow(b, i);
    char h[48];
    if (b.view() == Browser::View::Failed) {
      label(b.failWhat(), kDetailX, 14, 15);
      label(b.failWhy(), kDetailX, 24, 9);
      label(b.failChanged() ? "SET NOT AS IT WAS" : "NOTHING CHANGED", kDetailX, 32, 9);
      snprintf(h, sizeof h, "TRACK: TRY AGAIN  HOLD: CLOSE");
    } else {
      drawDetail(b, b.row(b.selected()), h, sizeof h);
    }
    hint(h);
  }
  /// The spine: a line down the dated rows (sets, stages), broken between months.
  void drawSpine(const Browser& b) {
    const uint16_t n = b.rowCount(), last = static_cast<uint16_t>(b.first() + Browser::kVisible);
    int16_t top = -1;
    ContentEngine::wallclock::Local prev{};
    bool havePrev = false;
    for (uint16_t i = b.first(); i < n && i < last; ++i) {
      const uint16_t rec = recordOf(b.row(i));
      const int16_t y = rowY(b, i);
      if (rec == Browser::kNone || !ContentEngine::wallclock::valid(b.record(rec).wallTime)) continue;
      const auto l = when(b.record(rec));
      if (top < 0) {
        top = i == b.first() && i > 0 && recordOf(b.row(static_cast<uint16_t>(i - 1))) != Browser::kNone
                  ? static_cast<int16_t>(kWY0 + 10)
                  : static_cast<int16_t>(y + 2);
      } else if (havePrev && (l.month != prev.month || l.year != prev.year)) {  // a month ends: a gap
        for (int16_t yy = top; yy <= y - 6; ++yy) px(kSpineX, yy, 6);
        top = static_cast<int16_t>(y - 2);
      }
      prev = l;
      havePrev = true;
    }
    if (top >= 0) {
      const int16_t bottom = n > last ? static_cast<int16_t>(kWY1 - 1) : static_cast<int16_t>(rowY(b, n - 1) + 2);
      for (int16_t yy = top; yy <= bottom; ++yy) px(kSpineX, yy, 6);
    }
  }
  void drawRow(const Browser& b, uint16_t i) {
    const Row r = b.row(i);
    const int16_t y = rowY(b, i);
    const bool on = i == b.selected();
    if (r.kind == RowKind::Divider) {  // LOAD: what the list below is for
      label("LOAD", kYearX, y, 7);  // a heading, dimmer than any row (owner 2026-11-09)
      hline(kYearX + 19, kDiv - 4, static_cast<int16_t>(y + 2), 6);
      return;
    }
    if (on) oled_.gfx.draw_rect_filled(fb(), kWX0 + 1, static_cast<int16_t>(y - 2), kDiv - 1, static_cast<int16_t>(y + 6), 5);
    const uint8_t v = on ? 15 : 10;
    switch (r.kind) {
      case RowKind::Save: label("SAVE", kYearX, y, on ? 15 : 11); return;
      case RowKind::Back: label("< SETS", kYearX, y, on ? 15 : 11); return;
      default: break;
    }
    const uint16_t rec = recordOf(r);
    const bool dated = rec != Browser::kNone && ContentEngine::wallclock::valid(b.record(rec).wallTime);
    if (dated) {
      const auto l = when(b.record(rec));
      // Year and month on the top row (sticky) and where a month starts; the day once per day.
      bool newMonth = true, newDay = true;
      for (uint16_t j = i; j > b.first();) {  // the nearest dated row above it on screen (one without a time is passed)
        const uint16_t above = recordOf(b.row(--j));
        if (above == Browser::kNone) break;
        if (!ContentEngine::wallclock::valid(b.record(above).wallTime)) continue;
        const auto a = when(b.record(above));
        newMonth = a.month != l.month || a.year != l.year;
        newDay = newMonth || a.day != l.day;
        break;
      }
      char buf[8];
      if (newMonth) {
        snprintf(buf, sizeof buf, "%u", l.year);
        label(buf, kYearX, y, on ? 15 : 8);
        label(ContentEngine::wallclock::monthName(l.month), kMonX, y, on ? 15 : 12);
      }
      if (newDay) {
        snprintf(buf, sizeof buf, "%u", l.day);
        rlabel(buf, kDayXR, y, on ? 15 : 9);  // right-aligned: 9 over 10 (owner 2026-11-09)
      }
    }
    // The node: the working set a ring; a stage off the line hollow; the selected one larger and bright.
    const int16_t cy = static_cast<int16_t>(y + 2);
    const bool hollow = r.kind == RowKind::Working || (r.kind == RowKind::Milestone && b.hasSide() && !b.onLine(i));
    if (on) oled_.gfx.draw_rect_filled(fb(), kSpineX - 2, cy - 2, kSpineX + 2, cy + 2, 0);
    if (hollow) {
      for (int16_t d = -2; d <= 2; ++d) {
        px(static_cast<int16_t>(kSpineX + d), static_cast<int16_t>(cy - 2), on ? 15 : 12);
        px(static_cast<int16_t>(kSpineX + d), static_cast<int16_t>(cy + 2), on ? 15 : 12);
        px(static_cast<int16_t>(kSpineX - 2), static_cast<int16_t>(cy + d), on ? 15 : 12);
        px(static_cast<int16_t>(kSpineX + 2), static_cast<int16_t>(cy + d), on ? 15 : 12);
      }
    } else if (on) {
      oled_.gfx.draw_rect_filled(fb(), kSpineX - 2, cy - 2, kSpineX + 2, cy + 2, 15);
    } else {
      oled_.gfx.draw_rect_filled(fb(), kSpineX - 1, cy - 1, kSpineX + 1, cy + 1, 10);
    }
    char text[16], note[12] = {};
    if (r.kind == RowKind::Milestone) idText(b.record(rec), text, sizeof text);
    else snprintf(text, sizeof text, "SET %02u", r.set);
    if (rec != Browser::kNone) {  // the time in one column for sets (their newest stage) and stages (owner 2026-11-09)
      ContentEngine::wallclock::formatClock(b.record(rec).wallTime, browserInfo_.tz, note, sizeof note);
      if (strcmp(note, "--:--") == 0) note[0] = 0;
    }
    label(text, kIdX, y, v);
    if (note[0] != 0) rlabel(note, kNoteXR, y, on ? 15 : kDimText);
  }
  /// A milestone: its id three times the size, the stage and a second line under it; the mosaic beside.
  void drawMilestone(const Record& m, const char* line2, const char* line3) {
    char id[8];
    idText(m, id, sizeof id);
    label(id, kDetailX, kMosaicY, 15, 3);
    label(ContentEngine::library::stageName(m.stage), kDetailX, 34, 13);
    if (line2 != nullptr) label(line2, kDetailX, 42, 10);
    if (line3 != nullptr) label(line3, kDetailX, 50, kDimText);
    drawMosaic(m);
  }
  void drawDetail(const Browser& b, const Row& r, char* h, size_t n) {
    char a[32], c[32];
    switch (r.kind) {
      case RowKind::Save: {
        label("SAVE", kDetailX, kMosaicY, 15, 2);
        label("CURRENT", kDetailX, 30, 11);
        const uint16_t base = b.find(b.baseSequence());
        if (base != Browser::kNone) {
          char id[8];
          idText(b.record(base), id, sizeof id);
          snprintf(c, sizeof c, "FROM %s", id);
        } else {
          snprintf(c, sizeof c, "NEW WORK");
        }
        label(c, kDetailX, 38, 9);
        label(browserInfo_.saved, kDetailX, 46, kDimText);
        drawLiveMosaic();
        snprintf(h, n, "TRACK: SAVE  HOLD: CLOSE");
        return;
      }
      case RowKind::Back:
        label("SETS", kDetailX, kMosaicY, 15, 2);
        label("BACK TO ALL SETS", kDetailX, 30, 9);
        snprintf(h, n, "TRACK: SETS  HOLD: CLOSE");
        return;
      case RowKind::Divider:
        h[0] = 0;
        return;
      case RowKind::Working:
      case RowKind::Set:
        snprintf(h, n, "TRACK: OPEN SET %02u  HOLD: CLOSE", r.set);
        if (r.record == Browser::kNone) {
          snprintf(a, sizeof a, "SET %02u", r.set);
          label(a, kDetailX, kMosaicY, 15, 2);
          label("NO STAGES YET", kDetailX, 30, kDimText);
          drawLiveMosaic();
          return;
        }
        snprintf(c, sizeof c, "%u STAGES", b.milestonesIn(r.set));
        drawMilestone(b.record(r.record), r.kind == RowKind::Working ? "LATEST  NOW" : "LATEST", c);
        return;
      case RowKind::Milestone: {
        const auto& m = b.record(r.record);
        const uint16_t parent = m.parent != 0 ? b.find(m.parent) : Browser::kNone;
        char id[8];
        if (parent != Browser::kNone) {
          idText(b.record(parent), id, sizeof id);
          snprintf(a, sizeof a, "FROM %s", id);
        }
        drawMilestone(m, parent != Browser::kNone ? a : nullptr, nullptr);
        idText(m, id, sizeof id);
        snprintf(h, n, "TRACK: LOAD %s  HOLD: CLOSE", id);
        return;
      }
    }
  }
  /// The 8 × 8 slots (rows tracks, columns slots), brighter for more content: a milestone's, from its record.
  void drawMosaic(const Record& m) {
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
        const int16_t x = static_cast<int16_t>(kMosaicX + s * 4), y = static_cast<int16_t>(kMosaicY + t * 4);
        if (l == 0) px(static_cast<int16_t>(x + 1), static_cast<int16_t>(y + 1), 3);
        else oled_.gfx.draw_rect_filled(fb(), x, y, static_cast<int16_t>(x + 2), static_cast<int16_t>(y + 2), kLevel[l & 3]);
      }
  }
  /// SAVE CURRENT (owner 2026-11-09): left, AS NEW SET on a lane of its own, then the new node on the working set's
  /// history (newest first, the base's line on the main lane); right, every stage with the id it would get, the chosen
  /// one bright and wired to the new node, `<` on the suggested one.
  void drawSavePopup(const Browser& b) {
    window(kWX0, kWY0, kWX1, kWY1, "SAVE CURRENT");
    const uint8_t sel = b.popupSelected();
    constexpr int16_t kMain = kWX0 + 30, kSide = kWX0 + 20, kLabel = kWX0 + 40, kNewY = 13, kHeadY = 24;
    constexpr int16_t kNode = kWX0 + 8, kDayX2 = kWX0 + 58, kWireX = kWX0 + 98, kListX = kWX1 - 81, kListXR = kWX1 - 5;
    // AS NEW SET: a hollow node, nothing wired.
    char buf[24];
    if (sel == 0) oled_.gfx.draw_rect_filled(fb(), kWX0 + 2, kNewY - 3, kWX0 + 102, kNewY + 5, 13);
    snprintf(buf, sizeof buf, "AS NEW SET %02u", b.newSetNumber());
    label(buf, kWX0 + 16, kNewY - 1, sel == 0 ? 0 : 9);
    for (int16_t d = -2; d <= 2; ++d) {
      const uint8_t v = sel == 0 ? 0 : 11;
      px(static_cast<int16_t>(kNode + d), kNewY - 1, v);
      px(static_cast<int16_t>(kNode + d), kNewY + 3, v);
      px(kNode - 2, static_cast<int16_t>(kNewY + 1 + d), v);
      px(kNode + 2, static_cast<int16_t>(kNewY + 1 + d), v);
    }
    // The history: up to four rows around the base, newest first.
    const uint16_t count = b.historyCount(), base = b.historyBase();
    const uint16_t from = base != Browser::kNone && base > 1 ? static_cast<uint16_t>(base - 1) : 0;
    int16_t lineTop = -1, lineBottom = -1, sideTop = -1, sideBottom = -1;
    for (uint16_t k = from; k < count && k < from + 4; ++k) {
      const int16_t y = static_cast<int16_t>(kHeadY + 8 + (k - from) * 7);
      const bool line = b.historyOnLine(k);
      const auto& m = b.record(b.historyRecord(k));
      const int16_t x = line ? kMain : kSide;
      if (line) {
        if (lineTop < 0) lineTop = y;
        lineBottom = y;
      } else {
        if (sideTop < 0) sideTop = y;
        sideBottom = y;
      }
      oled_.gfx.draw_rect_filled(fb(), x - 1, y - 1, x + 1, y + 1, k == base ? 13 : 10);
      idText(m, buf, sizeof buf);
      label(buf, kLabel, static_cast<int16_t>(y - 2), k == base ? 12 : 9);
      ContentEngine::wallclock::formatDay(m.wallTime, browserInfo_.tz, buf, sizeof buf);
      if (strcmp(buf, "--") != 0) label(buf, kDayX2, static_cast<int16_t>(y - 2), 6);
    }
    if (lineTop >= 0)
      for (int16_t y = lineTop; y <= (count > from + 4 ? kWY1 - 1 : lineBottom); ++y) px(kMain, y, 7);
    if (sideTop >= 0) {
      for (int16_t y = sideTop; y <= sideBottom + 4 && y < kWY1; ++y) px(kSide, y, 6);
      hline(kSide, kMain, static_cast<int16_t>(sideBottom + 4 < kWY1 ? sideBottom + 4 : kWY1 - 1), 6);
    }
    // The new node and the stages.
    for (uint8_t k = 0; k < Browser::kStages; ++k) {
      const auto stage = static_cast<ContentEngine::library::Stage>(k);
      const int16_t y = static_cast<int16_t>(12 + k * 7);
      const bool on = sel == k + 1;
      if (on) oled_.gfx.draw_rect_filled(fb(), kListX - 3, static_cast<int16_t>(y - 2), kListXR + 2, static_cast<int16_t>(y + 5), 13);
      label(ContentEngine::library::stageName(stage), kListX, y, on ? 0 : 6);
      snprintf(buf, sizeof buf, "%c%02u", ContentEngine::library::stageLetter(stage), b.nextNumber(b.popupSet(), stage));
      rlabel(buf, kListXR - 8, y, on ? 0 : 7);
      if (stage == b.suggested()) label(b.suggestedProvisional() ? "?" : "<", kListXR - 3, y, on ? 0 : 11);
    }
    if (sel >= 1) {
      const auto stage = static_cast<ContentEngine::library::Stage>(sel - 1);
      const int16_t ty = static_cast<int16_t>(12 + (sel - 1) * 7 + 2);
      for (int16_t y = kHeadY; y <= (lineTop >= 0 ? lineTop : kHeadY + 6); ++y) px(kMain, y, 13);
      oled_.gfx.draw_rect_filled(fb(), kMain - 2, kHeadY - 2, kMain + 2, kHeadY + 2, 15);
      snprintf(buf, sizeof buf, "%c%02u", ContentEngine::library::stageLetter(stage), b.nextNumber(b.popupSet(), stage));
      label(buf, kLabel, kHeadY - 2, 15);
      hline(kWX0 + 54, kWireX, kHeadY, 13);
      for (int16_t y = ty < kHeadY ? ty : kHeadY; y <= (ty < kHeadY ? kHeadY : ty); ++y) px(kWireX, y, 13);
      hline(kWireX, kListX - 5, ty, 13);
    } else {
      for (int16_t d = -2; d <= 2; ++d) {  // no stage: the new set's first, S01, dim
        px(static_cast<int16_t>(kMain + d), kHeadY - 2, 6);
        px(static_cast<int16_t>(kMain + d), kHeadY + 2, 6);
        px(kMain - 2, static_cast<int16_t>(kHeadY + d), 6);
        px(kMain + 2, static_cast<int16_t>(kHeadY + d), 6);
      }
      label("S01", kLabel, kHeadY - 2, 9);
    }
    hint(b.suggestedProvisional() ? "REC/EDIT CHOOSE  TRACK SAVE  HOLD CANCEL  ? LIKELY"
                                  : "REC/EDIT CHOOSE  TRACK SAVE  HOLD CANCEL  < SUGGESTED");
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
