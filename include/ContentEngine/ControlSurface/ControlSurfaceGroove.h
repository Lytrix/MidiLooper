//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// `ControlSurface`'s GROOVE window (fields, compare, cycle turns, motor faders, the columns saved): the member function bodies, declared (and documented) in `ControlSurface.h`, which
// includes this file after the class.

#pragma once

#include "ContentEngine/ControlSurface/ControlSurface.h"

namespace ContentEngine {

template <typename Engine>
inline auto ControlSurface<Engine>::grooveProgress() const -> uint8_t {
  if (groovePending_) {
    const uint32_t t = nowMs_ - groovePendingMs_;
    return static_cast<uint8_t>((t >= kLengthSettleMs ? kLengthSettleMs : t) * 25 / kLengthSettleMs);
  }
  const auto st = e_->grooveState();
  if (st == Engine::Groove::State::Waiting || st == Engine::Groove::State::Building || st == Engine::Groove::State::Ready)
    return static_cast<uint8_t>(25 + e_->grooveProgress() / 2);
  if (grooveSwitchAt_ > tick_ && grooveSwitchAt_ > grooveSwitchFrom_ && clock_->running())
    return static_cast<uint8_t>(75 + 25 * (tick_ - grooveSwitchFrom_) / (grooveSwitchAt_ - grooveSwitchFrom_));
  return 0xFF;
}

template <typename Engine>
inline auto ControlSurface<Engine>::grooveDots() const -> uint8_t {
  if (groovePending_) return 1;
  const auto st = e_->grooveState();
  if (st == Engine::Groove::State::Waiting || st == Engine::Groove::State::Building ||
      st == Engine::Groove::State::Ready || grooveToHear_)
    return 2;
  if (grooveHeardMs_ != 0 && nowMs_ - grooveHeardMs_ < kDotsDoneMs) return 3;
  return 0;
}

template <typename Engine>
inline auto ControlSurface<Engine>::cycleTurnPos() const -> uint16_t {
  if (!cycling_ || tick_ < cycleStart_) return 0;
  const uint32_t len = static_cast<uint32_t>(cycleBars_) * kBarTicks;
  return static_cast<uint16_t>((tick_ - cycleStart_) % len * 1000 / len);
}

template <typename Engine>
inline auto ControlSurface<Engine>::openGroove(uint8_t slot) -> void {
  if (slot >= Config::kSlotCount || empty(slot) || browsing()) return;
  groove_ = true;
  grooveWin_ = slot;
  grooveField_ = kFieldGrooveA;
  const uint32_t packed = transport().playWindow(slot).groove;
  if (packed != 0) {  // A the slot's groove, B a copy to change
    grooveA_ = groove::Settings::unpack(packed);
  } else {  // 1/16 by default, 50 %
    grooveA_ = groove::Settings{};
    grooveA_.tmpl = groove::kStraight;
    grooveA_.amount = 50;
  }
  grooveAuto_ = grooveA_.autoGroove;
  grooveALabel_[0] = 0;
  if (grooveA_.tmpl == groove::kFileTemplate) {
    const PlayWindow sw = transport().playWindow(slot);
    if (!readTemplate(e_->runs(), e_->pool(), sw.tableRun, sw.grooveShape, grooveATable_, grooveALabel_))
      grooveA_.tmpl = groove::kStraight;
  }
  grooveSet_ = grooveA_;
  heard_ = packed != 0 ? Heard::A : Heard::Off;
  restoreColumns(slot, packed);
  openedA_ = grooveA_.pack();  // as opened: `closeGroove` stores the columns only when a visit changed them
  openedB_ = grooveSet_.pack();
  openedHeard_ = static_cast<uint8_t>(static_cast<uint8_t>(heard_) + 1);
  faderSeen_ = 0;
  parkGrooveFaders(nowMs_);
  cycling_ = false;
  grooveAt_ = 0;
  leaveVisit();
  groovePending_ = false;
}

template <typename Engine>
inline auto ControlSurface<Engine>::restoreColumns(uint8_t slot, uint32_t packed) -> void {
  const PlayWindow w = transport().playWindow(slot);
  if (w.heard == 0) return;
  groove::Settings a = groove::Settings::unpack(w.columnA), b = groove::Settings::unpack(w.columnB);
  const auto fileless = [&](groove::Settings& g) {
    if (g.tmpl == groove::kFileTemplate && (packed == 0 || groove::Settings::unpack(packed).tmpl != groove::kFileTemplate))
      g.tmpl = groove::kStraight;
  };
  fileless(a);
  fileless(b);
  grooveSet_ = b;
  const Heard h = static_cast<Heard>(w.heard - 1);
  const groove::Settings played = groove::Settings::unpack(packed);
  const auto plays = [&](groove::Settings g) {  // as `settingsOf` built it: the whole loop, the slot's AUTO Q
    g.scope = groove::kFull;
    g.autoGroove = played.autoGroove;
    return g.pack() == packed;
  };
  const bool matches = h == Heard::Off ? packed == 0 || played.amount == 0 : plays(h == Heard::A ? a : b);
  if (!matches) return;  // the slot's groove changed outside the window: A is it, B kept
  grooveA_ = a;
  heard_ = h;
}

template <typename Engine>
inline auto ControlSurface<Engine>::toggleGroove() -> void {
  if (groove_) closeGroove();
  else openGroove(shownSlot(selected()));
}

template <typename Engine>
inline auto ControlSurface<Engine>::closeGroove() -> void {
  if (cycling_) stopCycle();  // what plays now stays; a turn built for later is dropped
  grooveAt_ = 0;
  if (groovePending_ && grooveHeard() == pendingCol_) playHeard(pendingCol_);  // a change still settling is kept
  groovePending_ = false;
  groove_ = false;
  if (e_->grooveState() == Engine::Groove::State::Waiting) e_->grooveCancel();  // waiting for room: given up
  // The columns as left are stored with the slot — now, replacing this visit's own state, or once a build still
  // under way is published.
  // Only a visit that changed A, B or the column heard: opening and closing alone is no step.
  columnsSlot_ = grooveWin_;
  columnsA_ = grooveA_.pack();
  columnsB_ = grooveSet_.pack();
  columnsHeard_ = static_cast<uint8_t>(static_cast<uint8_t>(heard_) + 1);
  columnsOwed_ = columnsA_ != openedA_ || columnsB_ != openedB_ || columnsHeard_ != openedHeard_;
  if (columnsOwed_ && e_->grooveState() == Engine::Groove::State::Idle) writeColumns(visitState_);
  leaveVisit();
}

template <typename Engine>
inline auto ControlSurface<Engine>::writeColumns(uint32_t replace) -> void {
  columnsOwed_ = false;
  control().setColumns(columnsSlot_, columnsA_, columnsB_, columnsHeard_, tick_, us_, us(), replace);
}

template <typename Engine>
inline auto ControlSurface<Engine>::leaveVisit() -> void {
  visitState_ = 0;
  grooveSlot_ = 0xFF;
}

template <typename Engine>
inline auto ControlSurface<Engine>::isFull(uint8_t slot) -> bool {
  if (slot >= Config::kSlotCount) return true;
  const ManifestId c = transport().contentOf(slot);
  if (c == kNoManifest) return true;
  beyondEnd(c);
  return playStart_ == 0 && loopTicks(slot) == storedTicks_;
}

template <typename Engine>
inline auto ControlSurface<Engine>::loopFrame(uint8_t slot, uint32_t& scale, uint32_t& start, uint32_t& known) -> bool {
  if (slot >= Config::kSlotCount) return false;
  const ManifestId c = transport().contentOf(slot);
  if (c == kNoManifest) return false;
  beyondScan(c, kScanRows / 2);  // the screen: in steps (the last state scanned until this one is done)
  scale = storedTicks_;
  start = playStart_;
  known = absEnd_;
  return true;
}

template <typename Engine>
inline auto ControlSurface<Engine>::service(uint32_t ms, uint32_t lastTick, uint32_t nowUs) -> void {
  tick_ = lastTick;
  us_ = nowUs;
  nowMs_ = ms;
  if (edit_.editing() && !edit_.velocityMode() && press_.heldMs(kLength, ms) >= kVelocityHoldMs) {
    press_.consume(kLength);  // a long hold: velocity mode, F4's motor to the velocity
    edit_.velocityBegin(ms, true);
  }
  serviceCopy(ms);
  // EDIT held `kEditExitMs` leaves the edit modes at once; its release is then no press.
  if ((edit_.editing() || edit_.entering() || loopEdit_) && press_.pressed(kEditMode) &&
      press_.heldMs(kEditMode, ms) >= kEditExitMs) {
    press_.consume(kEditMode);
    leaveEdit();
  }
  if (columnsOwed_ && !groove_ && e_->grooveState() == Engine::Groove::State::Idle) {  // after the visit's last build:
    const ManifestId c = transport().contentOf(columnsSlot_);                              // folded into its state
    writeColumns(c == kNoManifest ? 0 : e_->manifests().manifest(c).identity);
  }
  if (e_->grooveService(lastTick, clock_->running(), nowUs, us(), visitState_, grooveAt_) ==
      Engine::Transport::Result::Ok) {
    grooveAt_ = 0;
    visited(grooveSlot_);  // a groove published; this visit's next one replaces it
    grooveSwitchFrom_ = lastTick;  // the window's progress runs on to the beat it is heard on
    grooveSwitchAt_ = e_->groovePublishedAt();
    grooveToHear_ = true;
  }
  if (grooveToHear_ && (!clock_->running() || lastTick >= grooveSwitchAt_)) {  // heard — the dots lit
    grooveToHear_ = false;
    grooveHeardMs_ = ms == 0 ? 1 : ms;
  }
  serviceGroove(ms);
  for (uint8_t k = 0; k < 16 && lockStep_ == 0xFF && edit_.editing(); ++k)
    if (press_.heldMs(kStep0 + k, ms) >= kLockMs) {  // the parameter lock (an empty step: nothing, no gesture)
      lockStep_ = k;
      edit_.lockBegin(k, ms);
    }
  uint8_t b;
  Press p;
  while (press_.next(ms, b, p)) dispatch(b, p);
  edit_.service(ms);
  settleLength(ms);
  settleStart(ms);
  servicePopup(ms);
}

template <typename Engine>
inline auto ControlSurface<Engine>::popupRanges() -> uint8_t {
  if (transport().contentOf(popupSlot_) == kNoManifest) return 0;
  const PlayWindow w = transport().playWindow(popupSlot_);
  return static_cast<uint8_t>((w.aLength != 0 ? 1 : 0) | (w.bLength != 0 ? 2 : 0));
}

template <typename Engine>
inline auto ControlSurface<Engine>::popupLoop() const -> uint32_t {
  if (pendingLoop_ != 0) return pendingLoop_;
  const ManifestId c = e_->transport().contentOf(popupSlot_);
  return c == kNoManifest ? 0 : e_->manifests().manifest(c).loopLengthTicks;
}

template <typename Engine>
inline auto ControlSurface<Engine>::loopStart() -> uint32_t {
  return startPending_ ? pendingStart_ : e_->transport().playWindow(shownSlot(selected())).start;
}

template <typename Engine>
inline auto ControlSurface<Engine>::motor(uint32_t ms, typename ControlSurfaceNoteEdit<Engine>::MotorMsg& m) -> bool {
  if (edit_.motor(ms, m)) return true;
  if (loopPark_) parkLoopFaders(ms);
  for (uint8_t i = 0; i < loopMotorCount_; ++i) {  // the first one due, in queue order
    if (static_cast<int32_t>(ms - loopMotor_[i].at) < 0) continue;
    m = loopMotor_[i].m;
    for (uint8_t k = i; k + 1 < loopMotorCount_; ++k) loopMotor_[k] = loopMotor_[k + 1];
    --loopMotorCount_;
    return true;
  }
  return false;
}

template <typename Engine>
inline auto ControlSurface<Engine>::transportStarted(uint32_t firstTick) -> void {
  auto& x = transport();
  for (uint8_t t = 0; t < kTracks; ++t) {
    const uint8_t armed = control().queuedRecordSlot(t);
    if (armed != 0xFF) {
      control().queueRecord(t, armed, firstTick - 1);
      continue;
    }
    // Every track not muted and not playing starts its selected slot on the first tick.
    const uint8_t slot = selectedSlot(t);
    if (x.playing(t) || x.writingTrack() == t || control().muted(t) || empty(slot)) continue;
    control().queueSwitch(t, slot, Engine::Control::Quantize::NextGrid, firstTick - 1, false);  // not a gesture
  }
}

template <typename Engine>
inline auto ControlSurface<Engine>::setBrowser(ProductionBrowser* b) -> void {
  browser_ = b;
  if (b != nullptr) b->setNewLoopBars(kNewLoopBars);  // the picker starts on the preset closest to it
}

template <typename Engine>
inline auto ControlSurface<Engine>::bank(uint8_t& program) -> bool {
  const uint8_t want = groove_ ? kGrooveBank : edit_.editing() ? kNoteEditBank : kLoopEditBank;
  if (want == bankSent_) return false;
  bankSent_ = want;
  program = want;
  return true;
}

template <typename Engine>
inline auto ControlSurface<Engine>::ledsWanted(uint32_t lastTick, uint32_t ms, uint8_t* want) -> void {
  desired(lastTick, ms, want);
  copyLeds(ms, want);
}

template <typename Engine>
template <typename Send>
inline auto ControlSurface<Engine>::leds(uint32_t lastTick, uint32_t ms, Send send, uint8_t max) -> uint8_t {
  uint8_t want[128] = {};
  desired(lastTick, ms, want);
  copyLeds(ms, want);
  uint8_t n = 0;
  for (uint8_t note = 0; note < 128 && n < max; ++note) {
    if (sent_[note] == want[note]) continue;
    if (sent_[note] == 0xFF && want[note] == 0) {  // never lit: nothing to clear
      sent_[note] = 0;
      continue;
    }
    send(note, want[note]);
    sent_[note] = want[note];
    ++n;
  }
  return n;
}
template <typename Engine>
inline auto ControlSurface<Engine>::nextGrooveField(int8_t d) -> void {
  grooveField_ = static_cast<uint8_t>((grooveField_ + kGrooveFields + d) % kGrooveFields);
}

template <typename Engine>
inline auto ControlSurface<Engine>::columnOf(uint8_t f) -> Heard {
  return f == kFieldGrooveA || f == kFieldAmountA ? Heard::A : Heard::B;
}

template <typename Engine>
inline auto ControlSurface<Engine>::grooveChanged(Heard col, uint32_t ms) -> void {
  if (col != grooveHeard()) {
    if (cycling_) cycleAsked_ = 0xFFFFFFFFu;
    return;
  }
  groovePending_ = true;
  pendingCol_ = col;
  groovePendingMs_ = ms;
}

template <typename Engine>
inline auto ControlSurface<Engine>::toggleAutoQ(uint32_t ms) -> void {
  grooveAuto_ = !grooveAuto_;
  if (grooveHeard() != Heard::Off) grooveChanged(grooveHeard(), ms);
}

template <typename Engine>
inline auto ControlSurface<Engine>::grooveNudge(int32_t d, uint32_t ms) -> void {
  const Heard col = columnOf(grooveField_);
  groove::Settings& g = columnSettings(col);
  if (grooveField_ == kFieldGrooveA || grooveField_ == kFieldGrooveB) {
    const int32_t n = groove::kTemplateCount;
    g.tmpl = g.tmpl >= n ? groove::kStraight : static_cast<uint8_t>((g.tmpl + n + d % n) % n);  // a file's: the list
  } else {
    const int32_t a = static_cast<int32_t>(g.amount) + d * 5;
    g.amount = static_cast<uint8_t>(a < 0 ? 0 : a > 100 ? 100 : a);
  }
  grooveChanged(col, ms);
}

template <typename Engine>
inline auto ControlSurface<Engine>::grooveFader(uint8_t type, uint8_t channel, uint8_t d1, uint8_t d2, uint32_t ms) -> bool {
  if (type != 0xB0 || channel != 15 || d1 < kGrooveCc || d1 >= kGrooveCc + kGrooveFields) return false;
  const uint8_t field = static_cast<uint8_t>(d1 - kGrooveCc);
  if ((faderSeen_ & (1u << field)) == 0) {  // the DROID reports each fader as the bank opens: where it is, not a move
    faderSeen_ = static_cast<uint8_t>(faderSeen_ | (1u << field));
    return true;
  }
  if (landing(field, ms)) return true;  // its motor's travel to the value written, not a move
  const Heard col = columnOf(field);
  groove::Settings& g = columnSettings(col);
  const groove::Settings before = g;
  const uint32_t v = d2 & 0x7F;
  if (field == kFieldGrooveA || field == kFieldGrooveB) g.tmpl = static_cast<uint8_t>(v * groove::kTemplateCount / 128);
  else g.amount = static_cast<uint8_t>((v * 100 + 63) / 127);
  grooveField_ = field;
  if (g.tmpl != before.tmpl || g.amount != before.amount) grooveChanged(col, ms);
  return true;
}

template <typename Engine>
inline auto ControlSurface<Engine>::grooveCc(uint8_t field, const groove::Settings& g) -> uint8_t {
  if (field == kFieldGrooveA || field == kFieldGrooveB) {
    if (g.tmpl >= groove::kTemplateCount) return 0;
    const uint32_t v = (static_cast<uint32_t>(g.tmpl) * 128u + 64u) / groove::kTemplateCount;
    return static_cast<uint8_t>(v > 127 ? 127 : v);
  }
  return static_cast<uint8_t>((static_cast<uint32_t>(g.amount) * 127u + 50u) / 100u);
}

template <typename Engine>
inline auto ControlSurface<Engine>::parkGrooveFaders(uint32_t ms) -> void {
  loopMotorCount_ = 0;
  for (uint8_t f = 0; f < kGrooveFields; ++f)
    loopWrite(f, 0xB0 | kProgramChannel, static_cast<uint8_t>(kGrooveCc + f), grooveCc(f, columnSettings(columnOf(f))),
              kProgramChannel, static_cast<uint8_t>(kGrooveTrigger + f), ms);
}

template <typename Engine>
inline auto ControlSurface<Engine>::grooveHeard() const -> Heard {
  if (!cycling_ || tick_ < cycleStart_) return heard_;
  const uint32_t turn = (tick_ - cycleStart_) / (static_cast<uint32_t>(cycleBars_) * kBarTicks);
  return (turn & 1u) == 0 ? Heard::A : Heard::B;
}

template <typename Engine>
inline auto ControlSurface<Engine>::settingsOf(Heard h) const -> groove::Settings {
  groove::Settings x = h == Heard::B ? grooveSet_ : grooveA_;
  if (h == Heard::Off) x.amount = 0;
  x.scope = groove::kFull;
  x.autoGroove = grooveAuto_;
  return x;
}

template <typename Engine>
inline auto ControlSurface<Engine>::playHeard(Heard h, uint32_t at) -> void {
  const groove::Settings x = settingsOf(h);
  const bool file = x.tmpl == groove::kFileTemplate && x.amount != 0;
  grooveAt_ = at;
  applyGroove(grooveWin_, x, file ? &grooveATable_ : nullptr, file ? grooveALabel_ : nullptr,
              static_cast<uint8_t>(static_cast<uint8_t>(h) + 1));
}

template <typename Engine>
inline auto ControlSurface<Engine>::stopCycle() -> void {
  heard_ = grooveHeard();  // stopped where it is: what plays stays
  cycling_ = false;
  if (grooveAt_ != 0 && e_->grooveState() != Engine::Groove::State::Idle) e_->grooveCancel();  // a later turn's
  grooveAt_ = 0;
  if (grooveToHear_) playHeard(heard_);  // the next turn already published for its bar line: replaced by this one
}

template <typename Engine>
inline auto ControlSurface<Engine>::grooveCompare(Press p) -> void {
  if (p == Press::Long) {
    if (cycling_) {
      stopCycle();
    } else {
      cycling_ = true;
      cycleStart_ = (tick_ / kBarTicks + 1) * kBarTicks;
      cycleAsked_ = 0xFFFFFFFFu;
    }
    return;
  }
  if (p == Press::Double) {
    if (cycling_) stopCycle();
    heard_ = Heard::Off;
  } else if (p == Press::Short) {
    if (cycling_) {
      stopCycle();
      return;
    }
    heard_ = heard_ == Heard::A ? Heard::B : Heard::A;
  } else {
    return;
  }
  groovePending_ = false;  // the values stay; what plays is the one chosen
  playHeard(heard_);
}

template <typename Engine>
inline auto ControlSurface<Engine>::serviceCycle() -> void {
  if (!cycling_ || !clock_->running()) return;
  const uint32_t len = static_cast<uint32_t>(cycleBars_) * kBarTicks;
  const uint32_t k = tick_ < cycleStart_ ? 0 : (tick_ - cycleStart_) / len + 1;  // the turn starting next
  const uint32_t at = cycleStart_ + k * len;
  if (k == cycleAsked_ || at - tick_ > kBarTicks) return;
  cycleAsked_ = k;
  playHeard((k & 1u) == 0 ? Heard::A : Heard::B, at);
}

template <typename Engine>
inline auto ControlSurface<Engine>::serviceGroove(uint32_t ms) -> void {
  if (groove_) {
    if (groovePending_ && ms - groovePendingMs_ >= kLengthSettleMs) {
      groovePending_ = false;
      if (grooveHeard() == pendingCol_) playHeard(pendingCol_);  // still the one heard (cycling: still its turn)
    }
    serviceCycle();
    return;  // open until closed (DEC-292: groove browsing)
  }
  // AUTO ("at loop wrap everything is quantized"): a grooved shown slot whose groove asks for it,
  // with an overdub pass after its groove pass, is grooved again (the overdub folded in) — once per state.
  const uint8_t slot = shownSlot(selected());
  const ManifestId c = transport().contentOf(slot);
  if (c == kNoManifest || transport().writingSlot() == slot || edit_.editing() || loopEdit_ ||
      e_->grooveState() != Engine::Groove::State::Idle)
    return;
  const auto& d = e_->manifests().manifest(c);
  if (d.identity == autoTried_) return;
  const groove::Settings g = groove::Settings::unpack(transport().playWindow(slot).groove);
  if (g.amount == 0 || !g.autoGroove) return;
  bool afterGroove = false, overdub = false;
  for (uint8_t i = 0; i < d.runCount; ++i) {
    const uint8_t k = e_->runs().run(d.runs[i]).kind;
    if (k == RunKind::kGroove) afterGroove = true, overdub = false;
    else if (afterGroove && k == RunKind::kOverdub) overdub = true;
  }
  if (!overdub) return;
  autoTried_ = d.identity;  // once per state, whatever the outcome
  leaveVisit();
  applyGroove(slot, g);
}

}  // namespace ContentEngine
