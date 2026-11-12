//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The engine firmware's screen.
//
// The SSD1322 (256 × 64, 4-bit) through the `SSD1322_OLED` library: one frame every `kFrameMs`, drawn into the
// library's framebuffer and sent by DMA (the library copies the frame first, so the next one can be drawn at once).
// The first frame after `begin` goes over blocking SPI, which arms the panel's GRAM window (the library's resync path):
// the panel stays blank when the first frame is a DMA one.
// Frames are written off-screen (`SSD1322_API::setPageFlip`): to the 64 GRAM rows not shown, the display start line
// moved there once complete, so the panel never scans a half-written frame (the playhead tears).
// `!ENGINE oled-flip 0` turns it off for a comparison.
// The layout (no sidebar; every field has a fixed place and shows `-` when it has nothing to show):
// * Left column (x 0–21): the 8 tracks' numbers. While the clock runs, each track's activity level (Cubase's activity
//   column): the last NoteOn's velocity, falling over 0.5 s (`ClockOutput::level`), dim on a muted or solo-hidden
//   track. While it does not, the state letter (R recording, O overdubbing, A armed, P playing, S stopped with content
//   in its selected slot, - its selected slot empty, M muted or hidden by another track's solo — the selected track
//   shows its real state); the selected track's letter pulses (1 Hz, brightness 7–13).
// * Piano roll (x 22–255, rows 0–45; four dark rows under it, no rule before the status band): the shown slot — the
//   whole loop up to 16 bars, else a 16-bar page following the playhead — with bar lines (and beat lines up to 4 bars),
//   the engine's tiles and live overlay (`EngineRoot::drawRollBand`) and the playhead. No overview strip (too crowded;
//   BARS and the position say where the page is).
// * Status band (rows 50–63, the values on the bottom row; 3 × 5 labels over 5 × 7 values, every number right-aligned,
//   no leading zeros):
//   - the current tick: the save dots (4 dots: dim pending, one running while writing, bright 0.8 s after a save, mid
//     after a failure; `RO` before saving is approved) and the transport icon (stop, play, record, overdub, a pulsing
//     record circle when armed) over the position `bar.beat.16th.tick` (the bar bright); while no dots show, the loop's
//     progress runs in their row up to the icon. A queued slot or Record counts down to its start (`-1.3.2.24`). In
//     NOTE_EDIT the block reads START over the selected note's start (the bracket's in length mode), all bright.
//   - BARS: the loop's length in bars (a first take: its bars so far).
//   - the selection: TRACK, SLOT (1–8 within the track) and the port over its channel (`USB1 1`; `ALL` when the track
//     plays on every port); in NOTE_EDIT, NOTE (C3 = 60), VELOCITY (`M`: muted) and LENGTH (ticks); in LOOP_EDIT, START
//     (bar), LENGTH (bars) and BPM; their labels inverted.
//   - UNDO (the slot's undo steps), ODUB (the open overdub's) or EDIT (the open NOTE_EDIT session's).
//   - the tempo under the clock icon: dim on the internal clock; an arrow into it, bright, on an external one.
// * NOTE_EDIT (`EditView`): the window frozen at the session's, the selected note highlighted, the bracket as a dotted
//   line.
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

#include "ContentEngine/Timing/EngineClock.h"
#include "ContentEngine/ControlSurface/SetBrowser.h"
#include "ContentEngine/EngineRoot.h"
#include "ContentEngine/Display/StatusView.h"
#include "ContentEngine/Timing/WallClock.h"

/// Cold drawing: the browser's screens run from flash. Their own `.flashmem.*` section — the Teensy linker sends every
/// `.flashmem*` section to flash — because these are inline members, which may not share `FLASHMEM`'s section with the
/// non-inline functions placed there.
#define DISPLAY_COLD __attribute__((section(".flashmem.display")))

namespace Teensy {

/// What NOTE_EDIT shows (from the surface's `ControlSurfaceNoteEdit`).
struct EditView {
  bool active = false, ready = false;  // a session gathering / open
  uint8_t slot = 0xFF;
  uint32_t first = 0, ticks = 0, bracket = 0;
  bool note = false, length = false;
  uint32_t noteTick = 0, noteLength = 0;
  uint8_t notePitch = 0, noteVelocity = 0;
  uint8_t steps = 0;  // the session's E: level
  bool velocity = false;  // NOTELEN held: F4 is the velocity
  int8_t encoder = -1;    // the encoder's option: 0 pitch, 1 move, 2 length, 3 velocity
  uint8_t copy = 0;       // copy mode: 0 none, 1 a slot, 2 a bar, 3 a note
  uint8_t copySlot = 0, copyBar = 0, copyPitch = 0;  // its source: the engine slot, the bar (0–7), the note's pitch
  uint16_t copyLeft = 0;  // the countdown left, of 1000 (the window's bottom line shrinks with it)
  uint8_t copyPaste = 0;  // a bar / note paste: 0 by the press, 1 replace, 2 merge
  uint32_t barScale = 0, barStart = 0, barKnown = 0;  // the progress bar's frame (0: the plain bar)
  bool loop = false;      // LOOP_EDIT
  uint32_t loopTicks = 0;  // LOOP_EDIT's F2 moved, not yet applied: the pending length (ticks)
  uint32_t loopStart = 0;  // LOOP_EDIT's F1: the start, ticks from the content LOOP_EDIT found
  bool popup = false;      // the length popup
  uint8_t popupSlot = 0;
  uint32_t popupLoop = 0;  // the length it shows (ticks)
  uint8_t popupChoice = 0xFF;  // its selected choice (0 BASE, 1 CUSTOM, 2–13 the lengths)
  uint8_t popupRanges = 0;     // the remembered ranges there are (bit 0 A, bit 1 B)
  // The GROOVE window: open, its slot, selected field, the settings shown, the build's state (0 idle,
  // 1 waiting, 2 building, 3 ready, 4 too big, 5 failed).
  bool groove = false;
  uint8_t grooveSlot = 0, grooveField = 0, grooveTemplate = 0, grooveAmount = 0, grooveScope = 0, grooveState = 0;
  uint8_t grooveProgress = 0xFF;  // percent until a change is heard (0xFF: none)
  uint8_t grooveATemplate = 0, grooveAAmount = 0;  // A (both A and B edited in the window)
  uint8_t grooveHeard = 2;                         // 0 OFF, 1 A, 2 B
  bool grooveCycling = false;
  uint8_t grooveDots = 0;                          // 0 none, 1 pending, 2 building, 3 heard (bright)
  uint8_t grooveCycleBars = 2;                     // bars a turn
  uint16_t grooveTurnPos = 0;                      // the turn playing, in 1/1000
  bool grooveAuto = false;
};

/// What the sets browser shows beside its model: the working set's save state ("SAVED 14:32"),
/// what return brings back ("TO SET 02 AS LEFT 14:57"), the time zone for dates.
struct BrowserInfo {
  char saved[16] = {};
  char returnTo[32] = {};
  int16_t tz = 0;
  uint32_t now = 0;  // the wall time (0: not set): the timeline's right end
  bool pickSquares = false;  // the picker's empty slots as dim squares, else dots (compare on the device)
};

class OledScreen {
 public:
  static constexpr uint32_t kFrameMs = 33;
  static constexpr int16_t kX0 = 22, kX1 = 255, kRollY0 = 0, kRollY1 = 45;
  static constexpr int16_t kLabY = 50, kValBase = 64;  // labels rows 50–54, values rows 57–63 (the bottom)
  static constexpr uint32_t kViewBars = 16;
  static constexpr uint8_t kRule = 3;  // the roll's bar lines and the status band's cell dividers

  void begin() {
    oled_.begin();
    oled_.gfx.set_buffer_size(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    oled_.gfx.select_font(&Font5x7FixedMono);
    oled_.gfx.fill_buffer(oled_.api.getFrameBuffer(), 0);
    sendBlocking();  // arms the GRAM window before the first DMA frame
    // Off-screen frames (the playhead tore while the panel scanned a frame being written; no tear
    // with them): each frame goes to the GRAM rows not shown, then the start line moves there. Both pages blank.
    oled_.api.setPageFlip(true);
    sendBlocking();
    for (uint8_t& s : shown_) s = 0xFF;
    focus_ = 0xFF;
    started_ = false;
  }
  /// The firmware's version on the title.
  static constexpr const char* kVersion = "v0.7";
  /// The title (a full raster-bar title flickers on the panel — too much of it lit — so a light
  /// raster: thin 1-px lines every 4 rows at grey 6–11, symmetric about it, the letters kept clear of them by a
  /// black pixel): `OSTINATIX` in the 5 × 7 font four times its size,
  /// centred, the version at the bottom right — from the display's start until the boot load has ended (the first
  /// performance frame then opens on the saved selection).
  DISPLAY_COLD void drawTitle() {
    uint8_t* f = fb();
    oled_.gfx.fill_buffer(f, 0);
    static const char kTitle[] = "OSTINATIX";
    constexpr int16_t kScale = 4, kAdvance = 6;
    const int16_t w = static_cast<int16_t>((sizeof kTitle - 1) * kAdvance * kScale - kScale);
    int16_t x = static_cast<int16_t>((DISPLAY_WIDTH - w) / 2);
    const int16_t top = static_cast<int16_t>((DISPLAY_HEIGHT - 7 * kScale) / 2);
    // 12 lines (rows 10–54), symmetric about the title's centre (row 32): grey 11 for the two either side of it, down
    // to 6 at the edges (1–4 not visible; a repeating 4–9 left the lower half too dark; centred on row 28 the
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
  /// The last frame drawn as 64 lines `#FB,<row>,<256 hex digits>` (4-bit gray, two pixels a byte, high nibble first),
  /// for `scripts/render_display_dump.py`. Diagnostics: ~17 KB on the serial port.
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
  /// Panel timing (diagnostics, for a tear — the panel scanning rows while a frame is written): `reg` 0xB3 front clock
  /// (high nibble oscillator, low nibble divide − 1; boot 0x91) or 0xB1 phase lengths (boot 0xE2), `value` its byte,
  /// sent once the frame in flight is done; 0 leaves it. `frameMs` the frame period (0 leaves it). Not kept over a
  /// reboot.
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

  /// Any SSD1322 register (diagnostics, e.g. a long bar dimming toward its right end): `cmd` and up to two
  /// data bytes, sent once the frame in flight is done. Not kept over a reboot.
  void poke(uint8_t cmd, const uint8_t* data, uint8_t n) {
    while (oled_.api.isDisplayTransferActive()) oled_.api.pollDisplayTransfer();
    oled_.api.SSD1322_API_command(cmd);
    for (uint8_t i = 0; i < n; ++i) oled_.api.SSD1322_API_data(data[i]);
    Serial.printf("#ENGINE,oled,reg=0x%02X,n=%u,d0=0x%02X,d1=0x%02X\n", cmd, n, n > 0 ? data[0] : 0, n > 1 ? data[1] : 0);
  }

  /// The sets browser to draw while it is open, and what it shows beside its model.
  void setBrowser(const ContentEngine::ProductionBrowser* b) { browser_ = b; }
  void setBrowserInfo(const BrowserInfo& info) { browserInfo_ = info; }
  /// One line over the performance screen for `ms` (rule 6: "SAVED  SET 02  T04").
  void toast(const char* s, uint32_t nowMs, uint32_t ms = 2500) {
    snprintf(toast_, sizeof toast_, "%s", s);
    toastUntil_ = nowMs + ms;
  }

  /// The slot a track's piano roll shows (`ControlSurface::shown`): a queued switch's slot (`queued`: its playhead
  /// parked at the launch point, flashing, until the switch), else the playing one, else the selected one.
  /// `queuedPos`: where the queued switch starts (a step or bar button's restart; 0: the loop start).
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
    // A frame is drawn over several passes — the tracks and the slot's grid, the roll in `kRollBands` row bands, then
    // the status and the windows — and sent when complete: the roll alone is too long for one pass at 30 overdub
    // layers (DEC-297, deep-overdub timing). The panel shows each frame whole.
    if (stage_ == 0) {
      if (nowMs - lastFrameMs_ < frameMs_ || oled_.api.isDisplayTransferActive()) return 0;
      lastFrameMs_ = nowMs;
    }
    const uint32_t t0 = micros();
    const bool done = drawStage(e, clock, lastTick, nowMs);
    const uint32_t us = micros() - t0;
    if (done) {
      oled_.api.display();
      ++frames_;
    }
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
  /// `tenths` as whole.tenth right-aligned so its last column is `xr`, with a thin dot (one pixel in the
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
        {'*', 0x0AA8}, {'{', 0x2E92}, {'}', 0x24BA}, {'%', 0x54A5}};  // % the groove amount  // ? ( ) the trash popups; [ ] * (×) { (↑) } (↓) the buttons
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
  /// One stage of the frame (see `service`). True when the frame is complete.
  bool drawStage(ContentEngine::EngineRoot& e, const ContentEngine::EngineClock& clock, uint32_t lastTick,
                 uint32_t nowMs) {
    if (stage_ == 0) {
      oled_.gfx.fill_buffer(fb(), 0);
      auto& x = e.transport();
      frameSel_ = e.control().selectedTrack();
      drawTracks(e, clock, frameSel_, nowMs);
      // The shown slot: the selected track's queued, playing or selected one (`show`), the writing one above all.
      uint8_t slot = shown_[frameSel_];
      const bool queued = queued_[frameSel_] && x.writingTrack() != frameSel_;
      if (x.writingTrack() == frameSel_) slot = x.writingSlot();
      drawMs_ = nowMs;
      frameInfo_ = SlotInfo{};
      roll_.valid = false;
      if (slot < ContentEngine::Config::kSlotCount) {
        if (slot != focus_) {  // span tiles for the shown slot (swept first)
          e.setDisplayFocus(slot, slot);
          focus_ = slot;
        }
        frameInfo_ = drawSlot(e, frameSel_, slot, lastTick, queued);
      }
      stage_ = roll_.valid ? 1 : kRollBands + 1;
      return false;
    }
    if (stage_ <= kRollBands) {  // a band of the roll's rows
      const int16_t rows = static_cast<int16_t>(kRollY1 - kRollY0 + 1);
      const int16_t from = static_cast<int16_t>(kRollY0 + (stage_ - 1) * rows / kRollBands);
      const int16_t to = static_cast<int16_t>(kRollY0 + stage_ * rows / kRollBands - 1);
      e.drawRollBand(roll_.view, fb(), DISPLAY_WIDTH, from, to);
      ++stage_;
      return false;
    }
    stage_ = 0;
    if (roll_.valid) finishSlot();
    drawStatus(e, clock, frameSel_, frameInfo_, lastTick, nowMs);
    if (browser_ != nullptr && browser_->isOpen()) {  // a window over the performance screen
      drawBrowser(*browser_, e);
      if (static_cast<int32_t>(toastUntil_ - nowMs) > 0) drawToast();  // a deletion's line: the browser stays open
      return true;
    }
    if (edit_.popup) drawPopup();
    if (edit_.groove) drawGroove();
    if (edit_.copy != 0) drawCopy();
    if (static_cast<int32_t>(toastUntil_ - nowMs) > 0) drawToast();
    return true;
  }
  /// A whole frame at once (tests, the first frame).
  void draw(ContentEngine::EngineRoot& e, const ContentEngine::EngineClock& clock, uint32_t lastTick, uint32_t nowMs) {
    stage_ = 0;
    while (!drawStage(e, clock, lastTick, nowMs)) {
    }
  }

  // ---- The sets browser (a window over the performance screen, as the length popup; time down the
  // list — year and month sticky on the top row, the day once, the time — on a spine gapped between months; saving and
  // loading split; the save popup as the history with every stage beside it) ----------------------------------------
  using SetBrowser = ContentEngine::ProductionBrowser;
  using Row = SetBrowser::Row;
  using RowKind = SetBrowser::RowKind;
  using Entry = ContentEngine::library::Entry;
  // The window, the list's columns (year, month, day, the spine, id / name, time / right note) and the detail. The
  // day reads with the month (`NOV 9`, the gap of `2026 NOV`), the window narrower and centred.
  static constexpr int16_t kWX0 = 27, kWY0 = 1, kWX1 = 228, kWY1 = 56, kDiv = 137;
  static constexpr int16_t kYearX = 31, kMonX = 49, kDayXR = 71, kSpineX = 77, kIdX = 83, kNoteXR = 133;
  static constexpr int16_t kDetailX = 142, kMosaicX = 192, kMosaicY = 14, kRow0 = 13, kPitch = 9, kHintY = 59;
  static constexpr uint8_t kRuleLevel = 4, kDimText = 7;
  static void idText(const Entry& r, char* out, size_t n);
  static int16_t rowY(const SetBrowser& b, uint16_t i) { return static_cast<int16_t>(kRow0 + (i - b.first()) * kPitch); }
  /// A GEM window: a dim drop shadow, black, a frame, a hatched title bar with the title in its own black box.
  void window(int16_t x0, int16_t y0, int16_t x1, int16_t y1, const char* title);
  /// The bottom row starts with RECORD and EDIT's moves (`[REC]↑ [EDT]↓`).
  static constexpr const char* kMoves = "[REC]{ [EDT]}  ";
  /// Hint text: the buttons (`[TRK]`, brackets too) dimmer than the actions beside them.
  static constexpr uint8_t kHintV = 9, kButtonV = kDimText;  // the unselected menu text
  int16_t hintLabel(const char* s, int16_t x, int16_t top);
  void hint(const char* s);
  /// The bottom right (a long press exits every window): `HOLD EXIT`, or right after a move to the
  /// trash `3× [TRK] UNDO` beside a can, the can as bright as the words.
  void drawTrashCorner(bool undo);
  /// The entry a row stands for (its date on the spine), or kNone.
  static uint16_t entryOf(const Row& r);
  ContentEngine::wallclock::Local when(const Entry& m) const;
  DISPLAY_COLD void drawBrowser(const SetBrowser& b, ContentEngine::EngineRoot& e);
  /// The target exactly (`MOVE SET 01 TO TRASH?` with ALL SAVED STAGES, or `MOVE SET 01 S01 TO TRASH?` with
  /// its stage) and when it was saved — or `EMPTY THE TRASH?`, for good — over the list; YES and CANCEL, the selected
  /// one inverted.
  DISPLAY_COLD void drawConfirm(const SetBrowser& b);
  /// YES and CANCEL, the selected one inverted, and the confirmation's line.
  DISPLAY_COLD void drawButtons(const SetBrowser& b, int16_t x0, int16_t x1, int16_t buttonY);
  /// A label centred on `xc`.
  void clabel(const char* s, int16_t xc, int16_t top, uint8_t v);

  // ---- Get a loop's picker ------------------------------------------------------------------------------
  static constexpr int16_t kPresetX = 33, kPresetW = 13, kPresetH = 7, kPresetPitch = 8, kGridX = 52, kGridY = 13;
  static constexpr int16_t kCell = 4, kPitchX = 6, kPitchY = 5;
  static constexpr int16_t kPanelX0 = 141, kPanelX1 = 225, kPanelY0 = 21, kPanelY1 = 44;
  // The browser's drawing runs from flash (RAM1 is assigned in 32 KiB banks; the groove builder took a bank's edge).
  DISPLAY_COLD void drawPicker(const SetBrowser& b, ContentEngine::EngineRoot& e);
  void panelBox();
  void fillRect(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t v) { oled_.gfx.draw_rect_filled(fb(), x0, y0, x1, y1, v); }
  /// Why an audition is silent (the owner's code): 1 stopped, 2 the slot not playing, 3 muted, 4 + t: track t soloed.
  static const char* silentText(uint8_t why, char* buf, size_t n);
  /// The loop under the cursor, whole (every bar for now), from the preview slot's tiles.
  void drawPreviewRoll(ContentEngine::EngineRoot& e, uint16_t bars, bool dim);
  /// The spine: a line down the dated rows (sets, stages), broken between months.
  DISPLAY_COLD void drawSpine(const SetBrowser& b);
  DISPLAY_COLD void drawRow(const SetBrowser& b, uint16_t i);
  /// A milestone: its id three times the size, the stage and a second line under it; the mosaic beside.
  DISPLAY_COLD void drawMilestone(const Entry& m, const char* line2, const char* line3);
  DISPLAY_COLD void drawDetail(const SetBrowser& b, const Row& r, char* h, size_t n);
  /// The 8 × 8 slots (rows tracks, columns slots), brighter for more content: a milestone's, from its entry.
  void drawMosaic(const Entry& m);
  /// The working set's, live: each slot's rows against the fullest slot's, as a milestone's are stored.
  void drawLiveMosaic();
  void drawCells(const uint8_t* lv);
  /// SAVE CURRENT: left, AS NEW SET on a lane of its own, then the new node on the working set's
  /// history (newest first, the base's line on the main lane); right, every stage with the id it would get, the chosen
  /// one bright and wired to the new node, `<` on the suggested one.
  DISPLAY_COLD void drawSavePopup(const SetBrowser& b);
  /// The one line after a save, load or return (rule 6), over the performance screen.
  void drawToast();

  /// The length popup, a GEM-style window over the roll with the main screen around it — a dim drop shadow (black would
  /// vanish on the dark screen), a frame, a GEM title bar (hatched rows, the title in its own black box), the length
  /// large, the gestures in small letters. Kept light: the panel dims as a whole as its total load rises — a filled
  /// bright title bar and grey interior take the frame's summed levels from ~5,000 to ~25,000.
  /// Every choice in three rows — FULL · A · B, 1–8, 16 · 32 · 64 · 128 — BARS in the title once, the selected one
  /// inverted, the others dim (CUSTOM dimmer while there is none).
  void drawPopup();

  /// The GROOVE window: as the length popup — a shadow, a frame,
  /// the hatched title bar with `GROOVE T<n> S<n>` centred — then (layout A, the small font)
  /// the compare row or the cycle's timeline, B's template and amount with A's dim beside them, BARS and CYCLE, the
  /// slot's AUTO as a checkbox (with `WAIT` / `TOO BIG` / `FAILED`), and the progress bar along the bottom.
  DISPLAY_COLD void drawGroove();

  /// The progress bar over the stored loop: the whole bar is the stored loop (its frame) or the stored notes' reach,
  /// gapped every 1 / 4 / 8 / 16 / 32 bars (the finest that fits); brackets around the part looping now; the playhead
  /// fills it while playing; the loop past the stored notes dotted.
  void drawSection(int16_t x0, int16_t x1, const SlotInfo& info);

  /// Copy mode (its own minimal state): a small window over the roll's top —
  /// what is copied, what pastes it, and the way out (a slot's LOAD LOOP: the get-a-loop picker for it).
  void drawCopy();

  /// The state letter for track `t`.
  static char letter(ContentEngine::EngineRoot& e, uint8_t t, uint8_t sel, uint8_t shown);
  /// Muted, or silent under another track's solo.
  static bool hidden(ContentEngine::EngineRoot& e, uint8_t t);
  /// 1 Hz between 7 and 13 (readable at its darkest): the selected track's state letter,
  /// NOTE_EDIT's mode cell behind its dark (0) label.
  static uint8_t pulseAt(uint32_t nowMs);
  void drawTracks(ContentEngine::EngineRoot& e, const ContentEngine::EngineClock& clock, uint8_t sel, uint32_t nowMs);

  SlotInfo drawSlot(ContentEngine::EngineRoot& e, uint8_t track, uint8_t slot, uint32_t lastTick, bool queued = false);
  void finishSlot();

  // ---- Status band ---------------------------------------------------------------------------------------------------
  /// `bar.beat.16th.tick` right-aligned at `xr` with 1-px dots (the bar at `barV`, the rest at `v`; `sign`: a leading
  /// `-`). Unknown: `-.-.-.--`.
  void position(const ContentEngine::status::Position* p, int16_t xr, uint8_t barV, uint8_t v, bool sign);

  void drawStatus(ContentEngine::EngineRoot& e, const ContentEngine::EngineClock& clock, uint8_t sel,
                  const SlotInfo& info, uint32_t lastTick, uint32_t nowMs);

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
  // The frame being drawn over passes (`service`, `drawStage`).
  static constexpr uint8_t kRollBands = 4;
  uint8_t stage_ = 0;  // 0: none; 1..kRollBands: the next roll band; then the status and the send
  uint8_t frameSel_ = 0;
  struct Roll {
    ContentEngine::RollView view{};
    bool valid = false, playhead = false, queued = false, editing = false;
    uint32_t pos = 0, first = 0, ticks = 1;
  } roll_;
  SlotInfo frameInfo_{};
  uint32_t frameMs_ = kFrameMs;
  uint8_t clock_ = 0x91, phase_ = 0xE2;  // the library's boot values
};

}  // namespace Teensy

#include "Teensy/OledScreenBrowser.h"
#include "Teensy/OledScreenPerformance.h"

#endif  // ENGINE_FIRMWARE
