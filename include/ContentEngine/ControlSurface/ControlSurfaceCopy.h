//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// `ControlSurface`'s copy mode (sources, targets, paste, clear): the member function bodies, declared (and documented) in `ControlSurface.h`, which
// includes this file after the class.

#pragma once

#include "ContentEngine/ControlSurface/ControlSurface.h"

namespace ContentEngine {

template <typename Engine>
inline auto ControlSurface<Engine>::serviceCopy(uint32_t ms) -> void {
  if (exitButton_ != 0xFF && !press_.pressed(exitButton_)) exitButton_ = 0xFF;  // released: it may start one again
  if (copy_ == Copy::None) {
    for (uint8_t s = 0; s < kSlotsPerTrack && !browsing() && !popup_; ++s)
      if (kSlot0 + s != exitButton_ && press_.heldMs(static_cast<uint8_t>(kSlot0 + s), ms) >= kCopyHoldMs) {
        press_.consume(static_cast<uint8_t>(kSlot0 + s));  // no long press of its own
        beginCopy(Copy::Slot, s, ms);
        copySlot_ = static_cast<uint8_t>(selected() * kSlotsPerTrack + s);
        return;
      }
    if (!edit_.editing()) return;
    for (uint8_t bar = 0; bar < 8; ++bar)
      if (((barsHeld_ >> bar) & 1u) && !((exitBars_ >> bar) & 1u) && ms - barDownMs_[bar] >= kCopyHoldMs) {
        beginBarCopy(bar, ms);
        return;
      }
    if (lockStep_ != 0xFF && edit_.locked() && !edit_.lockUsed() && kStep0 + lockStep_ != exitButton_ &&
        press_.heldMs(static_cast<uint8_t>(kStep0 + lockStep_), ms) >= kCopyHoldMs && edit_.captureNote(edit_.selection())) {
      beginCopy(Copy::Note, lockStep_, ms);
      copyNote_ = edit_.selection();
      copyEditSlot_ = edit_.slot();
    }
    return;
  }
  if (pending_.active) {  // the loop grew: the paste once NOTE_EDIT is back
    pendingPaste(ms);
    return;
  }
  if (targetTaps_ == 1 && ms - targetMs_ > kCopyDoubleMs) {  // a target pressed once: replace
    targetTaps_ = 0;
    pasteTo(targetButton_, false, ms);
    return;
  }
  if (sourceTaps_ == 1 && ms - sourceTapMs_ > kCopyDoubleMs) {  // the source pressed once: cancelled
    endCopy();
    return;
  }
  if (selected() != copyTrack_) {  // a track switch: the countdown starts again
    copyTrack_ = selected();
    copyUntil_ = ms + kCopyIdleMs;
  }
  if (!copyHolding_ && static_cast<int32_t>(ms - copyUntil_) >= 0) {  // nothing pasted: the window closes
    endCopy();
    return;
  }
  if ((copy_ == Copy::Bar || copy_ == Copy::Note) && !edit_.active()) {  // NOTE_EDIT left (a switch re-enters it)
    endCopy();
    return;
  }
  if (copy_ == Copy::Note && copyHolding_ && edit_.lockUsed()) {  // it was a parameter lock after all
    endCopy();
    return;
  }
  for (uint8_t b = 0; b < kButtons; ++b) {  // a long press of anything else ends it, doing nothing else
    if (copyHolding_ && b == sourceButton()) continue;
    if (copy_ == Copy::Slot && press_.heldMs(b, ms) >= kCopyHoldMs &&
        (b == kTrackSwitch || (b == sourceButton() && copySlot_ == selected() * kSlotsPerTrack + copyButton_))) {
      // TRACK switch held, or the source held again: LOAD LOOP (its short press is the next track)
      press_.consume(b);
      exitButton_ = b;
      loadLoop();
      return;
    }
    if (copyTarget(b) && press_.heldMs(b, ms) >= kCopyHoldMs && reSource(b, ms)) return;
    if (press_.heldMs(b, ms) >= kCopyHoldMs) {
      press_.consume(b);
      exitButton_ = b;  // the hold that ended it starts none until released
      endCopy();
      return;
    }
  }
  for (uint8_t bar = 0; bar < 8 && edit_.editing(); ++bar)
    if (((barsHeld_ >> bar) & 1u) && !(copyHolding_ && copy_ == Copy::Bar && bar == copyButton_) &&
        ms - barDownMs_[bar] >= kCopyHoldMs) {
      if (copy_ == Copy::Bar && (bar != copyButton_ || edit_.slot() != copyEditSlot_)) {  // another bar: the source now
        beginBarCopy(bar, ms);
        return;
      }
      exitBars_ = static_cast<uint8_t>(exitBars_ | (1u << bar));
      endCopy();
      return;
    }
}

template <typename Engine>
inline auto ControlSurface<Engine>::reSource(uint8_t b, uint32_t ms) -> bool {
  if (copy_ == Copy::Slot) {
    const uint8_t s = static_cast<uint8_t>(b - kSlot0);
    const uint8_t slot = static_cast<uint8_t>(selected() * kSlotsPerTrack + s);
    if (slot == copySlot_) return false;
    press_.consume(b);
    beginCopy(Copy::Slot, s, ms);
    copySlot_ = slot;
    return true;
  }
  const uint8_t k = static_cast<uint8_t>(b - kStep0);
  if (k == copyButton_ && edit_.slot() == copyEditSlot_) return false;
  const int16_t note = edit_.firstNoteAt(k);
  if (note < 0 || !edit_.captureNote(note)) return false;
  press_.consume(b);
  beginCopy(Copy::Note, k, ms);
  copyNote_ = note;
  copyEditSlot_ = edit_.slot();
  return true;
}

template <typename Engine>
inline auto ControlSurface<Engine>::beginBarCopy(uint8_t bar, uint32_t ms) -> void {
  const uint32_t start = edit_.barStart(bar);
  if (!edit_.captureBar(start)) return;
  beginCopy(Copy::Bar, bar, ms);
  copyBar_ = start;
  copyEditSlot_ = edit_.slot();
}

template <typename Engine>
inline auto ControlSurface<Engine>::beginCopy(Copy c, uint8_t button, uint32_t ms) -> void {
  copy_ = c;
  copyButton_ = button;
  copyHolding_ = true;
  copyTrack_ = selected();
  copyUntil_ = ms + kCopyIdleMs;
  sourceTaps_ = 0;
  targetTaps_ = 0;
  pasteMode_ = PasteMode::Ask;
  pending_ = Pending{};
  noteBar_ = edit_.bracket() / ControlSurfaceNoteEdit<Engine>::kBar * ControlSurfaceNoteEdit<Engine>::kBar;  // the steps' bar
}

template <typename Engine>
inline auto ControlSurface<Engine>::endCopy() -> void {
  copy_ = Copy::None;
  copyHolding_ = false;
  sourceTaps_ = 0;
  targetTaps_ = 0;
  pending_ = Pending{};
}

template <typename Engine>
inline auto ControlSurface<Engine>::sourceButton() const -> uint8_t {
  return copy_ == Copy::Slot ? static_cast<uint8_t>(kSlot0 + copyButton_)
         : copy_ == Copy::Note ? static_cast<uint8_t>(kStep0 + copyButton_)
                               : 0xFF;
}

template <typename Engine>
inline auto ControlSurface<Engine>::loadLoop() -> void {
  const uint8_t src = copySlot_;
  endCopy();
  if (browser_ == nullptr) return;
  leaveEdit();
  browser_->openPickerFromSlot(src, !empty(src));
}

template <typename Engine>
inline auto ControlSurface<Engine>::copyTarget(uint8_t b) const -> bool {
  return (copy_ == Copy::Slot && b >= kSlot0 && b < kSlot0 + kSlotsPerTrack) ||
         (copy_ == Copy::Note && b >= kStep0 && b < kStep0 + 16);
}

template <typename Engine>
inline auto ControlSurface<Engine>::copyReleased(Copy c, uint8_t button, uint32_t ms) -> bool {
  if (copy_ != c || button != copyButton_) return false;
  if (c != Copy::Slot && edit_.slot() != copyEditSlot_) return false;  // another slot's bar / step: a target
  if (c == Copy::Slot && copySlot_ != selected() * kSlotsPerTrack + button) return false;  // another track's slot
  if (copyHolding_) {  // the hold that started it: the countdown runs from here
    copyHolding_ = false;
    copyUntil_ = ms + kCopyIdleMs;
  } else if (sourceTaps_ == 1 && ms - sourceTapMs_ <= kCopyDoubleMs) {  // a double press: the source cleared
    clearSource(ms);
    endCopy();
  } else {  // once: cancelled when no second press follows (`serviceCopy`)
    sourceTaps_ = 1;
    sourceTapMs_ = ms;
  }
  return true;
}

template <typename Engine>
inline auto ControlSurface<Engine>::targetPress(uint8_t button, uint32_t ms) -> void {
  if (pasteMode_ != PasteMode::Ask) {
    pasteTo(button, pasteMode_ == PasteMode::Merge, ms);
    return;
  }
  if (targetTaps_ == 1 && button == targetButton_ && ms - targetMs_ <= kCopyDoubleMs) {
    targetTaps_ = 0;
    pasteTo(button, true, ms);
    return;
  }
  if (targetTaps_ == 1) return;  // another target before the first one's window ended: that one is pending
  targetTaps_ = 1;
  targetButton_ = button;
  targetMs_ = ms;
}

template <typename Engine>
inline auto ControlSurface<Engine>::pasteTo(uint8_t button, bool merge, uint32_t ms) -> void {
  const bool bar = copy_ == Copy::Bar;
  const uint32_t at = bar ? edit_.barStart(button) : noteBar_ + button * ControlSurfaceNoteEdit<Engine>::kSixteenth;
  const uint8_t led = static_cast<uint8_t>(bar ? 40 + button : button);
  if (at >= edit_.loop()) {  // past the loop's end: the loop grows to that bar's end, the paste follows
    const uint32_t loop = (at / ControlSurfaceNoteEdit<Engine>::kBar + 1) * ControlSurfaceNoteEdit<Engine>::kBar;
    const uint8_t slot = edit_.slot(), t = static_cast<uint8_t>(slot / kSlotsPerTrack);
    edit_.leave();
    if (control().resize(slot, loop, tick_, us_, us()) != X::Result::Ok) {
      enterEditOn(t, slot);  // refused: back as it was, copy mode kept
      return;
    }
    enterEditOn(t, slot);
    pending_ = Pending{true, merge, at, led};
    return;
  }
  const bool ok = bar ? edit_.copyBarAt(at, ms, merge) : edit_.copyNoteAt(at, ms, merge);
  if (!ok) return;
  flash(led, selected(), ms);
  endCopy();  // done: the window closes
}

template <typename Engine>
inline auto ControlSurface<Engine>::pendingPaste(uint32_t ms) -> void {
  if (!pending_.active) return;
  if (!edit_.active()) {  // NOTE_EDIT did not come back: nothing to paste into
    endCopy();
    return;
  }
  if (!edit_.editing()) return;  // still gathering
  pending_.active = false;
  const bool ok = copy_ == Copy::Bar ? edit_.copyBarAt(pending_.at, ms, pending_.merge)
                                     : edit_.copyNoteAt(pending_.at, ms, pending_.merge);
  if (!ok) return;
  flash(pending_.led, selected(), ms);
  endCopy();
}

template <typename Engine>
inline auto ControlSurface<Engine>::clearSource(uint32_t ms) -> void {
  if (copy_ == Copy::Slot) {
    if (control().clear(copySlot_, tick_, us_, us()) == X::Result::Ok) {
      flash(static_cast<uint8_t>(kSlotBase + copyButton_), selected(), ms);
      slot_[copySlot_ / kSlotsPerTrack] = copyButton_;  // selected: record's double undoes the clear
    }
  } else if (edit_.slot() != copyEditSlot_) {  // another slot's session: the source is not here
    return;
  } else if (copy_ == Copy::Bar) {
    if (edit_.clearRange(copyBar_, ControlSurfaceNoteEdit<Engine>::kBar, ms))
      flash(static_cast<uint8_t>(40 + copyButton_), selected(), ms);
  } else if (copy_ == Copy::Note) {
    EditNote n;
    if (edit_.noteAt(copyNote_, n)) {
      const uint32_t step = n.tick / ControlSurfaceNoteEdit<Engine>::kSixteenth * ControlSurfaceNoteEdit<Engine>::kSixteenth;
      if (edit_.clearRange(step, ControlSurfaceNoteEdit<Engine>::kSixteenth, ms)) flash(copyButton_, selected(), ms);
    }
  }
}

template <typename Engine>
inline auto ControlSurface<Engine>::paste(uint8_t b, uint32_t ms) -> void {
  if (copy_ == Copy::Slot) {
    const uint8_t s = static_cast<uint8_t>(b - kSlot0);
    if (control().copySlot(copySlot_, static_cast<uint8_t>(selected() * kSlotsPerTrack + s), tick_, us_, us()) ==
        X::Result::Ok) {
      flash(static_cast<uint8_t>(kSlotBase + s), selected(), ms);
      slot_[selected()] = s;  // selected: record's double undoes the paste
      endCopy();  // done: the window closes
    }
  } else if (copy_ == Copy::Note) {
    targetPress(static_cast<uint8_t>(b - kStep0), ms);
  }
}

template <typename Engine>
inline auto ControlSurface<Engine>::flash(uint8_t led, uint8_t track, uint32_t ms) -> void {
  flashLed_ = led;
  flashTrack_ = track;
  flashMs_ = ms;
}

template <typename Engine>
inline auto ControlSurface<Engine>::copyLeds(uint32_t ms, uint8_t* want) -> void {
  const uint8_t t = selected();
  if (copy_ != Copy::None) {
    uint8_t led = 0xFF;
    if (copy_ == Copy::Slot && copySlot_ / kSlotsPerTrack == t) led = static_cast<uint8_t>(kSlotBase + copyButton_);
    else if (copy_ == Copy::Bar) led = static_cast<uint8_t>(40 + copyButton_);
    else if (copy_ == Copy::Note) led = copyButton_;
    if (led != 0xFF) want[led] = (ms / 250) % 2 == 0 ? 127 : 8;
  }
  if (flashLed_ != 0xFF) {
    const uint32_t phase = (ms - flashMs_) / kFlashMs;
    if (phase >= 6) flashLed_ = 0xFF;
    else if (flashTrack_ == t) want[flashLed_] = phase % 2 == 0 ? 127 : 0;
  }
}
template <typename Engine>
inline auto ControlSurface<Engine>::copyMsLeft(uint32_t ms) const -> uint32_t {
  if (copy_ == Copy::None) return 0;
  if (copyHolding_) return kCopyIdleMs;
  const int32_t left = static_cast<int32_t>(copyUntil_ - ms);
  return left > 0 ? static_cast<uint32_t>(left) : 0;
}

template <typename Engine>
inline auto ControlSurface<Engine>::applyGroove(uint8_t slot, const groove::Settings& s, const groove::Table* table, const char* label, uint8_t heard) -> typename Engine::Transport::Result {
  const ManifestId c = transport().contentOf(slot);
  if (c == kNoManifest) return Engine::Transport::Result::Empty;
  beyondEnd(c);
  const PlayWindow w = transport().playWindow(slot);
  GrooveRequest q;
  q.settings = s;
  if (s.tmpl != groove::kFileTemplate) {
    q.table = groove::builtin(s.tmpl);
  } else if (table != nullptr) {
    q.table = *table;
    for (uint8_t k = 0; label != nullptr && k < 20 && label[k] != 0; ++k) q.label[k] = label[k];
  } else {  // the state's own (a regroove, AUTO)
    const PlayWindow sw = transport().playWindow(slot);
    if (!readTemplate(e_->runs(), e_->pool(), sw.tableRun, sw.grooveShape, q.table, q.label))
      return Engine::Transport::Result::Refused;
  }
  q.storedTicks = storedTicks_;
  q.start = playStart_;
  if (s.scope == groove::kRangeA) {
    q.scopeStart = w.aStart;
    q.scopeLength = w.aLength;
  } else if (s.scope == groove::kRangeB) {
    q.scopeStart = w.bStart;
    q.scopeLength = w.bLength;
  }
  if (heard != 0) {  // from the GROOVE window: its columns stored with the state
    q.columnA = grooveA_.pack();
    q.columnB = grooveSet_.pack();
    q.heard = heard;
  }
  if (grooveSlot_ != slot) visitState_ = 0;  // a new visit: its first publish is a step
  grooveSlot_ = slot;
  return e_->grooveStart(slot, q);
}

}  // namespace ContentEngine
