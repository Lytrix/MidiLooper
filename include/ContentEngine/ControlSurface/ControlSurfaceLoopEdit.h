//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// `ControlSurface`'s LOOP_EDIT (start, length, ranges, the length popup, tempo) and the edit modes: the member function bodies, declared (and documented) in `ControlSurface.h`, which
// includes this file after the class.

#pragma once

#include "ContentEngine/ControlSurface/ControlSurface.h"

namespace ContentEngine {

template <typename Engine>
inline auto ControlSurface<Engine>::cycleEdit(uint8_t t) -> void {
  if (edit_.active()) {
    edit_.leave();
    enterLoopEdit();
    return;
  }
  loopEdit_ = false;
  enterEdit(t);
  if (!edit_.active()) enterLoopEdit();
}

template <typename Engine>
inline auto ControlSurface<Engine>::enterLoopEdit() -> void {
  loopEdit_ = true;
  rangeSteps_ = rangeBars_ = 0;  // no range from before
  rangeTapNote_ = 0xFF;
  visitState_ = 0;   // a new visit: its first change is a step
  loopPark_ = true;  // every fader to its value, at the next motor pass
  pendingLoop_ = 0;
  startPending_ = false;
  coarseBpm_ = static_cast<uint16_t>(tempoTenths() / 10);
}

template <typename Engine>
inline auto ControlSurface<Engine>::loopWrite(uint8_t fader, uint8_t status, uint8_t d1, uint8_t d2, uint8_t trigStatus, uint8_t trigNote, uint32_t ms) -> void {
  if (loopMotorCount_ + 3 > kLoopMotorQueue) return;
  loopMotor_[loopMotorCount_++] = LoopMotor{{status, d1, d2}, ms};
  loopMotor_[loopMotorCount_++] = LoopMotor{{static_cast<uint8_t>(0x90 | trigStatus), trigNote, 127}, ms};
  loopMotor_[loopMotorCount_++] =
      LoopMotor{{static_cast<uint8_t>(0x80 | trigStatus), trigNote, 0}, ms + ControlSurfaceNoteEdit<Engine>::kMotorGateMs};
  loopWrittenMs_[fader] = ms;
}

template <typename Engine>
inline auto ControlSurface<Engine>::parkLoopFaders(uint32_t ms) -> void {
  loopPark_ = false;
  loopMotorCount_ = 0;
  const ManifestId c = transport().contentOf(shownSlot(selected()));
  {  // the start: its place in the stored loop, F1's travel over the stored loop's 16ths
    uint16_t wire = 0;
    if (c != kNoManifest) {
      beyondEnd(c);
      const uint32_t steps = storedTicks_ / ControlSurfaceNoteEdit<Engine>::kSixteenth, k = playStart_ / ControlSurfaceNoteEdit<Engine>::kSixteenth;
      if (steps > 1) wire = static_cast<uint16_t>((k * 16383u + (steps - 1) / 2) / (steps - 1));
      parkedStart_ = static_cast<int32_t>(k);
    }
    loopWrite(0, 0xE0 | kProgramChannel, static_cast<uint8_t>(wire & 0x7F), static_cast<uint8_t>(wire >> 7),
              kProgramChannel, kStartTrigger, ms);
  }
  const uint32_t loop = c == kNoManifest ? ControlSurfaceNoteEdit<Engine>::kBar : e_->manifests().manifest(c).loopLengthTicks;
  parkedNotch_ = static_cast<int16_t>(positionOf(loop));
  loopWrite(1, 0xB0 | kProgramChannel, kLengthCc, ccOfNotch(positionOf(loop)), kProgramChannel, kLengthTrigger, ms);
  if (clock_->external()) return;
  writeFine(static_cast<uint8_t>(64 + tempoTenths() % 10), ms);
  const uint16_t wire = wireOfBpm(coarseBpm_);
  loopWrite(3, 0xE0 | kTempoChannel, static_cast<uint8_t>(wire & 0x7F), static_cast<uint8_t>(wire >> 7),
            kTempoChannel, kCoarseTrigger, ms);
}

template <typename Engine>
inline auto ControlSurface<Engine>::writeFine(uint8_t cc, uint32_t ms) -> void {
  loopWrite(2, 0xB0 | kProgramChannel, kFineCc, cc, kTempoChannel, kFineTrigger, ms);
}

template <typename Engine>
inline auto ControlSurface<Engine>::startFader(int16_t pb, uint32_t ms) -> void {
  if (landing(0, ms)) return;
  const ManifestId c = transport().contentOf(shownSlot(selected()));
  if (c == kNoManifest) return;
  beyondEnd(c);
  const uint32_t steps = storedTicks_ / ControlSurfaceNoteEdit<Engine>::kSixteenth;
  const uint32_t k =
      steps > 1 ? (static_cast<uint32_t>(static_cast<int32_t>(pb) + 8192) * (steps - 1) + 8191) / 16383 : 0;
  // The motor's echo of its parked place — within a
  // 16th of it, the fader's reading being coarser — is no move; the first other value is, and ends the guard.
  if (parkedStart_ >= 0 && static_cast<int32_t>(k) - parkedStart_ <= 1 && parkedStart_ - static_cast<int32_t>(k) <= 1)
    return;
  parkedStart_ = -1;
  pendingStart_ = k * ControlSurfaceNoteEdit<Engine>::kSixteenth;
  startPending_ = true;
  startMs_ = ms;
}

template <typename Engine>
inline auto ControlSurface<Engine>::settleStart(uint32_t ms) -> void {
  if (!startPending_ || ms - startMs_ < kLengthSettleMs) return;
  startPending_ = false;
  if (!loopEdit_) return;
  const uint8_t slot = shownSlot(selected());
  const ManifestId c = transport().contentOf(slot);
  if (c == kNoManifest) return;
  setPlayWindow(slot, pendingStart_, e_->manifests().manifest(c).loopLengthTicks, Mark::Keep);
}

template <typename Engine>
inline auto ControlSurface<Engine>::lengthFader(int16_t pb, uint32_t ms) -> void {
  lengthAt(static_cast<uint8_t>((static_cast<int32_t>(pb) + 8192) * kLengthScaleCount / 16384), ms);
}

template <typename Engine>
inline auto ControlSurface<Engine>::loopAt(uint8_t i) -> uint32_t {
  static constexpr uint16_t kFraction[5] = {48, 96, 192, 384, 576};  // ticks: 1/16 … 3/4 bar
  static constexpr uint16_t kTail[4] = {32, 64, 96, 128};            // bars
  const uint32_t bar = ControlSurfaceNoteEdit<Engine>::kBar;
  return i < 5 ? kFraction[i] : i < 21 ? (i - 4u) * bar : kTail[i < kLengthScaleCount ? i - 21 : 3] * bar;
}

template <typename Engine>
inline auto ControlSurface<Engine>::notchOfCc(uint8_t cc) -> uint8_t {
  return static_cast<uint8_t>(((cc & 0x7F) * (kLengthScaleCount - 1) + 63) / 127);
}

template <typename Engine>
inline auto ControlSurface<Engine>::positionOf(uint32_t loop) -> uint8_t {
  uint8_t i = 0;
  while (i + 1 < kLengthScaleCount && loopAt(static_cast<uint8_t>(i + 1)) <= loop) ++i;
  return i;
}

template <typename Engine>
inline auto ControlSurface<Engine>::lengthAt(uint8_t pos, uint32_t ms) -> void {
  if (landing(1, ms)) return;
  if (parkedNotch_ >= 0 && pos == parkedNotch_) return;  // the parked notch echoed (a length off the scale parks below)
  parkedNotch_ = -1;
  pendingLoop_ = loopAt(pos);  // past the last notch: the last
  pendingMs_ = ms;
}

template <typename Engine>
inline auto ControlSurface<Engine>::settleLength(uint32_t ms) -> void {
  if (pendingLoop_ == 0 || ms - pendingMs_ < kLengthSettleMs) return;
  const uint32_t loop = pendingLoop_;
  pendingLoop_ = 0;
  if (!loopEdit_ && !popup_) return;
  resizeTo(popup_ ? popupSlot_ : shownSlot(selected()), loop, Mark::Keep);
  if (popup_) popupChoice_ = choiceOf(popupSlot_);
}

template <typename Engine>
inline auto ControlSurface<Engine>::rangeInput(uint8_t note, bool down, uint32_t ms) -> void {
  const bool bars = note >= kBarStepBase;
  const uint8_t k = static_cast<uint8_t>(bars ? note - kBarStepBase : note);
  uint16_t& held = bars ? rangeBars_ : rangeSteps_;
  uint8_t& anchor = bars ? barAnchor_ : stepAnchor_;
  const uint16_t bit = static_cast<uint16_t>(1u << k);
  if (!down) {
    held = static_cast<uint16_t>(held & ~bit);
    return;
  }
  if (held != 0 && anchor != k) {
    loopRange(bars, anchor, k);
    rangeTapNote_ = 0xFF;
  } else if (held == 0 && rangeTapNote_ == note && ms - rangeTapMs_ <= kCopyDoubleMs) {
    loopRange(bars, k, k);
    rangeTapNote_ = 0xFF;
  } else {
    anchor = k;
    if (held == 0) {
      rangeTapNote_ = note;
      rangeTapMs_ = ms;
    }
  }
  held = static_cast<uint16_t>(held | bit);
}

template <typename Engine>
inline auto ControlSurface<Engine>::loopRange(bool bars, uint8_t a, uint8_t b) -> void {
  const uint8_t t = selected(), slot = shownSlot(t);
  auto& x = transport();
  const ManifestId c = x.contentOf(slot);
  if (c == kNoManifest) return;
  beyondEnd(c);
  const uint32_t loop = e_->manifests().manifest(c).loopLengthTicks;
  constexpr uint32_t kBar = ControlSurfaceNoteEdit<Engine>::kBar, kStep = ControlSurfaceNoteEdit<Engine>::kSixteenth;
  const uint32_t pos = x.playing(t) && x.playingSlot(t) == slot ? (tick_ - x.origin(t)) % loop : 0;
  const uint32_t at = (playStart_ + pos) % storedTicks_;
  const uint32_t unit = bars ? kBar : kStep, base = bars ? at / (8 * kBar) * (8 * kBar) : at / kBar * kBar;
  const uint8_t lo = a < b ? a : b, hi = a < b ? b : a;
  const uint32_t start = base + lo * unit, length = (hi - lo + 1u) * unit;
  if (start >= storedTicks_) return;
  setPlayWindow(slot, start, length, Mark::Range, true);  // buttons past the stored loop's end lengthen it
}

template <typename Engine>
inline auto ControlSurface<Engine>::resizeTo(uint8_t slot, uint32_t loop, Mark mark) -> void {
  const ManifestId c = transport().contentOf(slot);
  if (c == kNoManifest) return;
  beyondEnd(c);
  setPlayWindow(slot, playStart_, loop, mark);
}

template <typename Engine>
inline auto ControlSurface<Engine>::setPlayWindow(uint8_t slot, uint32_t start, uint32_t length, Mark mark, bool grow) -> bool {
  const ManifestId c = transport().contentOf(slot);
  if (c == kNoManifest || length == 0) return false;
  beyondEnd(c);
  uint32_t storedTicks = storedTicks_, at = playStart_;
  const uint32_t loop = e_->manifests().manifest(c).loopLengthTicks;
  if (start >= storedTicks) return false;
  PlayWindow marks = transport().playWindow(slot);
  if (mark == Mark::Range) {  // B the new range; A the one playing (when it was A or B), else B before
    const bool wasA = marks.aLength != 0 && at == marks.aStart && loop == marks.aLength;
    const bool wasB = marks.bLength != 0 && at == marks.bStart && loop == marks.bLength;
    if (wasB || (!wasA && marks.bLength != 0)) {
      marks.aStart = marks.bStart;
      marks.aLength = marks.bLength;
    }
    marks.bStart = start;
    marks.bLength = length;
  }
  if (start == at && length == loop) return false;
  const auto ok = [&](typename X::Result r) {
    if (r != X::Result::Ok) return false;
    visited(slot);
    return true;
  };
  if (loop != storedTicks && !ok(control().resize(slot, storedTicks, tick_, us_, us(), visitState_, &marks))) return false;
  if (length > storedTicks || (grow && start + length > storedTicks)) {  // longer: back to its start, then the storedTicks grows
    if (at != 0 && !ok(control().rotate(slot, storedTicks - at, tick_, us_, us(), visitState_, &marks))) return false;
    at = 0;
    storedTicks = start + length;
    if (!ok(control().resize(slot, storedTicks, tick_, us_, us(), visitState_, &marks))) return false;
  }
  const uint32_t shift = (start + storedTicks - at) % storedTicks;
  if (shift != 0 && !ok(control().rotate(slot, shift, tick_, us_, us(), visitState_, &marks))) return false;
  if (length != storedTicks && !ok(control().resize(slot, length, tick_, us_, us(), visitState_, &marks))) return false;
  loopPark_ = true;  // the faders to the new start and length
  return true;
}

template <typename Engine>
inline auto ControlSurface<Engine>::loopTicks(uint8_t slot) -> uint32_t {
  const ManifestId c = transport().contentOf(slot);
  return c == kNoManifest ? 0 : e_->manifests().manifest(c).loopLengthTicks;
}

template <typename Engine>
inline auto ControlSurface<Engine>::switchRange(uint8_t slot) -> void {
  const ManifestId c = transport().contentOf(slot);
  if (c == kNoManifest) return;
  beyondEnd(c);
  const PlayWindow w = transport().playWindow(slot);
  const uint32_t loop = loopTicks(slot);
  const bool onB = w.bLength != 0 && w.start == w.bStart && loop == w.bLength;
  visitState_ = 0;  // its own step
  if ((onB || w.bLength == 0) && w.aLength != 0)
    setPlayWindow(slot, w.aStart, w.aLength, Mark::Keep);
  else if (w.bLength != 0)
    setPlayWindow(slot, w.bStart, w.bLength, Mark::Keep);
  pendingStart_ = 0;
  startPending_ = false;
}

template <typename Engine>
inline auto ControlSurface<Engine>::playFull(uint8_t slot) -> void {
  const ManifestId c = transport().contentOf(slot);
  if (c == kNoManifest) return;
  beyondEnd(c);
  visitState_ = 0;
  setPlayWindow(slot, 0, storedTicks_, Mark::Keep);
  pendingStart_ = 0;
  startPending_ = false;
}

template <typename Engine>
inline auto ControlSurface<Engine>::openPopup(uint8_t slot, uint8_t button, uint32_t ms) -> void {
  if (empty(slot)) return;
  popup_ = true;
  popupSlot_ = slot;
  popupButton_ = button;
  popupTaps_ = 0;
  popupHeld_ = false;
  popupUntil_ = ms + kPopupMs;
  visitState_ = 0;
  popupChoice_ = choiceOf(slot);
  const uint32_t loop = e_->manifests().manifest(transport().contentOf(slot)).loopLengthTicks;
  loopWrite(1, 0xB0 | kProgramChannel, kLengthCc, ccOfNotch(positionOf(loop)), kProgramChannel, kLengthTrigger, ms);
}

template <typename Engine>
inline auto ControlSurface<Engine>::popupPress(bool down, uint32_t ms) -> void {
  popupUntil_ = ms + kPopupMs;
  if (down) {
    popupDownMs_ = ms;
    popupHeld_ = true;
    return;
  }
  if (popupHeld_ && ms - popupDownMs_ >= kPopupCancelMs) {
    cancelPopup();
    return;
  }
  popupHeld_ = false;
  ++popupTaps_;
  popupTapMs_ = ms;
}

template <typename Engine>
inline auto ControlSurface<Engine>::cancelPopup() -> void {
  pendingLoop_ = 0;
  popupTaps_ = 0;
  popupHeld_ = false;
  const ManifestId c = transport().contentOf(popupSlot_);
  if (visitState_ != 0 && c != kNoManifest && e_->manifests().manifest(c).identity == visitState_)
    control().undo(popupSlot_, tick_, us_, us());
  popup_ = false;
  visitState_ = 0;
}

template <typename Engine>
inline auto ControlSurface<Engine>::servicePopup(uint32_t ms) -> void {
  if (!popup_) return;
  if (popupTaps_ != 0 && ms - popupTapMs_ >= kPopupTapMs) {  // the burst is over
    const bool back = popupTaps_ >= 2;
    popupTaps_ = 0;
    popupChoice_ = nextChoice(popupSlot_, back);
    applyChoice(popupSlot_, popupChoice_);
    popupUntil_ = ms + kPopupMs;
  }
  if (pendingLoop_ != 0) popupUntil_ = ms + kPopupMs;
  if (popupTaps_ == 0 && static_cast<int32_t>(ms - popupUntil_) >= 0) {
    popup_ = false;
    visitState_ = 0;
  }
}

template <typename Engine>
inline auto ControlSurface<Engine>::choiceBars(uint8_t i) -> uint16_t {
  static constexpr uint16_t kBars[kChoiceCount - 3] = {1, 2, 3, 4, 5, 6, 7, 8, 16, 32, 64, 128};
  return i >= 3 && i < kChoiceCount ? kBars[i - 3] : 0;
}

template <typename Engine>
inline auto ControlSurface<Engine>::choiceOf(uint8_t slot) -> uint8_t {
  const ManifestId c = transport().contentOf(slot);
  if (c == kNoManifest) return kNoChoice;
  beyondEnd(c);
  const PlayWindow w = transport().playWindow(slot);
  const uint32_t loop = loopTicks(slot), start = playStart_;
  if (start == 0 && loop == storedTicks_) return kChoiceFull;
  if (w.bLength != 0 && start == w.bStart && loop == w.bLength) return kChoiceB;
  if (w.aLength != 0 && start == w.aStart && loop == w.aLength) return kChoiceA;
  for (uint8_t i = 3; i < kChoiceCount; ++i)
    if (start == 0 && loop == choiceBars(i) * ControlSurfaceNoteEdit<Engine>::kBar) return i;
  return kNoChoice;
}

template <typename Engine>
inline auto ControlSurface<Engine>::nextChoice(uint8_t slot, bool back) -> uint8_t {
  const PlayWindow w = transport().playWindow(slot);
  uint8_t i = popupChoice_;
  if (i == kNoChoice) {  // by length: the first numbered choice above it, or the last below it
    const uint32_t loop = loopTicks(slot), bar = ControlSurfaceNoteEdit<Engine>::kBar;
    if (back) {
      for (uint8_t k = kChoiceCount; k-- > 3;)
        if (choiceBars(k) * bar < loop) return k;
      return kChoiceCount - 1;
    }
    for (uint8_t k = 3; k < kChoiceCount; ++k)
      if (choiceBars(k) * bar > loop) return k;
    return kChoiceFull;
  }
  do {
    i = static_cast<uint8_t>(back ? (i + kChoiceCount - 1) % kChoiceCount : (i + 1) % kChoiceCount);
  } while ((i == kChoiceA && w.aLength == 0) || (i == kChoiceB && w.bLength == 0));
  return i;
}

template <typename Engine>
inline auto ControlSurface<Engine>::applyChoice(uint8_t slot, uint8_t i) -> void {
  const ManifestId c = transport().contentOf(slot);
  if (c == kNoManifest) return;
  beyondEnd(c);
  const PlayWindow w = transport().playWindow(slot);
  if (i == kChoiceFull)
    setPlayWindow(slot, 0, storedTicks_, Mark::Keep);
  else if (i == kChoiceA && w.aLength != 0)
    setPlayWindow(slot, w.aStart, w.aLength, Mark::Keep);
  else if (i == kChoiceB && w.bLength != 0)
    setPlayWindow(slot, w.bStart, w.bLength, Mark::Keep);
  else if (choiceBars(i) != 0)
    setPlayWindow(slot, 0, choiceBars(i) * ControlSurfaceNoteEdit<Engine>::kBar, Mark::Keep);
}

template <typename Engine>
inline auto ControlSurface<Engine>::tempoTenths() const -> uint32_t {
  const uint64_t d = static_cast<uint64_t>(clock_->usPerTick()) * EngineClock::kTicksPerQuarter;
  return static_cast<uint32_t>((600000000ull + d / 2) / d);
}

template <typename Engine>
inline auto ControlSurface<Engine>::bpmOfWire(uint32_t wire) -> uint16_t {
  return static_cast<uint16_t>(kCoarseMinBpm + (wire * (kCoarseMaxBpm - kCoarseMinBpm) + 8191) / 16383);
}

template <typename Engine>
inline auto ControlSurface<Engine>::wireOfBpm(uint32_t bpm) -> uint16_t {
  if (bpm < kCoarseMinBpm) bpm = kCoarseMinBpm;
  if (bpm > kCoarseMaxBpm) bpm = kCoarseMaxBpm;
  return static_cast<uint16_t>((bpm - kCoarseMinBpm) * 16383 / (kCoarseMaxBpm - kCoarseMinBpm));
}

template <typename Engine>
inline auto ControlSurface<Engine>::coarseTempo(int16_t pb, uint32_t ms) -> void {
  if (clock_->external() || landing(3, ms)) return;
  const uint16_t bpm = bpmOfWire(static_cast<uint32_t>(static_cast<int32_t>(pb) + 8192));
  if (bpm == coarseBpm_) return;
  coarseBpm_ = bpm;
  clock_->setTempo(bpm * 100u);
  writeFine(64, ms);
}

template <typename Engine>
inline auto ControlSurface<Engine>::fineTempo(uint8_t cc, uint32_t ms) -> void {
  if (clock_->external() || landing(2, ms)) return;
  int32_t tenths = static_cast<int32_t>(coarseBpm_) * 10 + static_cast<int32_t>(cc & 0x7F) - 64;
  if (tenths < static_cast<int32_t>(kCoarseMinBpm) * 10) tenths = kCoarseMinBpm * 10;
  if (tenths > static_cast<int32_t>(kCoarseMaxBpm) * 10) tenths = kCoarseMaxBpm * 10;
  if (static_cast<uint32_t>(tenths) == tempoTenths()) return;
  clock_->setTempo(static_cast<uint32_t>(tenths) * 10u);
}

}  // namespace ContentEngine
