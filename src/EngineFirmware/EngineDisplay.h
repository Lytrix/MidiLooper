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
  int8_t encoder = -1;    // the encoder's option (slice E): 0 pitch, 1 move, 2 length, 3 velocity
  uint8_t copy = 0;       // copy mode (DEC-279 amendment 1): 0 none, 1 a slot, 2 a bar, 3 a note
  uint8_t copySlot = 0, copyBar = 0, copyPitch = 0;  // its source: the engine slot, the bar (0–7), the note's pitch
  uint16_t copyLeft = 0;  // the countdown left, of 1000 (the window's bottom line shrinks with it)
  uint8_t copyPaste = 0;  // a bar / note paste (DEC-279 amendment 8): 0 by the press, 1 replace, 2 merge
  uint32_t barScale = 0, barStart = 0, barKnown = 0;  // the progress bar's frame (DEC-282; 0: the plain bar)
  bool loop = false;      // LOOP_EDIT (owner 2026-11-08)
  uint32_t loopTicks = 0;  // LOOP_EDIT's F2 moved, not yet applied: the pending length (ticks)
  uint32_t loopStart = 0;  // LOOP_EDIT's F1: the start, ticks from the content LOOP_EDIT found
  bool popup = false;      // the length popup (owner 2026-11-08)
  uint8_t popupSlot = 0;
  uint32_t popupLoop = 0;  // the length it shows (ticks)
  uint8_t popupChoice = 0xFF;  // its selected choice (DEC-284 amendment 1: 0 BASE, 1 CUSTOM, 2–13 the lengths)
  bool popupCustom = false;    // CUSTOM exists
};

/// What the sets browser shows beside its model (sets browser slice 3): the working set's save state ("SAVED 14:32"),
/// what return brings back ("TO SET 02 AS LEFT 14:57"), the time zone for dates.
struct BrowserInfo {
  char saved[16] = {};
  char returnTo[32] = {};
  int16_t tz = 0;
  uint32_t now = 0;  // the wall time (0: not set): the timeline's right end
  bool pickSquares = false;  // DEC-272: the picker's empty slots as dim squares, else dots (owner: compare on the device)
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
  /// The firmware's version on the title (owner 2026-11-10: legacy showed v0.6; the engine is v0.7).
  static constexpr const char* kVersion = "v0.7";
  /// The title (owner 2026-11-10, as legacy `drawBootScreen`; the raster-bar title flickered on the panel — too much
  /// of it lit — so a light raster: thin 1-px lines every 4 rows at grey 6–11, symmetric about it, the letters kept clear of them by a
  /// black pixel): `OSTINATIX` in the 5 × 7 font four times its size,
  /// centred, the version at the bottom right — from the display's start until the boot load has ended (the first
  /// performance frame then opens on the saved selection, DEC-273).
  void drawTitle() {
    uint8_t* f = fb();
    oled_.gfx.fill_buffer(f, 0);
    static const char kTitle[] = "OSTINATIX";
    constexpr int16_t kScale = 4, kAdvance = 6;
    const int16_t w = static_cast<int16_t>((sizeof kTitle - 1) * kAdvance * kScale - kScale);
    int16_t x = static_cast<int16_t>((DISPLAY_WIDTH - w) / 2);
    const int16_t top = static_cast<int16_t>((DISPLAY_HEIGHT - 7 * kScale) / 2);
    // 12 lines (rows 10–54), symmetric about the title's centre (row 32): grey 11 for the two either side of it, down
    // to 6 at the edges (owner: 1–4 not visible; a repeating 4–9 left the lower half too dark; centred on row 28 the
    // gradient sat a line high).
    const int16_t centre = static_cast<int16_t>(top + 7 * kScale / 2);
    for (int16_t y = 10; y <= 54; y = static_cast<int16_t>(y + 4)) {
      const int16_t d = static_cast<int16_t>(y > centre ? y - centre : centre - y);
      hline(0, DISPLAY_WIDTH - 1, y, static_cast<uint8_t>(11 - d / 4));
    }
    const GFXfont& font = Font5x7FixedMono;
    const int16_t xStart = x;
    for (uint8_t pass = 0; pass < 2; ++pass, x = xStart)  // a black edge around each letter, then the letters
    for (const char* c = kTitle; *c != 0; ++c, x = static_cast<int16_t>(x + kAdvance * kScale)) {
      const uint8_t code = static_cast<uint8_t>(*c);
      if (code < font.first || code > font.last) continue;
      const GFXglyph& g = font.glyph[code - font.first];
      uint16_t bit = 0;
      uint8_t bits = 0;
      for (uint8_t yy = 0; yy < g.height; ++yy)
        for (uint8_t xx = 0; xx < g.width; ++xx, ++bit) {
          if ((bit & 7) == 0) bits = font.bitmap[g.bitmapOffset + (bit >> 3)];
          if (bits & 0x80) {
            const int16_t px0 = static_cast<int16_t>(x + (g.xOffset + xx) * kScale);
            const int16_t py0 = static_cast<int16_t>(top + (7 + g.yOffset + yy) * kScale);
            if (pass == 0)
              oled_.gfx.draw_rect_filled(f, static_cast<int16_t>(px0 - 1), static_cast<int16_t>(py0 - 1),
                                         static_cast<int16_t>(px0 + kScale), static_cast<int16_t>(py0 + kScale), 0);
            else
              oled_.gfx.draw_rect_filled(f, px0, py0, static_cast<int16_t>(px0 + kScale - 1),
                                         static_cast<int16_t>(py0 + kScale - 1), 15);
          }
          bits = static_cast<uint8_t>(bits << 1);
        }
    }
    text(kVersion, static_cast<int16_t>(DISPLAY_WIDTH - static_cast<int16_t>(strlen(kVersion)) * 6 - 2),
         DISPLAY_HEIGHT, 8);
    sendBlocking();
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

  /// Any SSD1322 register (diagnostics, owner 2026-11-09: a long bar dims toward its right end): `cmd` and up to two
  /// data bytes, sent once the frame in flight is done. Not kept over a reboot.
  void poke(uint8_t cmd, const uint8_t* data, uint8_t n) {
    while (oled_.api.isDisplayTransferActive()) oled_.api.pollDisplayTransfer();
    oled_.api.SSD1322_API_command(cmd);
    for (uint8_t i = 0; i < n; ++i) oled_.api.SSD1322_API_data(data[i]);
    Serial.printf("#ENGINE,oled,reg=0x%02X,n=%u,d0=0x%02X,d1=0x%02X\n", cmd, n, n > 0 ? data[0] : 0, n > 1 ? data[1] : 0);
  }

  /// The sets browser to draw while it is open (slice 3), and what it shows beside its model.
  void setBrowser(const ContentEngine::ProductionBrowser* b) { browser_ = b; }
  void setBrowserInfo(const BrowserInfo& info) { browserInfo_ = info; }
  /// One line over the performance screen for `ms` (rule 6: "SAVED  SET 02  T04").
  void toast(const char* s, uint32_t nowMs, uint32_t ms = 2500) {
    snprintf(toast_, sizeof toast_, "%s", s);
    toastUntil_ = nowMs + ms;
  }

  /// The slot a track's piano roll shows (`Surface::shown`): a queued switch's slot (`queued`: its playhead parked at
  /// the launch point, flashing, until the switch — legacy), else the playing one, else the selected one.
  /// `queuedPos`: where the queued switch starts (a step or bar button's restart, slice C; 0: the loop start).
  void show(uint8_t track, uint8_t slot, bool queued = false, uint32_t queuedPos = 0) {
    if (track >= ContentEngine::kTrackCount) return;
    shown_[track] = slot;
    queued_[track] = queued;
    queuedPos_[track] = queuedPos;
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
  /// A pixel's level (the 4-bit frame buffer: two a byte, an even x in the high nibble).
  uint8_t pixel(int16_t x, int16_t y) {
    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) return 0;
    const uint8_t byte = fb()[y * (DISPLAY_WIDTH / 2) + x / 2];
    return static_cast<uint8_t>((x & 1) == 0 ? byte >> 4 : byte & 0x0F);
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
        {'Y', 0x5A92}, {'Z', 0x72A7}, {':', 0x0410}, {'.', 0x0002}, {'!', 0x2482}, {'<', 0x1511}, {'>', 0x4454},
        {'?', 0x6282}, {'(', 0x2922}, {')', 0x224A}, {'[', 0x6926}, {']', 0x324B},
        {'*', 0x0AA8}, {'{', 0x2E92}, {'}', 0x24BA}};  // ? ( ) the trash popups; [ ] * (×) { (↑) } (↓) the buttons
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
    // The shown slot: the selected track's queued, playing or selected one (`show`), the writing one above all.
    uint8_t slot = shown_[sel];
    const bool queued = queued_[sel] && x.writingTrack() != sel;
    if (x.writingTrack() == sel) slot = x.writingSlot();
    drawMs_ = nowMs;
    SlotInfo info;
    if (slot < ContentEngine::Config::kSlotCount) {
      if (slot != focus_) {  // span tiles for the shown slot (swept first)
        e.setDisplayFocus(slot, slot);
        focus_ = slot;
      }
      info = drawSlot(e, sel, slot, lastTick, queued);
    }
    drawStatus(e, clock, sel, info, lastTick, nowMs);
    if (browser_ != nullptr && browser_->isOpen()) {  // a window over the performance screen
      drawBrowser(*browser_, e);
      if (static_cast<int32_t>(toastUntil_ - nowMs) > 0) drawToast();  // a deletion's line: the browser stays open
      return;
    }
    if (edit_.popup) drawPopup();
    if (edit_.copy != 0) drawCopy();
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
  /// The bottom row starts with RECORD and EDIT's moves (owner 2026-11-09: `[REC]↑ [EDT]↓`).
  static constexpr const char* kMoves = "[REC]{ [EDT]}  ";
  /// Hint text: the buttons (`[TRK]`, brackets too) dimmer than the actions beside them (owner 2026-11-09).
  static constexpr uint8_t kHintV = 9, kButtonV = kDimText;  // the unselected menu text
  int16_t hintLabel(const char* s, int16_t x, int16_t top) {
    bool button = false;
    for (; *s; ++s) {
      if (*s == '[') button = true;
      const char c[2] = {*s, 0};
      x = label(c, x, top, button ? kButtonV : kHintV);
      if (*s == ']') button = false;
    }
    return x;
  }
  void hint(const char* s) {
    oled_.gfx.draw_rect_filled(fb(), 0, 57, 255, 63, 0);
    hintLabel(s, 2, kHintY);
  }
  /// The bottom right (DEC-279 amendment 2: a long press exits every window): `HOLD EXIT`, or right after a move to the
  /// trash `3× [TRK] UNDO` beside a can, the can as bright as the words.
  void drawTrashCorner(bool undo) {
    const uint8_t v = kHintV;
    const int16_t y = kHintY, x = 249;  // the can's left column
    if (!undo) {
      hintLabel("HOLD EXIT", static_cast<int16_t>(255 - 9 * 4), y);
      return;
    }
    const char* what = "3* [TRK] UNDO";
    hintLabel(what, static_cast<int16_t>(x - 5 - static_cast<int16_t>(strlen(what)) * 4 + 2), y);
    hline(x - 1, x + 3, y, v);  // the lid
    for (int16_t yy = static_cast<int16_t>(y + 1); yy <= y + 4; ++yy) {  // the can, ribbed
      px(x, yy, v);
      px(static_cast<int16_t>(x + 2), yy, v);
    }
    hline(x, x + 2, static_cast<int16_t>(y + 4), v);
  }
  /// The record a row stands for (its date on the spine), or kNone.
  static uint16_t recordOf(const Row& r) {
    return r.kind == RowKind::Milestone || r.kind == RowKind::Set || r.kind == RowKind::Working ||
                   r.kind == RowKind::Item
               ? r.record
               : Browser::kNone;
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
    if (b.picking() || (b.view() == Browser::View::Failed && b.listView() == Browser::View::Picker)) {
      drawPicker(b, e);
      return;
    }
    const bool confirming = b.view() == Browser::View::Confirm;
    char title[24];
    if (b.shownView() == Browser::View::Set) snprintf(title, sizeof title, "SET %u", b.openSetNumber());
    else if (b.shownView() == Browser::View::Stages) snprintf(title, sizeof title, "GET A LOOP  SET %u", b.openSetNumber());
    else if (b.shownView() == Browser::View::Bin) snprintf(title, sizeof title, "TRASH");
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
      snprintf(h, sizeof h, "[TRK] TRY AGAIN   HOLD EXIT");  // REC / EDIT and the trash do nothing here
    } else {
      drawDetail(b, b.row(b.selected()), h, sizeof h);
    }
    if (b.view() == Browser::View::Failed) {
      hint(h);
    } else {  // owner 2026-11-09: one line — the moves, the actions, and the trash on the right
      char line[64];
      snprintf(line, sizeof line, "%s%s", kMoves, h);
      hint(line);
      drawTrashCorner(b.lastTrash() != Browser::kNone);
    }
    if (confirming) drawConfirm(b);
  }
  /// DEC-268: the target exactly (`MOVE SET 01 TO TRASH?` with ALL SAVED STAGES, or `MOVE SET 01 S01 TO TRASH?` with
  /// its stage) and when it was saved — or `EMPTY THE TRASH?`, for good — over the list; YES and CANCEL, the selected
  /// one inverted.
  void drawConfirm(const Browser& b) {
    constexpr int16_t x0 = 58, y0 = 4, x1 = 197, y1 = 56, kButtonY = 46;
    if (b.confirmKind() == Browser::Confirm::EmptyLoop) {  // DEC-272: an empty loop over a filled slot, centred
      window(x0, y0, x1, y1, "EMPTY LOOP");
      char a[32], c[32];
      const uint8_t d = b.pickDestination();
      snprintf(a, sizeof a, "REPLACE %u-%u WITH AN", d / 8 + 1, d % 8 + 1);
      snprintf(c, sizeof c, "EMPTY %u-BAR LOOP?", b.confirmBars());
      clabel(a, (x0 + x1) / 2, static_cast<int16_t>(y0 + 13), 15);
      clabel(c, (x0 + x1) / 2, static_cast<int16_t>(y0 + 21), 15);
      clabel("UNDO BRINGS IT BACK", (x0 + x1) / 2, static_cast<int16_t>(y0 + 30), 7);
      drawButtons(b, x0, x1, kButtonY);
      return;
    }
    const bool empty = b.confirmKind() == Browser::Confirm::Empty;
    window(x0, y0, x1, y1, empty ? "EMPTY TRASH" : "TRASH");
    char q[32], line[32], stamp[24];
    uint16_t rec = Browser::kNone;
    if (empty) {
      uint16_t stages = 0;
      for (uint16_t k = 0; k < b.itemCount(); ++k) stages = static_cast<uint16_t>(stages + b.itemSize(b.item(k)));
      snprintf(q, sizeof q, "EMPTY THE TRASH?");
      snprintf(line, sizeof line, "%u ITEM%s, %u STAGE%s", b.itemCount(), b.itemCount() == 1 ? "" : "S", stages,
               stages == 1 ? "" : "S");
    } else if (b.confirmRecord() != Browser::kNone) {
      rec = b.confirmRecord();
      char id[8];
      idText(b.record(rec), id, sizeof id);
      snprintf(q, sizeof q, "MOVE SET %u %s TO TRASH?", b.confirmSet(), id);
      snprintf(line, sizeof line, "%s", ContentEngine::library::stageName(b.record(rec).stage));
    } else {
      rec = b.latestOf(b.confirmSet());
      snprintf(q, sizeof q, "MOVE SET %u TO TRASH?", b.confirmSet());
      snprintf(line, sizeof line, "ALL SAVED STAGES (%u)", b.milestonesIn(b.confirmSet()));
    }
    label(q, static_cast<int16_t>(x0 + 6), static_cast<int16_t>(y0 + 13), 15);
    label(line, static_cast<int16_t>(x0 + 6), static_cast<int16_t>(y0 + 22), 11);
    if (empty) {
      label("REMOVED FOR GOOD", static_cast<int16_t>(x0 + 6), static_cast<int16_t>(y0 + 30), 9);
    } else if (rec != Browser::kNone) {
      ContentEngine::wallclock::formatStamp(b.record(rec).wallTime, browserInfo_.tz, stamp, sizeof stamp);
      snprintf(line, sizeof line, b.confirmRecord() != Browser::kNone ? "SAVED %s" : "LATEST %s", stamp);
      label(line, static_cast<int16_t>(x0 + 6), static_cast<int16_t>(y0 + 30), 9);
    }
    drawButtons(b, x0, x1, kButtonY);
  }
  /// YES and CANCEL, the selected one inverted, and the confirmation's line.
  void drawButtons(const Browser& b, int16_t x0, int16_t x1, int16_t buttonY) {
    const char* const kButtons[2] = {"YES", "CANCEL"};
    for (uint8_t k = 0; k < 2; ++k) {
      const int16_t bx = static_cast<int16_t>(k == 0 ? x0 + 24 : x1 - 50), bw = 30;
      const bool on = b.confirmSelected() == k;
      if (on) oled_.gfx.draw_rect_filled(fb(), bx, static_cast<int16_t>(buttonY - 2), static_cast<int16_t>(bx + bw),
                                         static_cast<int16_t>(buttonY + 6), 13);
      const int16_t tw = static_cast<int16_t>(strlen(kButtons[k]) * 4 - 1);
      label(kButtons[k], static_cast<int16_t>(bx + (bw - tw) / 2 + 1), buttonY, on ? 0 : 10);
    }
    hint("[REC]{ [EDT]}  [TRK] CONFIRM  HOLD EXIT");
  }
  /// A label centred on `xc`.
  void clabel(const char* s, int16_t xc, int16_t top, uint8_t v) {
    label(s, static_cast<int16_t>(xc - (static_cast<int16_t>(strlen(s)) * 4 - 1) / 2), top, v);
  }

  // ---- Get a loop's picker (DEC-272; layout A of the plan's mockups) --------------------------------------------------
  static constexpr int16_t kPresetX = 33, kPresetW = 13, kPresetH = 7, kPresetPitch = 8, kGridX = 52, kGridY = 13;
  static constexpr int16_t kCell = 4, kPitchX = 6, kPitchY = 5;
  static constexpr int16_t kPanelX0 = 141, kPanelX1 = 225, kPanelY0 = 21, kPanelY1 = 44;
  void drawPicker(const Browser& b, ContentEngine::EngineRoot& e) {
    char title[32], a[32], c[24];
    const uint16_t rec = b.pickRecord();
    if (rec != Browser::kNone) {
      char id[8];
      idText(b.record(rec), id, sizeof id);
      snprintf(title, sizeof title, "LOOPS  SET %u  %s", b.record(rec).set, id);
    } else {
      snprintf(title, sizeof title, "EMPTY LOOPS");
    }
    window(kWX0, kWY0, kWX1, kWY1, title);
    for (int16_t y = kWY0 + 10; y < kWY1; ++y) px(kDiv, y, kRuleLevel);
    const uint8_t preset = b.pickPreset();
    for (uint8_t i = 0; i < Browser::kPresets; ++i) {  // the empty-loop presets: dotted, unlike a loop
      const int16_t x = kPresetX, y = static_cast<int16_t>(kGridY + i * kPresetPitch);
      const bool on = preset == i;
      if (on) {
        oled_.gfx.draw_rect_filled(fb(), x, y, static_cast<int16_t>(x + kPresetW - 1), static_cast<int16_t>(y + kPresetH - 1), 13);
      } else {
        for (int16_t xx = x; xx < x + kPresetW; xx = static_cast<int16_t>(xx + 2)) {
          px(xx, y, 6);
          px(xx, static_cast<int16_t>(y + kPresetH - 1), 6);
        }
        for (int16_t yy = y; yy < y + kPresetH; yy = static_cast<int16_t>(yy + 2)) {
          px(x, yy, 6);
          px(static_cast<int16_t>(x + kPresetW - 1), yy, 6);
        }
      }
      snprintf(a, sizeof a, "%u", Browser::kPresetBars[i]);
      clabel(a, static_cast<int16_t>(x + kPresetW / 2), static_cast<int16_t>(y + 1), on ? 0 : 9);
    }
    static const uint8_t kLevel[4] = {0, 7, 11, 15};
    for (uint8_t t = 0; t < 8; ++t)
      for (uint8_t k = 0; k < 8; ++k) {  // the stage's slots: squares as the mosaic, an empty one a dot (or a dim square)
        const int16_t x = static_cast<int16_t>(kGridX + k * kPitchX), y = static_cast<int16_t>(kGridY + t * kPitchY);
        const uint8_t l = b.pickLevel(static_cast<uint8_t>(t * 8 + k));
        if (l != 0)
          oled_.gfx.draw_rect_filled(fb(), x, y, static_cast<int16_t>(x + kCell - 1), static_cast<int16_t>(y + kCell - 1), kLevel[l]);
        else if (browserInfo_.pickSquares)
          oled_.gfx.draw_rect_filled(fb(), x, y, static_cast<int16_t>(x + kCell - 1), static_cast<int16_t>(y + kCell - 1), 2);
        else
          px(static_cast<int16_t>(x + 2), static_cast<int16_t>(y + 2), 3);
      }
    if (preset == Browser::kNoPreset) {  // the cursor: a hollow bright box
      const int16_t x = static_cast<int16_t>(kGridX + b.pickSlotIndex() * kPitchX), y = static_cast<int16_t>(kGridY + b.pickTrack() * kPitchY);
      oled_.gfx.draw_rect_filled(fb(), x, y, static_cast<int16_t>(x + kCell - 1), static_cast<int16_t>(y + kCell - 1), 0);
      hline(static_cast<int16_t>(x - 1), static_cast<int16_t>(x + kCell), static_cast<int16_t>(y - 1), 15);
      hline(static_cast<int16_t>(x - 1), static_cast<int16_t>(x + kCell), static_cast<int16_t>(y + kCell), 15);
      for (int16_t yy = static_cast<int16_t>(y - 1); yy <= y + kCell; ++yy) {
        px(static_cast<int16_t>(x - 1), yy, 15);
        px(static_cast<int16_t>(x + kCell), yy, 15);
      }
      snprintf(a, sizeof a, "SLOT %u-%u", b.pickTrack() + 1, b.pickSlotIndex() + 1);
    } else {
      snprintf(a, sizeof a, "EMPTY %u", Browser::kPresetBars[preset]);
    }
    rlabel(a, 133, kGridY, 13);
    const uint8_t d = b.pickDestination();
    snprintf(a, sizeof a, "INTO %u-%u", d / 8 + 1, d % 8 + 1);
    label(a, kPanelX0, kGridY, 11);
    // The panel: an empty loop, the loop's roll, or what the read is doing; the audition beneath.
    const auto au = b.audition();
    bool use = au == Browser::Audition::Sounding || au == Browser::Audition::Silent;
    if (preset != Browser::kNoPreset) {
      panelBox();
      clabel("EMPTY LOOP", (kPanelX0 + kPanelX1) / 2, 26, 15);
      snprintf(a, sizeof a, "%u BAR%s", Browser::kPresetBars[preset], Browser::kPresetBars[preset] == 1 ? "" : "S");
      clabel(a, (kPanelX0 + kPanelX1) / 2, 35, 9);
      label("AN EMPTY LOOP", kPanelX0, 49, 11);
      use = true;
    } else if (b.pickLevel(b.pickSource()) == 0) {
      panelBox();
      clabel("NO LOOP HERE", (kPanelX0 + kPanelX1) / 2, 30, 6);
    } else if (b.preview() == Browser::Preview::Ready) {
      drawPreviewRoll(e, b.previewBars(), au == Browser::Audition::Silent);
      snprintf(c, sizeof c, "%u BAR%s", b.previewBars(), b.previewBars() == 1 ? "" : "S");
      if (au == Browser::Audition::Sounding) {
        label("AUDITION", kPanelX0, 49, 15);
        rlabel(c, kPanelX1, 49, 9);
      } else if (au == Browser::Audition::Silent) {
        label("PREVIEW ONLY", kPanelX0, 49, 11);
        rlabel(silentText(b.silentWhy(), a, sizeof a), kPanelX1, 49, 6);
      } else {
        label(c, kPanelX0, 49, 11);
      }
    } else {
      panelBox();
      const auto pv = b.preview();
      clabel(pv == Browser::Preview::Failed   ? "READ FAILED"
             : pv == Browser::Preview::TooBig ? "TOO BIG TO PREVIEW"
                                              : "READING...",
             (kPanelX0 + kPanelX1) / 2, 30, 7);
      label("? BARS", kPanelX0, 49, 6);
    }
    char h[64];
    if (b.view() == Browser::View::Failed) {
      fillRect(kPanelX0 - 1, kPanelY0 - 1, kPanelX1 + 1, 55, 0);
      label(b.failWhat(), kPanelX0, 22, 15);
      label(b.failWhy(), kPanelX0, 30, 9);
      label("NOTHING CHANGED", kPanelX0, 38, 9);
      hint("[TRK] TRY AGAIN   HOLD EXIT");
      return;
    }
    snprintf(h, sizeof h, "[LEN]{ [PLY]} [REC]< [EDT]>  [TRK] %s  HOLD EXIT", use ? "USE" : "AUDITION");
    hint(h);
    if (b.view() == Browser::View::Confirm) drawConfirm(b);
  }
  void panelBox() {
    hline(kPanelX0 - 1, kPanelX1 + 1, kPanelY0 - 1, 4);
    hline(kPanelX0 - 1, kPanelX1 + 1, kPanelY1 + 1, 4);
    for (int16_t y = kPanelY0 - 1; y <= kPanelY1 + 1; ++y) {
      px(kPanelX0 - 1, y, 4);
      px(kPanelX1 + 1, y, 4);
    }
  }
  void fillRect(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t v) { oled_.gfx.draw_rect_filled(fb(), x0, y0, x1, y1, v); }
  /// Why an audition is silent (the owner's code): 1 stopped, 2 the slot not playing, 3 muted, 4 + t: track t soloed.
  static const char* silentText(uint8_t why, char* buf, size_t n) {
    if (why >= 4) {
      snprintf(buf, n, "SOLO ON %u", why - 3);
      return buf;
    }
    return why == 1 ? "STOPPED" : why == 2 ? "NOT PLAYING" : why == 3 ? "MUTED" : "";
  }
  /// The loop under the cursor, whole (owner 2026-11-10: every bar for now), from the preview slot's tiles.
  void drawPreviewRoll(ContentEngine::EngineRoot& e, uint16_t bars, bool dim) {
    panelBox();
    if (bars == 0) return;
    const uint32_t loop = static_cast<uint32_t>(bars) * kBar;
    const uint8_t slot = ContentEngine::EngineRoot::kPreviewSlot;
    uint8_t lo = 60, hi = 61;
    if (!ContentEngine::pitchExtent(e.tiles(), slot, 0, static_cast<uint8_t>(bars - 1), lo, hi)) {
      lo = 60;
      hi = 61;
    }
    if (hi == lo) hi = static_cast<uint8_t>(lo + 1);
    const int32_t w = kPanelX1 - kPanelX0;
    for (uint16_t bar = 1; bar < bars; ++bar) {
      if (bars > 16 && bar % 4 != 0) continue;
      const int16_t gx = static_cast<int16_t>(kPanelX0 + static_cast<int32_t>(bar) * w / bars);
      oled_.gfx.draw_vline(fb(), gx, kPanelY0, kPanelY1, 2);
    }
    ContentEngine::RollView v{slot, loop, kBar, 0, loop, lo, hi, kPanelX0, kPanelX1, kPanelY0, kPanelY1};
    e.drawRollBand(v, fb(), DISPLAY_WIDTH, kPanelY0, kPanelY1);
    if (dim)  // a silent preview: the notes drawn dimmer
      for (int16_t y = kPanelY0; y <= kPanelY1; ++y)
        for (int16_t x = kPanelX0; x <= kPanelX1; ++x) {
          const uint8_t v0 = pixel(x, y);
          if (v0 > 4) px(x, y, static_cast<uint8_t>(v0 / 2));
        }
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
    if (r.kind == RowKind::Divider) {  // what the list below is for: sets, or a set's stages (owner 2026-11-09)
      const Browser::View list = b.shownView();
      const char* what = list == Browser::View::Bin      ? "RESTORE SET/STAGE"
                         : list == Browser::View::Set    ? "LOAD STAGE"
                         : list == Browser::View::Stages ? "FROM STAGE"
                                                         : "LOAD SET";
      label(what, kYearX, y, 7);  // a heading, dimmer than any row
      hline(static_cast<int16_t>(kYearX + strlen(what) * 4 + 3), kDiv - 4, static_cast<int16_t>(y + 2), 6);
      return;
    }
    if (on) oled_.gfx.draw_rect_filled(fb(), kWX0 + 1, static_cast<int16_t>(y - 2), kDiv - 1, static_cast<int16_t>(y + 6), 5);
    const uint8_t v = on ? 15 : 10;
    switch (r.kind) {
      case RowKind::Close: label("< CLOSE", kYearX, y, on ? 15 : 11); return;
      case RowKind::Empty: label("EMPTY TRASH", kYearX, y, on ? 15 : b.itemCount() != 0 ? 11 : kDimText); return;
      case RowKind::Trash: {  // the second way in (owner 2026-11-09), its items on the right as a set's time is
        label("TRASH", kYearX, y, on ? 15 : 11);
        char n[8];
        snprintf(n, sizeof n, "%u", b.itemCount());
        if (b.itemCount() != 0) rlabel(n, kNoteXR, y, on ? 15 : kDimText);
        return;
      }
      case RowKind::Save: label("SAVE SET", kYearX, y, on ? 15 : 11); return;
      case RowKind::Back:
        label(b.shownView() == Browser::View::Stages ? "< BACK" : "< SETS", kYearX, y, on ? 15 : 11);
        return;
      case RowKind::GetLoop: label("GET A LOOP", kYearX, y, on ? 15 : 11); return;
      case RowKind::TrashSet: label("MOVE SET TO TRASH", kYearX, y, on ? 15 : 11); return;
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
    if (r.kind == RowKind::Milestone) {
      idText(b.record(rec), text, sizeof text);
    } else if (r.kind == RowKind::Item && b.record(rec).parent != 0) {  // a stage in the trash: its set and id
      const uint16_t m = b.find(b.record(rec).parent);
      char id[8] = "?";
      if (m != Browser::kNone) idText(b.record(m), id, sizeof id);
      snprintf(text, sizeof text, "SET %u %s", r.set, id);
    } else {
      snprintf(text, sizeof text, "SET %u", r.set);
    }
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
      case RowKind::Close:
        label("CLOSE", kDetailX, kMosaicY, 15, 2);
        label("BACK TO PLAYING", kDetailX, 30, 9);
        snprintf(h, n, "[TRK] CLOSE");
        return;
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
        snprintf(h, n, "[TRK] SAVE");
        return;
      }
      case RowKind::Back:
        if (b.shownView() == Browser::View::Stages) {
          snprintf(a, sizeof a, "SET %u", b.openSetNumber());
          label(a, kDetailX, kMosaicY, 15, 2);
          label("BACK TO THE SET", kDetailX, 30, 9);
          snprintf(h, n, "[TRK] BACK");
          return;
        }
        label("SETS", kDetailX, kMosaicY, 15, 2);
        label("BACK TO ALL SETS", kDetailX, 30, 9);
        snprintf(h, n, "[TRK] SETS");
        return;
      case RowKind::GetLoop: {  // DEC-272: into the selected slot, boxed in the working set's mosaic
        label("GET", kDetailX, kMosaicY, 15, 3);
        label("A LOOP", kDetailX, 32, 11);
        const uint8_t d = b.pickDestination();
        snprintf(a, sizeof a, "INTO %u-%u", d / 8 + 1, d % 8 + 1);
        label(a, kDetailX, 40, 13);
        drawLiveMosaic();
        const int16_t mx = static_cast<int16_t>(kMosaicX + (d % 8) * 4), my = static_cast<int16_t>(kMosaicY + (d / 8) * 4);
        hline(static_cast<int16_t>(mx - 1), static_cast<int16_t>(mx + 3), static_cast<int16_t>(my - 1), 15);
        hline(static_cast<int16_t>(mx - 1), static_cast<int16_t>(mx + 3), static_cast<int16_t>(my + 3), 15);
        for (int16_t yy = static_cast<int16_t>(my - 1); yy <= my + 3; ++yy) {
          px(static_cast<int16_t>(mx - 1), yy, 15);
          px(static_cast<int16_t>(mx + 3), yy, 15);
        }
        snprintf(h, n, "[TRK] OPEN");
        return;
      }
      case RowKind::Divider:
        h[0] = 0;
        return;
      case RowKind::TrashSet:  // DEC-279 amendment 2: destructive, so a double press; a triple brings it back
        snprintf(a, sizeof a, "SET %u", r.set);
        label(a, kDetailX, kMosaicY, 15, 2);
        snprintf(c, sizeof c, "%u STAGE%s", b.milestonesIn(r.set), b.milestonesIn(r.set) == 1 ? "" : "S");
        label(b.milestonesIn(r.set) != 0 ? c : "NO STAGES", kDetailX, 30, 11);
        label("TO THE TRASH", kDetailX, 38, 9);
        snprintf(h, n, b.milestonesIn(r.set) != 0 ? "2* [TRK] MOVE TO TRASH" : "");
        return;
      case RowKind::Trash:
        label("TRASH", kDetailX, kMosaicY, 15, 2);
        snprintf(a, sizeof a, "%u ITEM%s", b.itemCount(), b.itemCount() == 1 ? "" : "S");
        label(b.itemCount() != 0 ? a : "EMPTY", kDetailX, 30, 11);
        snprintf(h, n, "[TRK] OPEN TRASH");
        return;
      case RowKind::Empty: {  // DEC-268
        label("EMPTY", kDetailX, kMosaicY, 15, 2);
        snprintf(a, sizeof a, "%u ITEM%s", b.itemCount(), b.itemCount() == 1 ? "" : "S");
        label(b.itemCount() != 0 ? a : "THE TRASH IS EMPTY", kDetailX, 30, 11);
        if (b.itemCount() != 0) label("REMOVED FOR GOOD", kDetailX, 38, 9);
        if (b.itemCount() != 0) snprintf(h, n, "[TRK] EMPTY TRASH");
        else h[0] = 0;
        return;
      }
      case RowKind::Item: {  // the item's newest stage, its stage count (a set) or set (a stage), when it went to the trash
        const auto& t = b.record(r.record);
        const uint16_t newest = b.itemNewest(r.record);
        char at[8];
        ContentEngine::wallclock::formatClock(t.wallTime, browserInfo_.tz, at, sizeof at);
        // Beside the mosaic: 12 characters at most (kMosaicX − kDetailX); the set is in the row and the hint.
        const uint16_t size = b.itemSize(r.record);
        if (t.parent == 0) snprintf(a, sizeof a, "%u STAGE%s", size, size == 1 ? "" : "S");
        else snprintf(a, sizeof a, "SET %u", r.set);
        snprintf(c, sizeof c, "TRASHED %s", at);
        if (newest != Browser::kNone) drawMilestone(b.record(newest), a, c);
        if (t.parent == 0) {
          snprintf(h, n, "[TRK] RESTORE SET %u", r.set);
        } else {
          char id[8] = "";
          if (newest != Browser::kNone) idText(b.record(newest), id, sizeof id);
          snprintf(h, n, "[TRK] RESTORE STAGE %s", id);
        }
        return;
      }
      case RowKind::Working:
      case RowKind::Set:
        snprintf(h, n, "[TRK] OPEN");
        if (r.record == Browser::kNone) {
          snprintf(h, n, "[TRK] OPEN");
          snprintf(a, sizeof a, "SET %u", r.set);
          label(a, kDetailX, kMosaicY, 15, 2);
          label("NO STAGES", kDetailX, 30, kDimText);  // beside the mosaic
          drawLiveMosaic();
          return;
        }
        snprintf(c, sizeof c, "%u STAGE%s", b.milestonesIn(r.set), b.milestonesIn(r.set) == 1 ? "" : "S");
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
        if (b.shownView() == Browser::View::Stages) snprintf(h, n, "[TRK] SELECT STAGE");
        else snprintf(h, n, "[TRK] LOAD  2* [TRK] TRASH");
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
    snprintf(buf, sizeof buf, "AS NEW SET %u", b.newSetNumber());
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
    hint("[REC]{ [EDT]}  [TRK] SAVE  HOLD EXIT");  // the suggestion: `<` (`?` likely) in the list
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
  /// DEC-284 amendment 1 (owner 2026-11-10: "show all possible lengths in the popup to cycle through"): every choice in
  /// three rows — BASE · CUSTOM, 1–8, 16 · 32 · 64 · 128 — BARS in the title once, the selected one inverted, the
  /// others dim (CUSTOM dimmer while there is none).
  void drawPopup() {
    constexpr int16_t x0 = 72, y0 = 1, x1 = 183, y1 = 45, sh = 3;
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
    snprintf(title, sizeof title, "LOOP BARS T%u S%u", edit_.popupSlot / 8 + 1, edit_.popupSlot % 8 + 1);
    const int16_t tw = static_cast<int16_t>(strlen(title)) * 4 + 3, tx = static_cast<int16_t>((x0 + x1 - tw) / 2);
    oled_.gfx.draw_rect_filled(fb(), tx, static_cast<int16_t>(y0 + 1), static_cast<int16_t>(tx + tw), y0 + 8, 0);
    label(title, static_cast<int16_t>(tx + 2), static_cast<int16_t>(y0 + 2), 13);
    static const char* const kChoice[14] = {"BASE", "CUSTOM", "1", "2", "3", "4", "5", "6", "7", "8", "16", "32", "64", "128"};
    static constexpr uint8_t kRow[4] = {0, 2, 10, 14};     // the rows' first choices
    static constexpr int16_t kGap[3] = {12, 8, 10};        // the space between a row's choices
    for (uint8_t r = 0; r < 3; ++r) {
      int16_t w = static_cast<int16_t>(-kGap[r]);
      for (uint8_t i = kRow[r]; i < kRow[r + 1]; ++i) w = static_cast<int16_t>(w + strlen(kChoice[i]) * 4 - 1 + kGap[r]);
      int16_t x = static_cast<int16_t>((x0 + x1 - w) / 2);
      const int16_t y = static_cast<int16_t>(y0 + 13 + r * 8);
      for (uint8_t i = kRow[r]; i < kRow[r + 1]; ++i) {
        const int16_t iw = static_cast<int16_t>(strlen(kChoice[i]) * 4 - 1);
        if (i == edit_.popupChoice) {
          oled_.gfx.draw_rect_filled(fb(), static_cast<int16_t>(x - 2), static_cast<int16_t>(y - 1),
                                     static_cast<int16_t>(x + iw + 1), static_cast<int16_t>(y + 5), 13);
          label(kChoice[i], x, y, 0);
        } else {
          label(kChoice[i], x, y, i == 1 && !edit_.popupCustom ? 2 : 6);
        }
        x = static_cast<int16_t>(x + iw + kGap[r]);
      }
    }
    static const char kHint[] = "PRESS +  2X -  HOLD CANCEL";  // next length, the one before, back as opened
    label(kHint, static_cast<int16_t>((x0 + x1 - static_cast<int16_t>(sizeof kHint - 1) * 4) / 2),
          static_cast<int16_t>(y1 - 7), 5);
  }

  /// The progress bar over the stored loop (DEC-282, owner 2026-11-10: mockup B; amendment 1): the whole bar is the
  /// stored loop (DEC-283's frame) or the stored notes' reach, gapped every 1 / 4 / 8 / 16 / 32 bars (the finest that fits); brackets
  /// around the part looping now; the playhead fills it while playing; the loop past the stored notes dotted.
  void drawSection(int16_t x0, int16_t x1, const SlotInfo& info) {
    const uint32_t scale = edit_.barScale, start = edit_.barStart % scale, known = edit_.barKnown;
    const int32_t w = x1 - x0;
    const auto at = [&](uint32_t t) { return static_cast<int16_t>(x0 + static_cast<int64_t>(t > scale ? scale : t) * w / scale); };
    const int16_t y = kLabY + 2;
    const uint32_t bars = (scale + kBar - 1) / kBar;
    uint32_t every = 0;  // bars between gaps
    for (uint32_t step : {1u, 4u, 8u, 16u, 32u})
      if (bars > step && static_cast<int32_t>(bars / step) * 3 <= w) {
        every = step;
        break;
      }
    const auto gap = [&](int16_t x) {
      for (uint32_t k = every; every != 0 && k < bars; k += every)
        if (at(k * kBar) == x) return true;
      return false;
    };
    // DEC-283: the loop's place in the stored loop is absolute, so it may wrap past the frame's end to its start.
    const uint32_t loop = info.loop < scale ? info.loop : scale;
    const auto into = [&](int16_t x) {  // ticks from the loop's start to column x's tick
      const uint32_t t = static_cast<uint32_t>(static_cast<int64_t>(x - x0) * scale / (w > 0 ? w : 1));
      return (t + scale - start) % scale;
    };
    const int16_t a = at(start), b = (start + loop) % scale == 0 ? x1 : at((start + loop) % scale), past = at(known);
    for (int16_t x = x0; x <= x1; ++x) {
      const uint32_t d = into(x);
      const bool in = d < loop || loop == scale;
      if (gap(x)) px(x, y, 0);
      else if (in && info.moving && d <= info.pos) px(x, y, 15);
      else if (in && x >= past) px(x, y, (x - past) % 2 == 0 ? 6 : 0);  // the loop past the notes
      else px(x, y, in ? 6 : 3);
    }
    px(a, static_cast<int16_t>(y - 2), 12);  // the brackets: [ at the start, ] at the end
    px(a, static_cast<int16_t>(y - 1), 12);
    px(a, static_cast<int16_t>(y + 1), 12);
    px(a, static_cast<int16_t>(y + 2), 12);
    px(b, static_cast<int16_t>(y - 2), 12);
    px(b, static_cast<int16_t>(y - 1), 12);
    px(b, static_cast<int16_t>(y + 1), 12);
    px(b, static_cast<int16_t>(y + 2), 12);
    px(static_cast<int16_t>(a + 1), static_cast<int16_t>(y - 2), 12);
    px(static_cast<int16_t>(a + 1), static_cast<int16_t>(y + 2), 12);
    px(static_cast<int16_t>(b - 1), static_cast<int16_t>(y - 2), 12);
    px(static_cast<int16_t>(b - 1), static_cast<int16_t>(y + 2), 12);
  }

  /// Copy mode (DEC-279 amendment 1, owner 2026-11-10: its own minimal state): a small window over the roll's top —
  /// what is copied, what pastes it, and the way out (a slot's LOAD LOOP: the get-a-loop picker for it).
  void drawCopy() {
    constexpr int16_t x0 = 60, y0 = 3, x1 = 195, y1 = 31, sh = 3;
    oled_.gfx.draw_rect_filled(fb(), x0 + sh, y0 + sh, x1 + sh, y1 + sh, 2);
    oled_.gfx.draw_rect_filled(fb(), x0, y0, x1, y1, 0);
    for (int16_t x = x0; x <= x1; ++x) {
      px(x, y0, 10);
      px(x, y1, 10);
    }
    for (int16_t y = y0; y <= y1; ++y) {
      px(x0, y, 10);
      px(x1, y, 10);
    }
    char title[24], name[6];
    const char* paste = "PRESS A SLOT TO PASTE";
    if (edit_.copy == 1) {
      snprintf(title, sizeof title, "COPY LOOP %u-%u", edit_.copySlot / 8 + 1, edit_.copySlot % 8 + 1);
    } else if (edit_.copy == 2) {
      snprintf(title, sizeof title, "COPY BAR %u", edit_.copyBar + 1);
      paste = edit_.copyPaste == 1 ? "PRESS A BAR: REPLACE" : edit_.copyPaste == 2 ? "PRESS A BAR: MERGE"
                                                                                  : "BAR: PRESS REPLACE 2* MERGE";
    } else {
      ContentEngine::status::noteName(edit_.copyPitch, name);
      snprintf(title, sizeof title, "COPY NOTE %s", name);
      paste = edit_.copyPaste == 1 ? "PRESS A STEP: REPLACE" : edit_.copyPaste == 2 ? "PRESS A STEP: MERGE"
                                                                                    : "STEP: PRESS REPLACE 2* MERGE";
    }
    label(title, static_cast<int16_t>((x0 + x1 - static_cast<int16_t>(strlen(title)) * 4) / 2), y0 + 4, 15);
    label(paste, static_cast<int16_t>((x0 + x1 - static_cast<int16_t>(strlen(paste)) * 4) / 2), y0 + 13, 11);
    if (edit_.copy != 1) {  // a bar / a note: REC / EDIT choose the paste first (DEC-279 amendment 8)
      const char* way = "[REC] REPLACE  [EDT] MERGE";
      int16_t w = 0;
      for (const char* c = way; *c; ++c) w = static_cast<int16_t>(w + 4);
      hintLabel(way, static_cast<int16_t>((x0 + x1 - w) / 2), static_cast<int16_t>(y1 - 7));
    } else {  // a slot: the picker, or cleared (owner 2026-11-10: a double press of the source)
      const char* way = "HOLD [TRK] LOAD LOOP  2* CLEAR";
      int16_t w = 0;
      for (const char* c = way; *c; ++c) w = static_cast<int16_t>(w + 4);
      hintLabel(way, static_cast<int16_t>((x0 + x1 - w) / 2), static_cast<int16_t>(y1 - 7));
    }
    // The countdown (owner 2026-11-10: it closes by itself): the bottom edge, bright for what is left.
    const int16_t left = static_cast<int16_t>(x0 + 1 + static_cast<int32_t>(x1 - x0 - 2) * edit_.copyLeft / 1000);
    if (left > x0 + 1) hline(static_cast<int16_t>(x0 + 1), left, static_cast<int16_t>(y1 - 1), 15);
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

  SlotInfo drawSlot(ContentEngine::EngineRoot& e, uint8_t track, uint8_t slot, uint32_t lastTick, bool queued = false) {
    auto& x = e.transport();
    SlotInfo info;
    info.slot = slot;
    const bool writing = x.writingSlot() == slot;
    const ContentEngine::ManifestId m = x.contentOf(slot);  // a cleared slot (an empty state) shows as empty
    uint32_t loop = m == ContentEngine::kNoManifest ? 0 : e.manifests().manifest(m).loopLengthTicks;
    uint32_t pos = 0;
    const bool playing = !queued && x.playing(track) && x.playingSlot(track) == slot;
    bool firstTake = false;
    if (writing) {
      pos = x.writePosition(lastTick);
      if (loop == 0 || loop > 256 * kBar) {  // a first take: the view grows with it (legacy)
        loop = pos == 0 ? 1 : pos;
        firstTake = true;
      }
    } else if (playing && loop != 0) {
      pos = (lastTick - x.origin(track)) % loop;
    } else if (queued && loop != 0) {  // parked where it will start: its page shown
      pos = queuedPos_[track] % loop;
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
    } else if (queued && (drawMs_ / 250) % 2 == 0) {  // queued: parked at its launch point, flashing
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
      if (edit_.ready && edit_.encoder == 1) hline(x0, xr, kLabY + 6, 15);  // the encoder moves the start (slice E)
    } else {
      // The transport icon, left of the counter (DEC-282 amendment 2, owner 2026-11-10).
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
      icon(ic, x0, kValBase - 6, iv);
      // The progress bar, the column's full width (amendment 2): the stored loop always (amendment 1), else the
      // loop's progress while it moves.
      if (edit_.barScale != 0 && info.loop != 0 && queued == 0) {
        drawSection(x0, xr, info);
      } else if (info.moving && queued == 0 && info.loop != 0) {
        hline(x0, xr, kLabY + 2, 3);
        hline(x0, static_cast<int16_t>(x0 + static_cast<int64_t>(info.pos) * (xr - x0) / info.loop), kLabY + 2, 13);
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
    rlabel("BARS", static_cast<int16_t>(x0 + kBarsW - 1), kLabY, 5);  // right-aligned over its number (amendment 2)
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
      static constexpr int8_t kEncoderCell[3] = {0, 3, 2};  // NOTE pitch, VELOCITY, LENGTH (slice E)
      if (noteEdit && edit_.ready && edit_.encoder == kEncoderCell[i])  // the encoder's option: a line under its label
        hline(x0, static_cast<int16_t>(x0 + cells[i].width - 1), kLabY + 6, 15);
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
    // Save dots over the tempo while saving, or RO before saving is approved (DEC-282 amendment 2: the tempo's label
    // row is the static one; the dots had hidden the progress bar).
    if (!started_) {
      dots_.start(p.stats().saves, p.stats().saveFailed);
      started_ = true;
    }
    if (!p.approved()) {
      rlabel("RO", kX1, kLabY, 4);
    } else {
      const st::SaveDots d = dots_.update(nowMs, p.writing(), p.unsaved(), p.stats().saves, p.stats().saveFailed);
      for (uint8_t i = 0; i < 4 && d != st::SaveDots::Off; ++i) {
        const uint8_t v = d == st::SaveDots::Pending ? 2
                          : d == st::SaveDots::Busy  ? (st::SaveIndicator::step(nowMs) == i ? 8 : 2)
                          : d == st::SaveDots::Done  ? 15
                                                     : 6;
        px(static_cast<int16_t>(kX1 - 9 + i * 3), kLabY + 2, v);
      }
    }
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
  bool queued_[ContentEngine::kTrackCount] = {};
  uint32_t queuedPos_[ContentEngine::kTrackCount] = {};
  uint32_t drawMs_ = 0;  // the frame being drawn (a queued slot's playhead flashes on it)
  uint8_t focus_ = 0xFF;
  bool started_ = false;
  uint32_t lastFrameMs_ = 0, frames_ = 0, worstUs_ = 0;
  uint32_t frameMs_ = kFrameMs;
  uint8_t clock_ = 0x91, phase_ = 0xE2;  // the library's boot values
};

}  // namespace EngineFirmware

#endif  // ENGINE_FIRMWARE
