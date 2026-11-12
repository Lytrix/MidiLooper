//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// `ControlSurface`'s LEDs (the wanted state, the stored-loop scan, LOOP_EDIT's bar and step LEDs): the member function bodies, declared (and documented) in `ControlSurface.h`, which
// includes this file after the class.

#pragma once

#include "ContentEngine/ControlSurface/ControlSurface.h"

namespace ContentEngine {

template <typename Engine>
inline auto ControlSurface<Engine>::beyondScan(ManifestId m, uint32_t budget) -> bool {
  const auto& d = e_->manifests().manifest(m);
  if (d.identity == beyondIdentity_) return true;
  Scan& w = scan_;
  if (w.phase == 0 || w.identity != d.identity) {  // a new state: start over
    w = Scan{};
    w.identity = d.identity;
    w.phase = 1;
    for (uint8_t i = 0; i < d.runCount; ++i) w.fit |= e_->runs().run(d.runs[i]).kind == RunKind::kFit;
    w.played = playedFrom(d);
    w.loop = d.loopLengthTicks;
  }
  const uint32_t endBar = w.loop / kBar;
  while (w.phase != 0) {
    if (w.run >= d.runCount) {
      if (w.phase == 1) {  // the stored loop — the window's frame, at least the stored notes' reach in whole bars
        const LoopWindow lw = windowOf(e_->runs(), e_->pool(), d.runs, d.runCount, w.loop);
        const uint32_t stored = (w.storedEnd + kBar - 1) / kBar * kBar;
        w.frame = lw.frame > stored ? lw.frame : stored;
        if (w.frame == 0) w.frame = kBar;
        w.frameStart = lw.start % w.frame;
        w.phase = 2;
        w.run = 0;
        w.row = 0;
        continue;
      }
      beyondIdentity_ = w.identity;  // done: the new values at once
      beyondBars_ = w.beyondBars;
      beyondSteps_ = w.beyondSteps;
      storedEnd_ = w.storedEnd;
      frame_ = w.frame;
      frameStart_ = w.frameStart;
      absBars_ = w.absBars;
      absEnd_ = w.absEnd > w.frame ? w.frame : w.absEnd;
      for (uint8_t k = 0; k < 64; ++k) barSteps_[k] = w.barSteps[k];
      ++rowScans_;
      w.phase = 0;
      return true;
    }
    const auto& run = e_->runs().run(d.runs[w.run]);
    if (run.kind == RunKind::kFit || w.run < w.played) {  // a groove pass's sources are not played
      ++w.run;
      w.row = 0;
      continue;
    }
    for (; w.row < run.rowCount; ++w.row) {
      if (budget == 0) return false;
      --budget;
      const PassEvent& e = e_->runs().rowAt(d.runs[w.run], w.row, e_->pool());
      if ((e.status & 0xF0) != 0x90 || e.data2 == 0) continue;
      if (w.phase == 1) {
        if (e.tick + 1 > w.storedEnd) w.storedEnd = e.tick + 1;  // the stored notes' reach (a NoteOff may wrap)
        if (!w.fit || run.kind == RunKind::kEdit || e.tick < w.loop) continue;
        const uint32_t bb = e.tick / kBar;
        if (bb < 64) w.beyondBars |= 1ull << bb;
        if (bb == endBar) w.beyondSteps = static_cast<uint16_t>(w.beyondSteps | (1u << ((e.tick % kBar) / kSixteenth)));
      } else {
        const uint32_t at = (e.tick + w.frameStart) % w.frame, bar = at / kBar;
        if (bar < 64) {
          w.absBars |= 1ull << bar;
          w.barSteps[bar] = static_cast<uint16_t>(w.barSteps[bar] | (1u << ((at % kBar) / kSixteenth)));
        }
        if ((bar + 1) * kBar > w.absEnd) w.absEnd = (bar + 1) * kBar;
      }
    }
    ++w.run;
    w.row = 0;
  }
  return true;
}

template <typename Engine>
template <typename D>
inline auto ControlSurface<Engine>::playedFrom(const D& d) -> uint8_t {
  uint8_t p = 0;
  for (uint8_t i = 0; i < d.runCount; ++i)
    if (e_->runs().run(d.runs[i]).kind == RunKind::kGroove) p = i;
  return p;
}

template <typename Engine>
inline auto ControlSurface<Engine>::absSteps(ManifestId m, uint32_t bar) -> uint16_t {
  const auto& d = e_->manifests().manifest(m);
  if (bar < 64) return barSteps_[bar];  // built with the scan (`loopEditLeds` advanced it)
  if (d.identity == stepsIdentity_ && bar == stepsBar_) return steps_;
  stepsIdentity_ = d.identity;
  stepsBar_ = bar;
  ++rowScans_;
  steps_ = 0;
  const uint8_t played = playedFrom(d);
  for (uint8_t i = 0; i < d.runCount; ++i) {
    const auto& run = e_->runs().run(d.runs[i]);
    if (run.kind == RunKind::kFit || i < played) continue;  // a groove pass's sources are not played
    for (uint32_t r = 0; r < run.rowCount; ++r) {
      const PassEvent& e = e_->runs().rowAt(d.runs[i], r, e_->pool());
      if ((e.status & 0xF0) != 0x90 || e.data2 == 0) continue;
      const uint32_t a = (e.tick + frameStart_) % frame_;
      if (a / kBar == bar) steps_ = static_cast<uint16_t>(steps_ | (1u << ((a % kBar) / kSixteenth)));
    }
  }
  return steps_;
}

template <typename Engine>
inline auto ControlSurface<Engine>::loopEditLeds(ManifestId m, uint32_t loop, uint32_t pos, bool moving, uint8_t* want) -> void {
  beyondScan(m, kScanRows);  // the LEDs: in steps
  const uint32_t frame = frame_, start = frameStart_;
  const uint32_t at = (start + (moving ? pos % loop : 0)) % frame;
  const auto in = [&](uint32_t a) { return (a + frame - start) % frame < loop; };
  const uint32_t page = at / (8 * kBar) * 8, bar = at / kBar;
  for (uint8_t i = 0; i < 8; ++i) {
    const uint32_t b = page + i, a = b * kBar;
    if (a >= frame) {
      want[40 + i] = 0;
      continue;
    }
    const bool notes = b < 64 && ((absBars_ >> b) & 1u);
    const bool inside = in(a) || in(a + kBar - 1 < frame ? a + kBar - 1 : frame - 1);
    want[40 + i] = moving && b == bar ? 127 : inside ? (notes ? kLoopNotesLed : kLoopEmptyLed) : notes ? kBeyondLed : 0;
  }
  const uint16_t steps = absSteps(m, bar);
  for (uint8_t k = 0; k < 16; ++k) {
    const uint32_t a = bar * kBar + k * kSixteenth;
    want[k] = a >= frame || !((steps >> k) & 1u) ? 0 : in(a) ? 64 : kBeyondLed;
    want[16 + k] = 0;
  }
  if (moving) want[16 + (at % kBar) / kSixteenth] = 127;
}

template <typename Engine>
inline auto ControlSurface<Engine>::heldStep() const -> uint8_t {
  for (uint8_t k = 0; k < 16; ++k)
    if (press_.pressed(static_cast<uint8_t>(kStep0 + k))) return k;
  return 0xFF;
}
template <typename Engine>
inline auto ControlSurface<Engine>::desired(uint32_t lastTick, uint32_t ms, uint8_t* want) -> void {
  auto& x = transport();
  auto& c = control();
  const uint8_t t = c.selectedTrack();
  for (uint8_t i = 0; i < kTracks; ++i) {
    bool content = false;
    for (uint8_t s = 0; s < kSlotsPerTrack && !content; ++s) content = !empty(static_cast<uint8_t>(i * kSlotsPerTrack + s));
    // In mute mode the track LEDs show what sounds — unmuted with content full, muted dim, empty dark
    want[kTrackBase + i] = trackMute_ ? (!content ? 0 : control().muted(i) ? 32 : 127)
                                      : (i == t ? 127 : content ? 32 : 0);
  }
  if (clock_->running()) want[kTransportNote] = 127;
  const bool blinkBright = (ms / 250) % 2 == 0;
  for (uint8_t s = 0; s < kSlotsPerTrack; ++s) {
    const uint8_t slot = static_cast<uint8_t>(t * kSlotsPerTrack + s);
    uint8_t v;
    if (x.writingSlot() == slot || c.queuedRecordSlot(t) == slot) {
      v = 96;
    } else if (c.queuedSwitchSlot(t) == slot) {
      v = blinkBright ? 96 : 64;
    } else if (s == slot_[t]) {
      v = 127;
    } else if (empty(slot)) {
      v = 0;
    } else if (x.playing(t) && x.playingSlot(t) == slot) {
      v = c.muted(t) ? 16 : 48;
    } else {
      v = 32;
    }
    want[kSlotBase + s] = v;
  }
  // The shown slot's bar, steps and position.
  uint8_t slot = selectedSlot(t);
  if (x.writingTrack() == t) slot = x.writingSlot();
  else if (x.playing(t)) slot = x.playingSlot(t);
  const ManifestId m = x.contentOf(slot);
  uint32_t loop = m == kNoManifest ? 0 : e_->manifests().manifest(m).loopLengthTicks;
  uint32_t pos = 0;
  bool moving = false;
  if (x.writingSlot() == slot) {
    pos = x.writePosition(lastTick);
    moving = true;
    if (loop == 0 || loop > 256 * kBar) loop = (pos / kBar + 1) * kBar;
  } else if (x.playing(t) && x.playingSlot(t) == slot && loop != 0) {
    pos = (lastTick - x.origin(t)) % loop;
    moving = true;
  }
  if (loop == 0) return;
  const uint32_t bars = (loop + kBar - 1) / kBar;
  if (edit_.editing() && edit_.slot() == slot) {  // NOTE_EDIT: the bracket's bar and step (the step buttons')
    pos = edit_.bracket() % loop;
    moving = true;
  }
  if (m != kNoManifest && x.writingSlot() != slot && !(edit_.editing() && edit_.slot() == slot)) {
    // The stored loop's bars, absolute (performance too)
    loopEditLeds(m, loop, pos, moving, want);
    return;
  }
  const uint32_t bar = pos / kBar;
  for (uint8_t i = 0; i < 8 && i < bars; ++i) {
    const auto* d = e_->tiles().densityTile(slot, i);
    bool notes = false;
    for (uint8_t k = 0; d != nullptr && k < 64 && !notes; ++k) notes = d->counts[k] != 0;
    want[40 + i] = moving && bar == i ? 127 : notes ? 64 : 32;
  }
  // Notes kept past the loop's end (a shortening): their bars, and the last bar's 16ths, dim.
  if (m != kNoManifest) {
    beyondScan(m, kScanRows);  // the LEDs: in steps
    for (uint8_t i = static_cast<uint8_t>(bars); i < 8; ++i)
      if ((beyondBars_ >> i) & 1u) want[40 + i] = kBeyondLed;
    if (bar == loop / kBar && loop % kBar != 0)
      for (uint8_t k = static_cast<uint8_t>((loop % kBar + kSixteenth - 1) / kSixteenth); k < 16; ++k)
        if ((beyondSteps_ >> k) & 1u) want[k] = kBeyondLed;
  }
  if (moving) want[16 + (pos % kBar) / kSixteenth] = 127;
  if (bar >= 64) return;
  const auto src = e_->tiles().drawSource(slot, static_cast<uint8_t>(bar));
  using Kind = typename Engine::Tiles::Kind;
  if (src.kind == Kind::Span) {
    for (uint16_t i = 0; i < src.header->count; ++i) want[(src.span->segments[i].start % kBar) / kSixteenth] = 64;
  } else if (src.kind == Kind::Dense) {
    for (uint8_t col = 0; col < 32; ++col)
      for (uint8_t k = 0; k < 16; ++k)
        if (src.span->raster[col][k] != 0) want[col / 2] = 64;
  }
}

}  // namespace ContentEngine
