//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Banded piano-roll drawing from tiles (fixed-memory-engine E9.6, plan § 9.3 "DisplayManager: banded draw / DMA").
//
// The note layer only — grid, playhead, bracket and sidebar stay `DisplayManager`'s. Draws into the SSD1322 4-bit
// framebuffer (two pixels a byte, even x in the high nibble), one band of rows at a time, so a frame can be drawn and
// sent in bands. It reads tiles and the live overlay only: nothing on the draw path composes, allocates or reads pages.
// The legacy geometry: x = x0 + map(position in the view, 0, view ticks, 0, width), y = map(pitch, low, high, y1, y0),
// one-row note bars at brightness 7, the highlighted note at 10; a muted note at 3, 6 highlighted. A note's first pixel
// is brighter (13, 15 highlighted, 6 / 9 muted; owner 2026-11-08), so notes that touch on a pitch read as separate.
// * Detailed view (`drawRollBand`): per visible bar its draw source — span segments; a dense bar's 32-column raster;
//   else (no span tile yet) its density tile as a dim bar-wide line per sounding pitch; never blank once swept — then
//   the live overlay's notes (a take being recorded) on top, at the view's scale.
// * `pitchExtent`: the lowest and highest pitch sounding in a bar range, from density tiles (the auto-fit range).

#pragma once

#include <cstdint>

#include "ContentEngine/Content/PassEvent.h"

namespace ContentEngine {

struct RollView {
  uint8_t slot;
  uint32_t loop;       // the slot's loop length
  uint32_t barTicks;
  uint32_t first;      // loop position at x0
  uint32_t ticks;      // ticks the view spans (it may cross the loop end)
  uint8_t lowPitch, highPitch;
  int16_t x0, x1, y0, y1;  // the note area, inclusive
  uint32_t highlightTick = 0xFFFFFFFFu;  // a note starting here on `highlightPitch` draws highlighted
  uint32_t highlightLength = 0;          // with it, its segments in later bars (0: the start segment only)
  uint8_t highlightPitch = 0;
};

constexpr uint8_t kRollNoteBrightness = 7;
constexpr uint8_t kRollHighlightBrightness = 10;
constexpr uint8_t kRollDensityBrightness = 3;
constexpr uint8_t kRollMutedBrightness = 3;           // a muted note (`kVelocityMuted`; owner 2026-11-08)
constexpr uint8_t kRollMutedHighlightBrightness = 6;  // a muted note selected
// A note's first pixel, brighter than its bar (owner 2026-11-08: notes touching on a pitch read as one).
constexpr uint8_t kRollNoteStartBrightness = 13;
constexpr uint8_t kRollHighlightStartBrightness = 15;
constexpr uint8_t kRollMutedStartBrightness = 6;
constexpr uint8_t kRollMutedHighlightStartBrightness = 9;

namespace roll {

/// Arduino `map` (integer).
inline int32_t map(int64_t x, int64_t inMin, int64_t inMax, int64_t outMin, int64_t outMax) {
  if (inMax == inMin) return static_cast<int32_t>((outMin + outMax) / 2);
  return static_cast<int32_t>((x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin);
}
inline void pixel(uint8_t* fb, uint16_t width, int16_t x, int16_t y, uint8_t v) {
  uint8_t& b = fb[(static_cast<uint32_t>(y) * width + static_cast<uint32_t>(x)) / 2];
  if ((static_cast<uint32_t>(y) * width + static_cast<uint32_t>(x)) % 2 == 1)
    b = static_cast<uint8_t>((b & 0xF0) | (v & 0x0F));
  else
    b = static_cast<uint8_t>((b & 0x0F) | (v << 4));
}
/// Whether a segment at loop position `start` belongs to the highlighted note: its start, or a later bar's part of it.
inline bool highlighted(const RollView& v, uint32_t start, uint8_t pitch) {
  if (pitch != v.highlightPitch || v.highlightTick == 0xFFFFFFFFu) return false;
  const uint32_t rel = (start + v.loop - v.highlightTick % v.loop) % v.loop;
  return rel == 0 || rel < v.highlightLength;
}
inline int16_t rowOf(const RollView& v, uint8_t pitch) {
  int32_t y = map(pitch, v.lowPitch, v.highPitch, v.y1, v.y0);
  if (y < v.y0) y = v.y0;
  if (y > v.y1) y = v.y1;
  return static_cast<int16_t>(y);
}
/// Draws [start, start + length) (loop positions) of the pitch's row, clipped to the view and to the band.
inline void span(const RollView& v, uint8_t* fb, uint16_t width, int16_t rowFrom, int16_t rowTo, uint32_t start,
                 uint32_t length, uint8_t pitch, uint8_t value) {
  if (pitch < v.lowPitch || pitch > v.highPitch || v.loop == 0 || v.ticks == 0) return;
  const int16_t y = rowOf(v, pitch);
  if (y < rowFrom || y > rowTo) return;
  const uint32_t rel = (start + v.loop - v.first % v.loop) % v.loop;  // ticks from the view's left edge
  if (rel >= v.ticks) return;
  const uint32_t end = rel + length > v.ticks ? v.ticks : rel + length;
  const int32_t w = v.x1 - v.x0;
  const int16_t xa = static_cast<int16_t>(v.x0 + map(rel, 0, v.ticks, 0, w));
  int16_t xb = static_cast<int16_t>(v.x0 + map(end, 0, v.ticks, 0, w));
  if (xb < xa) xb = xa;
  for (int16_t x = xa; x <= xb && x <= v.x1; ++x) pixel(fb, width, x, y, value);
}

}  // namespace roll

/// Rows [rowFrom, rowTo] of the detailed note layer. O(segments of the visible bars + overlay cells in the band).
template <typename Store, typename Overlay>
void drawRollBand(const Store& store, const Overlay* overlay, const RollView& v, uint8_t* fb, uint16_t width,
                  int16_t rowFrom, int16_t rowTo) {
  if (rowFrom < v.y0) rowFrom = v.y0;
  if (rowTo > v.y1) rowTo = v.y1;
  if (rowFrom > rowTo || v.loop == 0 || v.barTicks == 0) return;
  const uint32_t bars = (v.loop + v.barTicks - 1) / v.barTicks;
  const uint32_t viewBars = (v.ticks + v.barTicks - 1) / v.barTicks + 1;
  const uint32_t firstBar = (v.first % v.loop) / v.barTicks;
  for (uint32_t k = 0; k <= viewBars && k < bars + 1; ++k) {
    const uint8_t bar = static_cast<uint8_t>((firstBar + k) % bars);
    if (k == bars) break;  // every bar once
    const uint32_t base = bar * v.barTicks;
    const auto d = store.drawSource(v.slot, bar);
    using Kind = typename Store::Kind;
    if (d.kind == Kind::Span) {
      for (uint16_t i = 0; i < d.header->count; ++i) {
        const auto& s = d.span->segments[i];
        const bool hi = roll::highlighted(v, base + s.start, s.pitch);
        const bool muted = isMuted(s.velocity);
        roll::span(v, fb, width, rowFrom, rowTo, base + s.start, s.length, s.pitch,
                   muted ? (hi ? kRollMutedHighlightBrightness : kRollMutedBrightness)
                         : (hi ? kRollHighlightBrightness : kRollNoteBrightness));
      }
    } else if (d.kind == Kind::Dense) {
      const uint32_t col = v.barTicks / 32 == 0 ? 1 : v.barTicks / 32;
      for (uint8_t c = 0; c < 32; ++c)
        for (uint8_t p = v.lowPitch; p <= v.highPitch && p < 128; ++p)
          if (d.span->raster[c][p / 8] & (1u << (p % 8)))
            roll::span(v, fb, width, rowFrom, rowTo, base + c * col, col, p, kRollNoteBrightness);
    } else if (d.kind == Kind::Density) {
      for (uint8_t p = v.lowPitch; p <= v.highPitch && p < 128; ++p) {
        const uint8_t n = (d.density->counts[p / 2] >> ((p & 1) * 4)) & 0x0F;
        if (n != 0) roll::span(v, fb, width, rowFrom, rowTo, base, v.barTicks, p, kRollDensityBrightness);
      }
    }
  }
  // Note starts, after every bar so a touching neighbour's last pixel cannot cover one; a segment continued from the
  // previous bar (flags bit 4) has none.
  for (uint32_t k = 0; k <= viewBars && k < bars; ++k) {
    const uint8_t bar = static_cast<uint8_t>((firstBar + k) % bars);
    const auto d = store.drawSource(v.slot, bar);
    if (d.kind != Store::Kind::Span) continue;
    for (uint16_t i = 0; i < d.header->count; ++i) {
      const auto& s = d.span->segments[i];
      if (s.flags & 0x10) continue;
      const uint32_t at = bar * v.barTicks + s.start;
      const bool hi = roll::highlighted(v, at, s.pitch);
      roll::span(v, fb, width, rowFrom, rowTo, at, 0, s.pitch,
                 isMuted(s.velocity) ? (hi ? kRollMutedHighlightStartBrightness : kRollMutedStartBrightness)
                                     : (hi ? kRollHighlightStartBrightness : kRollNoteStartBrightness));
    }
  }
  if (overlay == nullptr) return;
  // The live take's notes, at this view's scale (a held note reaches the current position; one crossing the loop end
  // wraps at the overlay's loop, or a first take's pickup at the take's length so far).
  const uint32_t oLoop = overlay->wrapLength();
  for (uint16_t i = 0; i < overlay->count(); ++i) {
    const auto n = overlay->note(i);
    if (n.end >= n.start) {
      roll::span(v, fb, width, rowFrom, rowTo, n.start, n.end - n.start + 1, n.pitch, n.value);
    } else if (oLoop != 0) {
      roll::span(v, fb, width, rowFrom, rowTo, n.start, oLoop - n.start, n.pitch, n.value);
      roll::span(v, fb, width, rowFrom, rowTo, 0, n.end + 1, n.pitch, n.value);
    }
  }
}

/// The lowest and highest pitch sounding in bars [firstBar, lastBar] of `slot`, from density tiles. False: none.
template <typename Store>
bool pitchExtent(const Store& store, uint8_t slot, uint8_t firstBar, uint8_t lastBar, uint8_t& lo, uint8_t& hi) {
  bool any = false;
  lo = 127;
  hi = 0;
  for (uint32_t bar = firstBar; bar <= lastBar && bar < 64; ++bar) {
    const auto* t = store.densityTile(slot, static_cast<uint8_t>(bar));
    if (t == nullptr) continue;
    for (uint8_t p = 0; p < 128; ++p)
      if ((t->counts[p / 2] >> ((p & 1) * 4)) & 0x0F) {
        any = true;
        if (p < lo) lo = p;
        if (p > hi) hi = p;
      }
  }
  return any;
}

}  // namespace ContentEngine
