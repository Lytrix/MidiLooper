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
// The layout (no sidebar; every field has a fixed place and shows `-` when it has nothing to show):
// * Left column (x 0–21): the 8 tracks' numbers. While the clock runs, each track's activity level (Cubase's activity
//   column): the last NoteOn's velocity, falling over 0.5 s (`ClockOutput::level`), dim on a muted or solo-hidden track.
//   While it does not, the legacy state letter (`trackStateToLetter`: R recording, O overdubbing, A armed, P playing,
//   S stopped with content in its selected slot, - its selected slot empty, M muted or hidden by another track's solo
//   — the selected track shows its real state); the selected track's letter pulses (legacy: 1 Hz, brightness 4–10).
// * Piano roll (x 22–255, rows 0–39): the shown slot — the whole loop up to 16 bars, else a 16-bar page following the
//   playhead — with bar lines (and beat lines up to 4 bars), the engine's tiles and live overlay
//   (`EngineRoot::drawRollBand`) and the playhead; the overview strip (rows 43–46, three dark rows under the roll)
//   when the loop is longer than the view: a full-height cell per beat as bright as the pitches sounding in it
//   (2–9), a bright bar under the view's range (row 47).
// * Status band (rows 50–63, the values on the bottom row; 3 × 5 labels over 5 × 7 values, every number
//   right-aligned, no leading zeros):
//   - the current tick: the save dots (legacy, 4 dots: dim pending, one running while writing, bright 0.8 s after a
//     save, mid after a failure; `RO` before saving is approved) and the transport icon (stop, play, record, overdub,
//     a pulsing record circle when armed) over the position `bar.beat.16th.tick` (the bar bright); while no dots show,
//     the loop's progress runs in their row up to the icon. A queued slot or Record counts down to its start (`-1.3.2.24`). In NOTE_EDIT the block reads START over
//     the selected note's start (the bracket's in length mode), all bright.
//   - BARS: the loop's length in bars (a first take: its bars so far).
//   - the selection: TRACK, SLOT (1–8 within the track) and the port over its channel (`USB1 1`; `ALL` when the track
//     plays on every port); in NOTE_EDIT, NOTE (C3 = 60), VELOCITY and LENGTH (ticks), their labels inverted.
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
#include "ContentEngine/EngineRoot.h"
#include "ContentEngine/StatusView.h"

namespace EngineFirmware {

/// What NOTE_EDIT shows (from the surface's `EditSurface`).
struct EditView {
  bool active = false, ready = false;  // a session gathering / open
  uint8_t slot = 0xFF;
  uint32_t first = 0, ticks = 0, bracket = 0;
  bool note = false, length = false;
  uint32_t noteTick = 0, noteLength = 0;
  uint8_t notePitch = 0, noteVelocity = 0;
};

class EngineDisplay {
 public:
  static constexpr uint32_t kFrameMs = 33;
  static constexpr int16_t kX0 = 22, kX1 = 255, kRollY0 = 0, kRollY1 = 39, kOverY0 = 43, kOverY1 = 46;
  static constexpr int16_t kSepY = 48, kLabY = 50, kValBase = 64;  // labels rows 50–54, values rows 57–63 (the bottom)
  static constexpr uint32_t kViewBars = 16;

  void begin() {
    oled_.begin();
    oled_.gfx.set_buffer_size(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    oled_.gfx.select_font(&Font5x7FixedMono);
    oled_.gfx.fill_buffer(oled_.api.getFrameBuffer(), 0);
    sendBlocking();  // arms the GRAM window before the first DMA frame
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
                   uint32_t nowMs, const EditView& edit = EditView{}) {
    edit_ = edit;
    oled_.api.pollDisplayTransfer();
    if (nowMs - lastFrameMs_ < kFrameMs || oled_.api.isDisplayTransferActive()) return 0;
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
  void sendBlocking() {
    oled_.api.SSD1322_API_set_window(0, 63, 0, 63);
    oled_.api.SSD1322_API_command(SSD1322_WRITE_RAM);
    oled_.api.SSD1322_API_send_buffer(fb(), FRAMEBUFFER_SIZE);
  }
  void text(const char* s, int16_t x, int16_t y, uint8_t brightness) { oled_.gfx.draw_text(fb(), s, x, y, brightness); }
  void px(int16_t x, int16_t y, uint8_t v) {
    if (x >= 0 && x < DISPLAY_WIDTH && y >= 0 && y < DISPLAY_HEIGHT) oled_.gfx.draw_pixel(fb(), x, y, v);
  }
  /// 5 × 7 text right-aligned so its last column is `xr`.
  void rtext(const char* s, int16_t xr, uint8_t v) {
    text(s, static_cast<int16_t>(xr - static_cast<int16_t>(strlen(s)) * 6 + 2), kValBase, v);
  }

  // ---- 3 × 5 labels and 5-row icons ----------------------------------------------------------------------------------
  /// Bit 14 is the top-left pixel, rows of 3 (row-major).
  static uint16_t micro(char c) {
    static const struct { char c; uint16_t bits; } kGlyphs[] = {
        {'-', 0x01C0}, {'0', 0x2B6A}, {'1', 0x2C97}, {'2', 0x62A7}, {'3', 0x628E}, {'4', 0x5BC9}, {'5', 0x798E},
        {'6', 0x39AA}, {'7', 0x7292}, {'8', 0x2AAA}, {'9', 0x2ACE}, {'A', 0x2BED}, {'B', 0x6BAE}, {'C', 0x3923},
        {'D', 0x6B6E}, {'E', 0x79A7}, {'F', 0x79A4}, {'G', 0x396B}, {'H', 0x5BED}, {'I', 0x7497}, {'J', 0x126A},
        {'K', 0x5BAD}, {'L', 0x4927}, {'M', 0x5F6D}, {'N', 0x6B6D}, {'O', 0x2B6A}, {'P', 0x6BA4}, {'Q', 0x2B6B},
        {'R', 0x6BAD}, {'S', 0x388E}, {'T', 0x7492}, {'U', 0x5B6F}, {'V', 0x5B6A}, {'W', 0x5BFD}, {'X', 0x5AAD},
        {'Y', 0x5A92}, {'Z', 0x72A7}};
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
  void separator(int16_t x) { oled_.gfx.draw_vline(fb(), x, kLabY - 1, 63, 2); }
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
    hline(kX0, kX1, kSepY, 2);
    drawStatus(e, clock, sel, info, lastTick, nowMs);
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
  void drawTracks(ContentEngine::EngineRoot& e, const ContentEngine::EngineClock& clock, uint8_t sel, uint32_t nowMs) {
    char label[2] = {0, 0};
    // The selected track's letter pulses: 1 Hz between 4 and 10 (legacy `minPulse` / `maxPulse`).
    const float phase = static_cast<float>(nowMs % 1000) / 1000.0f;
    const uint8_t pulse = static_cast<uint8_t>(4.0f + 6.0f * (0.5f + 0.5f * sinf(phase * 2.0f * 3.1415926f)));
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
      oled_.gfx.draw_vline(fb(), gx, kRollY0, kRollY1, b % 4 == 0 ? 3 : 1);
    }
    RollView v{slot, loop, kBar, first, viewTicks, lo, hi, kX0, kX1, kRollY0, kRollY1};
    if (editing && edit_.note) {
      v.highlightTick = edit_.noteTick;
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
    // Overview: the whole loop, the view's range and the playhead (when the view does not already show it whole).
    if (loop > viewTicks) drawOverview(e, slot, loop, first, viewTicks, pos, playing || writing);
    return info;
  }

  void drawOverview(ContentEngine::EngineRoot& e, uint8_t slot, uint32_t loop, uint32_t first, uint32_t viewTicks,
                    uint32_t pos, bool moving) {
    const int32_t w = kX1 - kX0;
    ContentEngine::drawOverviewBand(e.tiles(), slot, loop, kBar, kX0, kX1, kOverY0, kOverY1, fb(), DISPLAY_WIDTH,
                                    kOverY0, kOverY1, static_cast<uint8_t>(kBar / kQuarter));
    // Under the strip: a bright bar under the detailed view's range, a dim one under the rest.
    const int16_t vx0 = static_cast<int16_t>(kX0 + static_cast<int64_t>(first) * w / loop);
    int16_t vx1 = static_cast<int16_t>(kX0 + static_cast<int64_t>(first + viewTicks) * w / loop);
    if (vx1 > kX1) vx1 = kX1;
    hline(kX0, kX1, kOverY1 + 1, 2);
    hline(vx0, vx1, kOverY1 + 1, 13);
    if (moving)
      oled_.gfx.draw_vline(fb(), static_cast<int16_t>(kX0 + static_cast<int64_t>(pos) * w / loop), kOverY0, kOverY1, 12);
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
        snprintf(cells[1].value, sizeof cells[1].value, "%u", edit_.noteVelocity);
        snprintf(cells[2].value, sizeof cells[2].value, "%lu", static_cast<unsigned long>(edit_.noteLength));
      }
      const int16_t end = static_cast<int16_t>(x0 + 18 + 32 + 24 + 2 * kGap - 1);
      oled_.gfx.draw_rect_filled(fb(), static_cast<int16_t>(x0 - 1), kLabY - 1, static_cast<int16_t>(end + 1),
                                 kLabY + 5, 11);
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
      label(cells[i].label, x0, kLabY, noteEdit ? 0 : 5);
      rtext(cells[i].value, static_cast<int16_t>(x0 + cells[i].width - 1), noteEdit || i < 2 ? 13 : 11);
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
      steps = h.sessionDepth(ContentEngine::SessionKind::Edit);
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
    const uint16_t tenths = st::tempoTenths(clock.usPerTick(), kQuarter);
    snprintf(buf, sizeof buf, "%u", tenths / 10);
    const int16_t wWhole = static_cast<int16_t>(strlen(buf)) * 6;
    const int16_t tx = static_cast<int16_t>(kX1 - (wWhole + 1 + 5) + 1);  // the legacy thin dot before the tenth
    const uint8_t tv = ext ? 12 : 9;
    text(buf, tx, kValBase, tv);
    px(static_cast<int16_t>(tx + wWhole - 1), kValBase - 1, tv);
    char tenth[2] = {static_cast<char>('0' + tenths % 10), 0};
    text(tenth, static_cast<int16_t>(tx + wWhole + 1), kValBase, tv);
  }

  SSD1322 oled_;
  EditView edit_;
  ContentEngine::status::SaveIndicator dots_;
  uint8_t shown_[ContentEngine::kTrackCount] = {};
  uint8_t focus_ = 0xFF;
  bool started_ = false;
  uint32_t lastFrameMs_ = 0, frames_ = 0, worstUs_ = 0;
};

}  // namespace EngineFirmware

#endif  // ENGINE_FIRMWARE
