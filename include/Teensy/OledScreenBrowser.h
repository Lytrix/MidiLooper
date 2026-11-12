//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// `OledScreen`'s sets browser, get-a-loop picker and save windows: the member function bodies, declared (and documented) in `OledScreen.h`, which includes this
// file after the class.

#pragma once

#if defined(ENGINE_FIRMWARE)

#include "Teensy/OledScreen.h"

namespace Teensy {

inline auto OledScreen::idText(const Record& r, char* out, size_t n) -> void {
  snprintf(out, n, "%c%02u", ContentEngine::library::stageLetter(r.stage), r.stageNumber);
}

inline auto OledScreen::window(int16_t x0, int16_t y0, int16_t x1, int16_t y1, const char* title) -> void {
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

inline auto OledScreen::hintLabel(const char* s, int16_t x, int16_t top) -> int16_t {
  bool button = false;
  for (; *s; ++s) {
    if (*s == '[') button = true;
    const char c[2] = {*s, 0};
    x = label(c, x, top, button ? kButtonV : kHintV);
    if (*s == ']') button = false;
  }
  return x;
}

inline auto OledScreen::hint(const char* s) -> void {
  oled_.gfx.draw_rect_filled(fb(), 0, 57, 255, 63, 0);
  hintLabel(s, 2, kHintY);
}

inline auto OledScreen::drawTrashCorner(bool undo) -> void {
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

inline auto OledScreen::recordOf(const Row& r) -> uint16_t {
  return r.kind == RowKind::Milestone || r.kind == RowKind::Set || r.kind == RowKind::Working ||
                 r.kind == RowKind::Item
             ? r.record
             : SetBrowser::kNone;
}

inline auto OledScreen::when(const Record& m) const -> ContentEngine::wallclock::Local {
  return ContentEngine::wallclock::local(m.wallTime, browserInfo_.tz);
}

inline auto OledScreen::drawBrowser(const SetBrowser& b, ContentEngine::EngineRoot& e) -> void {
  engine_ = &e;
  if (b.view() == SetBrowser::View::Popup) {
    drawSavePopup(b);
    return;
  }
  if (b.picking() || (b.view() == SetBrowser::View::Failed && b.listView() == SetBrowser::View::Picker)) {
    drawPicker(b, e);
    return;
  }
  const bool confirming = b.view() == SetBrowser::View::Confirm;
  char title[24];
  if (b.shownView() == SetBrowser::View::Set) snprintf(title, sizeof title, "SET %u", b.openSetNumber());
  else if (b.shownView() == SetBrowser::View::Stages) snprintf(title, sizeof title, "GET A LOOP  SET %u", b.openSetNumber());
  else if (b.shownView() == SetBrowser::View::Bin) snprintf(title, sizeof title, "TRASH");
  else snprintf(title, sizeof title, "SETS");
  window(kWX0, kWY0, kWX1, kWY1, title);
  for (int16_t y = kWY0 + 10; y < kWY1; ++y) px(kDiv, y, kRuleLevel);
  drawSpine(b);
  const uint16_t n = b.rowCount();
  for (uint16_t i = b.first(); i < n && i < b.first() + SetBrowser::kVisible; ++i) drawRow(b, i);
  char h[48];
  if (b.view() == SetBrowser::View::Failed) {
    label(b.failWhat(), kDetailX, 14, 15);
    label(b.failWhy(), kDetailX, 24, 9);
    label(b.failChanged() ? "SET NOT AS IT WAS" : "NOTHING CHANGED", kDetailX, 32, 9);
    snprintf(h, sizeof h, "[TRK] TRY AGAIN   HOLD EXIT");  // REC / EDIT and the trash do nothing here
  } else {
    drawDetail(b, b.row(b.selected()), h, sizeof h);
  }
  if (b.view() == SetBrowser::View::Failed) {
    hint(h);
  } else {  // one line — the moves, the actions, and the trash on the right
    char line[64];
    snprintf(line, sizeof line, "%s%s", kMoves, h);
    hint(line);
    drawTrashCorner(b.lastTrash() != SetBrowser::kNone);
  }
  if (confirming) drawConfirm(b);
}

inline auto OledScreen::drawConfirm(const SetBrowser& b) -> void {
  constexpr int16_t x0 = 58, y0 = 4, x1 = 197, y1 = 56, kButtonY = 46;
  if (b.confirmKind() == SetBrowser::Confirm::EmptyLoop) {  // an empty loop over a filled slot, centred
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
  const bool empty = b.confirmKind() == SetBrowser::Confirm::Empty;
  window(x0, y0, x1, y1, empty ? "EMPTY TRASH" : "TRASH");
  char q[32], line[32], stamp[24];
  uint16_t rec = SetBrowser::kNone;
  if (empty) {
    uint16_t stages = 0;
    for (uint16_t k = 0; k < b.itemCount(); ++k) stages = static_cast<uint16_t>(stages + b.itemSize(b.item(k)));
    snprintf(q, sizeof q, "EMPTY THE TRASH?");
    snprintf(line, sizeof line, "%u ITEM%s, %u STAGE%s", b.itemCount(), b.itemCount() == 1 ? "" : "S", stages,
             stages == 1 ? "" : "S");
  } else if (b.confirmRecord() != SetBrowser::kNone) {
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
  } else if (rec != SetBrowser::kNone) {
    ContentEngine::wallclock::formatStamp(b.record(rec).wallTime, browserInfo_.tz, stamp, sizeof stamp);
    snprintf(line, sizeof line, b.confirmRecord() != SetBrowser::kNone ? "SAVED %s" : "LATEST %s", stamp);
    label(line, static_cast<int16_t>(x0 + 6), static_cast<int16_t>(y0 + 30), 9);
  }
  drawButtons(b, x0, x1, kButtonY);
}

inline auto OledScreen::drawButtons(const SetBrowser& b, int16_t x0, int16_t x1, int16_t buttonY) -> void {
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

inline auto OledScreen::clabel(const char* s, int16_t xc, int16_t top, uint8_t v) -> void {
  label(s, static_cast<int16_t>(xc - (static_cast<int16_t>(strlen(s)) * 4 - 1) / 2), top, v);
}

inline auto OledScreen::drawPicker(const SetBrowser& b, ContentEngine::EngineRoot& e) -> void {
  char title[32], a[32], c[24];
  const uint16_t rec = b.pickRecord();
  if (rec != SetBrowser::kNone) {
    char id[8];
    idText(b.record(rec), id, sizeof id);
    snprintf(title, sizeof title, "LOOPS  SET %u  %s", b.record(rec).set, id);
  } else {
    snprintf(title, sizeof title, "EMPTY LOOPS");
  }
  window(kWX0, kWY0, kWX1, kWY1, title);
  for (int16_t y = kWY0 + 10; y < kWY1; ++y) px(kDiv, y, kRuleLevel);
  const uint8_t preset = b.pickPreset();
  for (uint8_t i = 0; i < SetBrowser::kPresets; ++i) {  // the empty-loop presets: dotted, unlike a loop
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
    snprintf(a, sizeof a, "%u", SetBrowser::kPresetBars[i]);
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
  if (preset == SetBrowser::kNoPreset) {  // the cursor: a hollow bright box
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
    snprintf(a, sizeof a, "EMPTY %u", SetBrowser::kPresetBars[preset]);
  }
  rlabel(a, 133, kGridY, 13);
  const uint8_t d = b.pickDestination();
  snprintf(a, sizeof a, "INTO %u-%u", d / 8 + 1, d % 8 + 1);
  label(a, kPanelX0, kGridY, 11);
  // The panel: an empty loop, the loop's roll, or what the read is doing; the audition beneath.
  const auto au = b.audition();
  bool use = au == SetBrowser::Audition::Sounding || au == SetBrowser::Audition::Silent;
  if (preset != SetBrowser::kNoPreset) {
    panelBox();
    clabel("EMPTY LOOP", (kPanelX0 + kPanelX1) / 2, 26, 15);
    snprintf(a, sizeof a, "%u BAR%s", SetBrowser::kPresetBars[preset], SetBrowser::kPresetBars[preset] == 1 ? "" : "S");
    clabel(a, (kPanelX0 + kPanelX1) / 2, 35, 9);
    label("AN EMPTY LOOP", kPanelX0, 49, 11);
    use = true;
  } else if (b.pickLevel(b.pickSource()) == 0) {
    panelBox();
    clabel("NO LOOP HERE", (kPanelX0 + kPanelX1) / 2, 30, 6);
  } else if (b.preview() == SetBrowser::Preview::Ready) {
    drawPreviewRoll(e, b.previewBars(), au == SetBrowser::Audition::Silent);
    snprintf(c, sizeof c, "%u BAR%s", b.previewBars(), b.previewBars() == 1 ? "" : "S");
    if (au == SetBrowser::Audition::Sounding) {
      label("AUDITION", kPanelX0, 49, 15);
      rlabel(c, kPanelX1, 49, 9);
    } else if (au == SetBrowser::Audition::Silent) {
      label("PREVIEW ONLY", kPanelX0, 49, 11);
      rlabel(silentText(b.silentWhy(), a, sizeof a), kPanelX1, 49, 6);
    } else {
      label(c, kPanelX0, 49, 11);
    }
  } else {
    panelBox();
    const auto pv = b.preview();
    clabel(pv == SetBrowser::Preview::Failed   ? "READ FAILED"
           : pv == SetBrowser::Preview::TooBig ? "TOO BIG TO PREVIEW"
                                            : "READING...",
           (kPanelX0 + kPanelX1) / 2, 30, 7);
    label("? BARS", kPanelX0, 49, 6);
  }
  char h[64];
  if (b.view() == SetBrowser::View::Failed) {
    fillRect(kPanelX0 - 1, kPanelY0 - 1, kPanelX1 + 1, 55, 0);
    label(b.failWhat(), kPanelX0, 22, 15);
    label(b.failWhy(), kPanelX0, 30, 9);
    label("NOTHING CHANGED", kPanelX0, 38, 9);
    hint("[TRK] TRY AGAIN   HOLD EXIT");
    return;
  }
  snprintf(h, sizeof h, "[LEN]{ [PLY]} [REC]< [EDT]>  [TRK] %s  HOLD EXIT", use ? "USE" : "AUDITION");
  hint(h);
  if (b.view() == SetBrowser::View::Confirm) drawConfirm(b);
}

inline auto OledScreen::panelBox() -> void {
  hline(kPanelX0 - 1, kPanelX1 + 1, kPanelY0 - 1, 4);
  hline(kPanelX0 - 1, kPanelX1 + 1, kPanelY1 + 1, 4);
  for (int16_t y = kPanelY0 - 1; y <= kPanelY1 + 1; ++y) {
    px(kPanelX0 - 1, y, 4);
    px(kPanelX1 + 1, y, 4);
  }
}

inline auto OledScreen::silentText(uint8_t why, char* buf, size_t n) -> const char* {
  if (why >= 4) {
    snprintf(buf, n, "SOLO ON %u", why - 3);
    return buf;
  }
  return why == 1 ? "STOPPED" : why == 2 ? "NOT PLAYING" : why == 3 ? "MUTED" : "";
}

inline auto OledScreen::drawPreviewRoll(ContentEngine::EngineRoot& e, uint16_t bars, bool dim) -> void {
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

inline auto OledScreen::drawSpine(const SetBrowser& b) -> void {
  const uint16_t n = b.rowCount(), last = static_cast<uint16_t>(b.first() + SetBrowser::kVisible);
  int16_t top = -1;
  ContentEngine::wallclock::Local prev{};
  bool havePrev = false;
  for (uint16_t i = b.first(); i < n && i < last; ++i) {
    const uint16_t rec = recordOf(b.row(i));
    const int16_t y = rowY(b, i);
    if (rec == SetBrowser::kNone || !ContentEngine::wallclock::valid(b.record(rec).wallTime)) continue;
    const auto l = when(b.record(rec));
    if (top < 0) {
      top = i == b.first() && i > 0 && recordOf(b.row(static_cast<uint16_t>(i - 1))) != SetBrowser::kNone
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

inline auto OledScreen::drawRow(const SetBrowser& b, uint16_t i) -> void {
  const Row r = b.row(i);
  const int16_t y = rowY(b, i);
  const bool on = i == b.selected();
  if (r.kind == RowKind::Divider) {  // what the list below is for: sets, or a set's stages
    const SetBrowser::View list = b.shownView();
    const char* what = list == SetBrowser::View::Bin      ? "RESTORE SET/STAGE"
                       : list == SetBrowser::View::Set    ? "LOAD STAGE"
                       : list == SetBrowser::View::Stages ? "FROM STAGE"
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
    case RowKind::Trash: {  // the second way in, its items on the right as a set's time is
      label("TRASH", kYearX, y, on ? 15 : 11);
      char n[8];
      snprintf(n, sizeof n, "%u", b.itemCount());
      if (b.itemCount() != 0) rlabel(n, kNoteXR, y, on ? 15 : kDimText);
      return;
    }
    case RowKind::Save: label("SAVE SET", kYearX, y, on ? 15 : 11); return;
    case RowKind::Back:
      label(b.shownView() == SetBrowser::View::Stages ? "< BACK" : "< SETS", kYearX, y, on ? 15 : 11);
      return;
    case RowKind::GetLoop: label("GET A LOOP", kYearX, y, on ? 15 : 11); return;
    case RowKind::TrashSet: label("MOVE SET TO TRASH", kYearX, y, on ? 15 : 11); return;
    default: break;
  }
  const uint16_t rec = recordOf(r);
  const bool dated = rec != SetBrowser::kNone && ContentEngine::wallclock::valid(b.record(rec).wallTime);
  if (dated) {
    const auto l = when(b.record(rec));
    // Year and month on the top row (sticky) and where a month starts; the day once per day.
    bool newMonth = true, newDay = true;
    for (uint16_t j = i; j > b.first();) {  // the nearest dated row above it on screen (one without a time is passed)
      const uint16_t above = recordOf(b.row(--j));
      if (above == SetBrowser::kNone) break;
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
      rlabel(buf, kDayXR, y, on ? 15 : 9);  // right-aligned: 9 over 10
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
    if (m != SetBrowser::kNone) idText(b.record(m), id, sizeof id);
    snprintf(text, sizeof text, "SET %u %s", r.set, id);
  } else {
    snprintf(text, sizeof text, "SET %u", r.set);
  }
  if (rec != SetBrowser::kNone) {  // the time in one column for sets (their newest stage) and stages
    ContentEngine::wallclock::formatClock(b.record(rec).wallTime, browserInfo_.tz, note, sizeof note);
    if (strcmp(note, "--:--") == 0) note[0] = 0;
  }
  label(text, kIdX, y, v);
  if (note[0] != 0) rlabel(note, kNoteXR, y, on ? 15 : kDimText);
}

inline auto OledScreen::drawMilestone(const Record& m, const char* line2, const char* line3) -> void {
  char id[8];
  idText(m, id, sizeof id);
  label(id, kDetailX, kMosaicY, 15, 3);
  label(ContentEngine::library::stageName(m.stage), kDetailX, 34, 13);
  if (line2 != nullptr) label(line2, kDetailX, 42, 10);
  if (line3 != nullptr) label(line3, kDetailX, 50, kDimText);
  drawMosaic(m);
}

inline auto OledScreen::drawDetail(const SetBrowser& b, const Row& r, char* h, size_t n) -> void {
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
      if (base != SetBrowser::kNone) {
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
      if (b.shownView() == SetBrowser::View::Stages) {
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
    case RowKind::GetLoop: {  // into the selected slot, boxed in the working set's mosaic
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
    case RowKind::TrashSet:  // destructive, so a double press; a triple brings it back
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
    case RowKind::Empty: {
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
      if (newest != SetBrowser::kNone) drawMilestone(b.record(newest), a, c);
      if (t.parent == 0) {
        snprintf(h, n, "[TRK] RESTORE SET %u", r.set);
      } else {
        char id[8] = "";
        if (newest != SetBrowser::kNone) idText(b.record(newest), id, sizeof id);
        snprintf(h, n, "[TRK] RESTORE STAGE %s", id);
      }
      return;
    }
    case RowKind::Working:
    case RowKind::Set:
      snprintf(h, n, "[TRK] OPEN");
      if (r.record == SetBrowser::kNone) {
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
      const uint16_t parent = m.parent != 0 ? b.find(m.parent) : SetBrowser::kNone;
      char id[8];
      if (parent != SetBrowser::kNone) {
        idText(b.record(parent), id, sizeof id);
        snprintf(a, sizeof a, "FROM %s", id);
      }
      drawMilestone(m, parent != SetBrowser::kNone ? a : nullptr, nullptr);
      if (b.shownView() == SetBrowser::View::Stages) snprintf(h, n, "[TRK] SELECT STAGE");
      else snprintf(h, n, "[TRK] LOAD  2* [TRK] TRASH");
      return;
    }
  }
}

inline auto OledScreen::drawMosaic(const Record& m) -> void {
  uint8_t lv[64];
  for (uint8_t k = 0; k < 64; ++k) lv[k] = static_cast<uint8_t>((m.mosaic[k / 4] >> ((k % 4) * 2)) & 3);
  drawCells(lv);
}

inline auto OledScreen::drawLiveMosaic() -> void {
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

inline auto OledScreen::drawCells(const uint8_t* lv) -> void {
  static const uint8_t kLevel[4] = {0, 7, 11, 15};
  for (uint8_t t = 0; t < 8; ++t)
    for (uint8_t s = 0; s < 8; ++s) {
      const uint8_t l = lv[t * 8 + s];
      const int16_t x = static_cast<int16_t>(kMosaicX + s * 4), y = static_cast<int16_t>(kMosaicY + t * 4);
      if (l == 0) px(static_cast<int16_t>(x + 1), static_cast<int16_t>(y + 1), 3);
      else oled_.gfx.draw_rect_filled(fb(), x, y, static_cast<int16_t>(x + 2), static_cast<int16_t>(y + 2), kLevel[l & 3]);
    }
}

inline auto OledScreen::drawSavePopup(const SetBrowser& b) -> void {
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
  const uint16_t from = base != SetBrowser::kNone && base > 1 ? static_cast<uint16_t>(base - 1) : 0;
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
  for (uint8_t k = 0; k < SetBrowser::kStages; ++k) {
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

inline auto OledScreen::drawToast() -> void {
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

}  // namespace Teensy

#endif  // ENGINE_FIRMWARE
