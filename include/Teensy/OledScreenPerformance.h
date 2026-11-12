//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// `OledScreen`'s performance screen: the length popup, the GROOVE and copy windows, the tracks, the piano roll and the status band: the member function bodies, declared (and documented) in `OledScreen.h`, which includes this
// file after the class.

#pragma once

#if defined(ENGINE_FIRMWARE)

#include "Teensy/OledScreen.h"

namespace Teensy {

inline auto OledScreen::drawPopup() -> void {
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
  static const char* const kChoice[15] = {"FULL", "A", "B", "1", "2", "3", "4", "5", "6", "7", "8", "16", "32", "64", "128"};
  static constexpr uint8_t kRow[4] = {0, 3, 11, 15};     // the rows' first choices
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
        label(kChoice[i], x, y, (i == 1 || i == 2) && !((edit_.popupRanges >> (i - 1)) & 1) ? 2 : 6);
      }
      x = static_cast<int16_t>(x + iw + kGap[r]);
    }
  }
  static const char kHint[] = "PRESS +  2X -  HOLD CANCEL";  // next length, the one before, back as opened
  label(kHint, static_cast<int16_t>((x0 + x1 - static_cast<int16_t>(sizeof kHint - 1) * 4) / 2),
        static_cast<int16_t>(y1 - 7), 5);
}

inline auto OledScreen::drawGroove() -> void {
  // Laid out as the load / save window, static; only the highlight moves:
  // OFF · A · B over two columns of GROOVE and AMOUNT, AUTO Q in the title bar, the keys along the bottom.
  constexpr int16_t x0 = kWX0, y0 = 1, x1 = kWX1, y1 = 51;
  constexpr int16_t lx = x0 + 6, ax = x0 + 52, bx = x0 + 130;
  oled_.gfx.draw_rect_filled(fb(), x0 + 3, y0 + 3, x1 + 3, y1 + 3, 2);
  oled_.gfx.draw_rect_filled(fb(), x0, y0, x1, y1, 0);
  for (int16_t y = static_cast<int16_t>(y0 + 2); y < y0 + 9; y += 2) hline(x0 + 1, x1 - 1, y, 5);
  for (int16_t x = x0; x <= x1; ++x) px(x, y0, 10), px(x, static_cast<int16_t>(y0 + 9), 10), px(x, y1, 10);
  for (int16_t y = y0; y <= y1; ++y) px(x0, y, 10), px(x1, y, 10);
  char title[24];
  snprintf(title, sizeof title, "GROOVE T%u S%u", edit_.grooveSlot / 8 + 1, edit_.grooveSlot % 8 + 1);
  const int16_t tw = static_cast<int16_t>(strlen(title)) * 4 + 3, tx = static_cast<int16_t>((x0 + x1 - tw) / 2);
  oled_.gfx.draw_rect_filled(fb(), tx, static_cast<int16_t>(y0 + 1), static_cast<int16_t>(tx + tw), y0 + 8, 0);
  label(title, static_cast<int16_t>(tx + 2), static_cast<int16_t>(y0 + 2), 13);
  {  // AUTO Q: a checkbox in the title bar (GEM's widget place), TRK's (as in the load / save window)
    const char* t = "[TRK]";
    const int16_t w = static_cast<int16_t>(strlen(t)) * 4 + 4 + 7 + 24 + 3, x = static_cast<int16_t>(x1 - 4 - w);
    oled_.gfx.draw_rect_filled(fb(), x, static_cast<int16_t>(y0 + 1), static_cast<int16_t>(x + w), y0 + 8, 0);
    int16_t cx = hintLabel(t, static_cast<int16_t>(x + 2), static_cast<int16_t>(y0 + 2));
    cx = static_cast<int16_t>(cx + 3);
    for (int16_t i = 0; i < 5; ++i)
      px(static_cast<int16_t>(cx + i), static_cast<int16_t>(y0 + 2), 10), px(static_cast<int16_t>(cx + i), static_cast<int16_t>(y0 + 6), 10),
          px(cx, static_cast<int16_t>(y0 + 2 + i), 10), px(static_cast<int16_t>(cx + 4), static_cast<int16_t>(y0 + 2 + i), 10);
    if (edit_.grooveAuto)
      oled_.gfx.draw_rect_filled(fb(), static_cast<int16_t>(cx + 1), static_cast<int16_t>(y0 + 3),
                                 static_cast<int16_t>(cx + 3), static_cast<int16_t>(y0 + 5), 15);
    label("AUTO Q", static_cast<int16_t>(cx + 7), static_cast<int16_t>(y0 + 2), 13);
  }
  const auto inv = [&](const char* s, int16_t x, int16_t y) {
    const int16_t w = static_cast<int16_t>(strlen(s)) * 4 + 1;
    oled_.gfx.draw_rect_filled(fb(), static_cast<int16_t>(x - 2), static_cast<int16_t>(y - 1),
                               static_cast<int16_t>(x + w), static_cast<int16_t>(y + 5), 13);
    label(s, x, y, 0);
  };
  // The heard one (0 OFF, 1 A, 2 B), inverted; cycling, it moves between A and B.
  const int16_t hy = static_cast<int16_t>(y0 + 13);
  static const char* const kHead[3] = {"OFF", "A", "B"};
  const int16_t hx[3] = {lx, ax, bx};
  for (uint8_t i = 0; i < 3; ++i) {
    if (edit_.grooveHeard == i) inv(kHead[i], hx[i], hy);
    else label(kHead[i], hx[i], hy, 10);
  }
  for (int16_t y = static_cast<int16_t>(y0 + 11); y < y0 + 40; ++y) px(ax - 7, y, 4), px(bx - 7, y, 4);
  hline(x0 + 3, x1 - 3, static_cast<int16_t>(y0 + 20), 4);
  static const char* const kTemplate[] = {"STRAIGHT", "MPC16 54", "MPC16 58", "MPC16 62", "MPC16 66", "MPC16 71",
                                          "MPC8 58",  "MPC8 66",  "SP16 54",  "SP16 58",  "SP16 63",  "SP16 67",
                                          "SP16 71",  "TRIPLET 8", "TRIPLET 16", "JAZZ RIDE", "BOSSA", "LAID BACK",
                                          "PUSH"};
  static_assert(sizeof kTemplate / sizeof kTemplate[0] == ContentEngine::groove::kTemplateCount, "every template named");
  const auto name = [&](uint8_t t) {
    return t < ContentEngine::groove::kTemplateCount ? kTemplate[t] : t == ContentEngine::groove::kFileTemplate ? "FILE" : "?";
  };
  char a[8], b[8];
  snprintf(a, sizeof a, "%u%%", edit_.grooveAAmount);
  snprintf(b, sizeof b, "%u%%", edit_.grooveAmount);
  const char* const cell[2][2] = {{name(edit_.grooveATemplate), name(edit_.grooveTemplate)}, {a, b}};
  static const char* const kRow[2] = {"GROOVE", "AMOUNT"};
  for (uint8_t r = 0; r < 2; ++r) {
    const int16_t y = static_cast<int16_t>(y0 + 24 + r * 9);
    label(kRow[r], lx, y, 6);
    for (uint8_t c = 0; c < 2; ++c) {  // fields: A's groove 0, A's amount 1, B's groove 2, B's amount 3
      const int16_t x = c == 0 ? ax : bx;
      if (edit_.grooveField == c * 2 + r) inv(cell[r][c], x, y);
      else label(cell[r][c], x, y, 13);
    }
  }
  static const char* const kState[6] = {"", "WAIT", "", "", "TOO BIG", "FAILED"};
  if (edit_.grooveState < 6 && kState[edit_.grooveState][0] != 0)
    rlabel(kState[edit_.grooveState], static_cast<int16_t>(x1 - 6), hy, edit_.grooveState >= 4 ? 15 : 10);
  // A change on its way as the save dots,
  // bottom left — dim settling, one running while it builds and waits for its bar line or beat, bright once heard.
  const int16_t by = static_cast<int16_t>(y1 - 5);
  for (uint8_t i = 0; i < 4 && edit_.grooveDots != 0; ++i) {
    const uint8_t v = edit_.grooveDots == 1 ? 2
                      : edit_.grooveDots == 2 ? (ContentEngine::status::SaveIndicator::step(drawMs_) == i ? 8 : 2)
                                              : 15;
    px(static_cast<int16_t>(lx + i * 3), by, v);
  }
  // Cycling: under A and B the main screen's loop bar — a gap a bar line, `[ ]` around the one playing, its playhead
  // bright; the other waits dim.
  if (edit_.grooveCycling) {
    const uint8_t bars = edit_.grooveCycleBars == 0 ? 1 : edit_.grooveCycleBars;
    for (uint8_t c = 0; c < 2; ++c) {
      const int16_t c0 = c == 0 ? static_cast<int16_t>(ax - 2) : static_cast<int16_t>(bx - 2);
      const int16_t c1 = c == 0 ? static_cast<int16_t>(bx - 12) : static_cast<int16_t>(x1 - 6);
      const bool on = edit_.grooveHeard == c + 1;
      const int32_t w = c1 - c0;
      const int16_t fill = static_cast<int16_t>(c0 + w * edit_.grooveTurnPos / 1000);
      for (int16_t x = c0; x <= c1; ++x) {
        bool gap = false;
        for (uint8_t k = 1; k < bars; ++k) gap |= x == c0 + w * k / bars;
        if (!gap) px(x, by, on ? (x <= fill ? 15 : 6) : 3);
      }
      if (!on) continue;
      for (int16_t e : {c0, c1})
        for (int16_t dy : {-2, -1, 1, 2}) px(e, static_cast<int16_t>(by + dy), 12);
      px(static_cast<int16_t>(c0 + 1), static_cast<int16_t>(by - 2), 12), px(static_cast<int16_t>(c0 + 1), static_cast<int16_t>(by + 2), 12);
      px(static_cast<int16_t>(c1 - 1), static_cast<int16_t>(by - 2), 12), px(static_cast<int16_t>(c1 - 1), static_cast<int16_t>(by + 2), 12);
    }
  }
  hint(edit_.grooveCycling ? "[REC]{ [EDT]}  [ADD] KEEP  2*OFF  HOLD STOP"  // ADD's three
                           : "[REC]{ [EDT]}  [ADD] A/B  2*OFF  HOLD CYCLE");
  hintLabel("HOLD [PLY] APPLY", static_cast<int16_t>(256 - 16 * 4), kHintY);
}

inline auto OledScreen::drawSection(int16_t x0, int16_t x1, const SlotInfo& info) -> void {
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
  // The loop's place in the stored loop is absolute, so it may wrap past the frame's end to its start.
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

inline auto OledScreen::drawCopy() -> void {
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
  if (edit_.copy != 1) {  // a bar / a note: REC / EDIT choose the paste first
    const char* way = "[REC] REPLACE  [EDT] MERGE";
    int16_t w = 0;
    for (const char* c = way; *c; ++c) w = static_cast<int16_t>(w + 4);
    hintLabel(way, static_cast<int16_t>((x0 + x1 - w) / 2), static_cast<int16_t>(y1 - 7));
  } else {  // a slot: the picker, or cleared (a double press of the source)
    const char* way = "HOLD [TRK] LOAD LOOP  2* CLEAR";
    int16_t w = 0;
    for (const char* c = way; *c; ++c) w = static_cast<int16_t>(w + 4);
    hintLabel(way, static_cast<int16_t>((x0 + x1 - w) / 2), static_cast<int16_t>(y1 - 7));
  }
  // The countdown (it closes by itself): the bottom edge, bright for what is left.
  const int16_t left = static_cast<int16_t>(x0 + 1 + static_cast<int32_t>(x1 - x0 - 2) * edit_.copyLeft / 1000);
  if (left > x0 + 1) hline(static_cast<int16_t>(x0 + 1), left, static_cast<int16_t>(y1 - 1), 15);
}

inline auto OledScreen::letter(ContentEngine::EngineRoot& e, uint8_t t, uint8_t sel, uint8_t shown) -> char {
  auto& x = e.transport();
  auto& c = e.control();
  if (t != sel && hidden(e, t)) return 'M';
  if (x.writingTrack() == t) return x.mode() == Mode::Recording ? 'R' : 'O';
  if (c.queuedRecordSlot(t) != kNone) return 'A';
  if (x.playing(t)) return 'P';
  // Stopped: the selected slot's content (a cancelled arm on an empty slot is '-' again).
  return shown < ContentEngine::Config::kSlotCount && x.contentOf(shown) != ContentEngine::kNoManifest ? 'S' : '-';
}

inline auto OledScreen::hidden(ContentEngine::EngineRoot& e, uint8_t t) -> bool {
  auto& c = e.control();
  bool anySolo = false;
  for (uint8_t i = 0; i < 8; ++i) anySolo |= c.soloed(i);
  return c.muted(t) || (anySolo && !c.soloed(t));
}

inline auto OledScreen::pulseAt(uint32_t nowMs) -> uint8_t {
  const float phase = static_cast<float>(nowMs % 1000) / 1000.0f;
  return static_cast<uint8_t>(7.0f + 6.0f * (0.5f + 0.5f * sinf(phase * 2.0f * 3.1415926f)));
}

inline auto OledScreen::drawTracks(ContentEngine::EngineRoot& e, const ContentEngine::EngineClock& clock, uint8_t sel, uint32_t nowMs) -> void {
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

inline auto OledScreen::drawSlot(ContentEngine::EngineRoot& e, uint8_t track, uint8_t slot, uint32_t lastTick, bool queued) -> SlotInfo {
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
    if (loop == 0 || loop > 256 * kBar) {  // a first take: the view grows with it
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
  // A first take's view ends at the current tick (the playhead at the right edge); else the page holding
  // the playhead.
  uint32_t first = firstTake ? loop - viewTicks : (pos / viewTicks) * viewTicks;
  if (editing && edit_.ready && edit_.ticks != 0) {  // NOTE_EDIT: the session's window, frozen
    first = edit_.first;
    viewTicks = edit_.ticks;
  }
  // Pitch range: the view's bars, from density tiles (auto-fit).
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
  // The roll's rows are drawn in bands on the next passes (`drawStage`); the playhead and bracket after them.
  roll_.view = v;
  roll_.valid = true;
  roll_.playhead = playing || writing;
  roll_.queued = queued;
  roll_.editing = editing;
  roll_.pos = pos;
  roll_.first = first;
  roll_.ticks = viewTicks;
  return info;
}

inline auto OledScreen::finishSlot() -> void {
  const int32_t w = kX1 - kX0;
  const auto& r = roll_;
  if (r.playhead) {
    const int16_t px = static_cast<int16_t>(kX0 + static_cast<int64_t>(r.pos - r.first) * w / r.ticks);
    oled_.gfx.draw_vline(fb(), px, kRollY0, kRollY1, 15);
  } else if (r.queued && (drawMs_ / 250) % 2 == 0) {  // queued: parked at its launch point, flashing
    const int16_t px = static_cast<int16_t>(kX0 + static_cast<int64_t>(r.pos - r.first) * w / r.ticks);
    oled_.gfx.draw_vline(fb(), px, kRollY0, kRollY1, 15);
  }
  if (r.editing && edit_.ready && edit_.bracket >= r.first && edit_.bracket < r.first + r.ticks) {  // dotted bracket
    const int16_t bx = static_cast<int16_t>(kX0 + static_cast<int64_t>(edit_.bracket - r.first) * w / r.ticks);
    for (int16_t y = kRollY0; y <= kRollY1; y += 2) oled_.gfx.draw_pixel(fb(), bx, y, 10);
  }
}

inline auto OledScreen::position(const ContentEngine::status::Position* p, int16_t xr, uint8_t barV, uint8_t v, bool sign) -> void {
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

inline auto OledScreen::drawStatus(ContentEngine::EngineRoot& e, const ContentEngine::EngineClock& clock, uint8_t sel, const SlotInfo& info, uint32_t lastTick, uint32_t nowMs) -> void {
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
    if (edit_.ready && edit_.encoder == 1) hline(x0, xr, kLabY + 6, 15);  // the encoder moves the start
  } else {
    // The transport icon, left of the counter.
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
    // The progress bar, the column's full width: the stored loop always, else the
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
  rlabel("BARS", static_cast<int16_t>(x0 + kBarsW - 1), kLabY, 5);  // right-aligned over its number
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
      if (ContentEngine::isMuted(edit_.noteVelocity))  // a muted note
        snprintf(cells[1].value, sizeof cells[1].value, "M");
      else
        snprintf(cells[1].value, sizeof cells[1].value, "%u", edit_.noteVelocity);
      snprintf(cells[2].value, sizeof cells[2].value, "%lu", static_cast<unsigned long>(edit_.noteLength));
    }
    const int16_t end = static_cast<int16_t>(x0 + 18 + 32 + 24 + 2 * kGap - 1);
    oled_.gfx.draw_rect_filled(fb(), static_cast<int16_t>(x0 - 1), kLabY - 1, static_cast<int16_t>(end + 1),
                               kLabY + 5, 11);
    // The cell F2 / F3 or F4 edits pulses: LENGTH in length mode, VELOCITY while NOTELEN is held.
    // Its square spans from the divider on its left to the next divider (VELOCITY) or the bar's end (LENGTH), so no
    // column of the steady bar is left beside it; its darkest level keeps the label readable.
    if (edit_.ready && (edit_.length || edit_.velocity)) {
      const int16_t cx = static_cast<int16_t>(edit_.velocity ? x0 + 18 + kGap : x0 + 18 + 32 + 2 * kGap);
      const int16_t right = static_cast<int16_t>(edit_.velocity ? cx + 32 + kGap / 2 - 1 : end + 1);
      oled_.gfx.draw_rect_filled(fb(), static_cast<int16_t>(cx - kGap / 2), kLabY - 1, right, kLabY + 5,
                                 pulseAt(nowMs));
    }
  } else if (loopEdit) {  // LOOP_EDIT: the shown loop's start, length (bars) and the tempo
    cells[0] = Cell{"START", "-", 24};
    cells[1] = Cell{"LENGTH", "-", 32};
    cells[2] = Cell{"BPM", "", 24};
    if (info.loop != 0) {
      snprintf(cells[0].value, sizeof cells[0].value, "%lu.%lu",
               static_cast<unsigned long>(edit_.loopStart / kBar + 1),
               static_cast<unsigned long>(edit_.loopStart % kBar / (kBar / 16) + 1));
      lengthText(edit_.loopTicks != 0 ? edit_.loopTicks : info.loop, cells[1].value, sizeof cells[1].value);
    }
    // The tempo with its tenth (F3 sets tenths), drawn after the cells with the thin dot.
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
    static constexpr int8_t kEncoderCell[3] = {0, 3, 2};  // NOTE pitch, VELOCITY, LENGTH
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
    steps = edit_.steps;  // SlotUndoHistory holds the session as one state: the session counts its steps
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
  rtenths(st::tempoTenths(clock.usPerTick(), kQuarter), kX1, ext ? 12 : 9);  // a thin dot before the tenth
  // Save dots over the tempo while saving, or RO before saving is approved (the tempo's label
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

}  // namespace Teensy

#endif  // ENGINE_FIRMWARE
