//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Gestures and live input of the content engine (fixed-memory-engine E9.4; owner 2026-11-05: one sounding slot per
// track, layering and jam later, Clear undoable). Surface actions (buttons, MIDI controls) call these; the legacy
// semantics they keep (TrackManager capture queue, slot state machine, transport):
//
// * Timing: a gesture at Clock tick `clock` takes effect before that tick is played; input at `clock` arrives after
//   it. Queued gestures run in `beforeTick`, never on the tick they were queued.
// * Record (queued): on a track that plays, at its slot's next loop start (phase reference); otherwise at the next bar.
//   The track's playing slot stops then and the take begins on the queued slot. While another track writes (one
//   writer) the record stays queued and starts at the first eligible tick after the writer is free.
// * Stop (queued): the take ends at the next bar. `stopNow` ends it at once.
// * Slot switch: the target composes from the request on (`SlotTransport::launch`, fresh from its loop start) and plays
//   from the next grid step (`NextGrid`, 16ths by default) or the playing slot's loop end (`LoopEnd`); a take on the
//   track ends first. An empty target stops the track and takes the slot, ready to record.
// * Clear: an undoable empty state (`SlotTransport::clear`); Undo / Redo per slot.
// * Mute / solo: a track is audible when not muted and either no track is soloed or it is (`ClockOutput::setAudible`).
// * Transport stop: queued gestures dropped, a take ends, every track stops — each one's slot starts composing again
//   from its loop start, so transport start plays it from position 0 at once.
// * Input: notes and events go to the writer when a take runs (live occupancy NoteOffs routed) and through to the
//   ports of the writing track, else of the selected track.
// No allocation.

#pragma once

#include <cstdint>

namespace ContentEngine {

template <typename Transport, typename Output, typename Emit, uint8_t Tracks, uint8_t T>
class TrackControl {
 public:
  using Result = typename Transport::Result;
  enum class Quantize : uint8_t { NextGrid, LoopEnd };
  static constexpr uint8_t kNone = 0xFF;

  struct Stats {
    uint32_t recordsStarted;  // queued Records that began
    uint32_t recordsWaited;   // eligible ticks a queued Record waited for the writer
    uint32_t switches;        // slot switches committed
  };

  void init(Transport& transport, Output& output, uint32_t ticksPerBar, uint32_t gridTicks) {
    x_ = &transport;
    out_ = &output;
    bar_ = ticksPerBar;
    grid_ = gridTicks;
    selected_ = 0;
    for (auto& p : pending_) p = Pending{};
    for (auto& m : muted_) m = false;
    for (auto& s : soloed_) s = false;
    for (auto& w : wasPlaying_) w = false;
    stats_ = Stats{};
  }
  /// The runtime bar and grid (time signature, launch grid).
  void setGrid(uint32_t ticksPerBar, uint32_t gridTicks) {
    bar_ = ticksPerBar;
    grid_ = gridTicks;
  }

  void selectTrack(uint8_t t) { selected_ = t; }
  uint8_t selectedTrack() const { return selected_; }

  // ---- Gestures ------------------------------------------------------------------------------------------------------
  Result queueRecord(uint8_t t, uint8_t slot, uint32_t clock) {
    if (x_->contentOf(slot) != kNoManifest) return Result::DeniedSlot;
    pending_[t].recordSlot = slot;
    pending_[t].recordAt = clock;
    return Result::Ok;
  }
  Result queueStop(uint8_t t, uint32_t clock) {
    if (x_->writingTrack() != t) return Result::NotWriting;
    pending_[t].stop = true;
    pending_[t].stopAt = clock;
    return Result::Ok;
  }
  void cancelQueued(uint8_t t) {
    pending_[t].recordSlot = kNone;
    pending_[t].stop = false;
    if (pending_[t].switchSlot != kNone) x_->stopLaunch(t);
    pending_[t].switchSlot = kNone;
  }
  Result stopNow(uint32_t clock, uint32_t nowUs, uint32_t usPerTick) {
    const uint8_t w = x_->writingTrack();
    if (w == kNone) return Result::NotWriting;
    pending_[w].stop = false;
    Emit buf[T];
    uint8_t n;
    const Result r = x_->stop(clock, buf, T, n);
    out_->route(buf, n, nowUs, usPerTick);
    return r;
  }
  Result startOverdub(uint8_t t, uint32_t clock) { return x_->startOverdub(t, clock); }

  Result queueSwitch(uint8_t t, uint8_t slot, Quantize q, uint32_t clock) {
    Pending& p = pending_[t];
    if (p.switchSlot != kNone) x_->stopLaunch(t);
    p.switchSlot = slot;
    p.switchQ = q;
    p.switchAt = clock;
    if (x_->contentOf(slot) != kNoManifest && x_->writingTrack() != t) x_->launch(t, slot);  // composes from now on
    return Result::Ok;
  }

  Result clear(uint8_t slot, uint32_t clock, uint32_t nowUs, uint32_t usPerTick) {
    Emit buf[Tracks * T];
    uint8_t n;
    const Result r = x_->clear(slot, clock, buf, Tracks * T, n);
    out_->route(buf, n, nowUs, usPerTick);
    return r;
  }
  Result undo(uint8_t slot, uint32_t clock, uint32_t nowUs, uint32_t usPerTick) {
    Emit buf[Tracks * T];
    uint8_t n;
    const Result r = x_->undo(slot, clock, buf, Tracks * T, n);
    out_->route(buf, n, nowUs, usPerTick);
    return r;
  }
  Result redo(uint8_t slot, uint32_t clock, uint32_t nowUs, uint32_t usPerTick) {
    Emit buf[Tracks * T];
    uint8_t n;
    const Result r = x_->redo(slot, clock, buf, Tracks * T, n);
    out_->route(buf, n, nowUs, usPerTick);
    return r;
  }

  void setMute(uint8_t t, bool muted, uint32_t nowUs) {
    muted_[t] = muted;
    applyAudible(nowUs);
  }
  void setSolo(uint8_t t, bool soloed, uint32_t nowUs) {
    soloed_[t] = soloed;
    applyAudible(nowUs);
  }

  void transportStop(uint32_t clock, uint32_t nowUs, uint32_t usPerTick) {
    for (uint8_t t = 0; t < Tracks; ++t) cancelQueued(t);
    if (x_->writingTrack() != kNone) stopNow(clock, nowUs, usPerTick);
    Emit buf[T];
    for (uint8_t t = 0; t < Tracks; ++t) {
      wasPlaying_[t] = x_->playing(t);
      if (!wasPlaying_[t]) continue;
      out_->route(buf, x_->stopTrack(t, clock, buf, T), nowUs, usPerTick);
      x_->launch(t, x_->playingSlot(t));  // ready to start from position 0
    }
  }
  void transportStart(uint32_t clock, uint32_t nowUs, uint32_t usPerTick) {
    Emit buf[T];
    for (uint8_t t = 0; t < Tracks; ++t) {
      if (!wasPlaying_[t]) continue;
      wasPlaying_[t] = false;
      uint8_t n;
      x_->commitLaunch(t, clock, buf, T, n);
      out_->route(buf, n, nowUs, usPerTick);
    }
  }

  // ---- Input (after tick `clock`) ------------------------------------------------------------------------------------
  void inputNoteOn(uint32_t clock, uint8_t status, uint8_t pitch, uint8_t velocity, uint32_t nowUs,
                   uint32_t usPerTick) {
    if (velocity == 0) {
      inputNoteOff(clock, status, pitch, nowUs);
      return;
    }
    if (x_->writingTrack() != kNone) {
      Emit buf[T];
      uint8_t n;
      x_->noteOn(clock, static_cast<uint8_t>((status & 0x0F) + 1), pitch, velocity, buf, T, n);
      out_->route(buf, n, nowUs, usPerTick);
    }
    out_->thruNoteOn(target(), status, pitch, velocity, nowUs);
  }
  void inputNoteOff(uint32_t clock, uint8_t status, uint8_t pitch, uint32_t nowUs) {
    if (x_->writingTrack() != kNone) x_->noteOff(clock, static_cast<uint8_t>((status & 0x0F) + 1), pitch, 0);
    out_->thruNoteOff(target(), status, pitch, nowUs);
  }
  void inputEvent(uint32_t clock, uint8_t status, uint8_t d1, uint8_t d2, uint32_t nowUs) {
    if (x_->writingTrack() != kNone) x_->channelEvent(clock, status, d1, d2);
    out_->thruEvent(target(), status, d1, d2, nowUs);
  }

  // ---- Clock ---------------------------------------------------------------------------------------------------------
  /// The queued gestures due at `clock`, before that tick is played.
  void beforeTick(uint32_t clock, uint32_t nowUs, uint32_t usPerTick) {
    for (uint8_t t = 0; t < Tracks; ++t) {
      Pending& p = pending_[t];
      if (p.stop && clock != p.stopAt && onBar(clock)) {
        p.stop = false;
        stopNow(clock, nowUs, usPerTick);
      }
      if (p.switchSlot != kNone && clock != p.switchAt && switchDue(t, p.switchQ, clock)) commitSwitch(t, clock, nowUs, usPerTick);
      if (p.recordSlot != kNone && clock != p.recordAt && recordDue(t, clock)) startRecord(t, clock, nowUs, usPerTick);
    }
  }

  bool recordQueued(uint8_t t) const { return pending_[t].recordSlot != kNone; }
  bool stopQueued(uint8_t t) const { return pending_[t].stop; }
  bool switchQueued(uint8_t t) const { return pending_[t].switchSlot != kNone; }
  bool muted(uint8_t t) const { return muted_[t]; }
  const Stats& stats() const { return stats_; }

 private:
  struct Pending {
    uint8_t recordSlot = kNone;
    uint32_t recordAt = 0;
    bool stop = false;
    uint32_t stopAt = 0;
    uint8_t switchSlot = kNone;
    Quantize switchQ = Quantize::NextGrid;
    uint32_t switchAt = 0;
  };

  uint8_t target() const {
    const uint8_t w = x_->writingTrack();
    return w != kNone ? w : selected_;
  }
  bool onBar(uint32_t clock) const { return bar_ != 0 && clock % bar_ == 0; }
  /// The track's playing slot is at its loop start (position 0 of a cycle) at `clock`.
  bool atLoopStart(uint8_t t, uint32_t clock) const {
    const ManifestId m = x_->contentOf(x_->playingSlot(t));
    if (!x_->playing(t) || m == kNoManifest) return false;
    const uint32_t loop = x_->loopLength(m);
    return loop != 0 && (clock - x_->origin(t)) % loop == 0;
  }
  bool recordDue(uint8_t t, uint32_t clock) const {
    return x_->playing(t) && x_->contentOf(x_->playingSlot(t)) != kNoManifest ? atLoopStart(t, clock) : onBar(clock);
  }
  bool switchDue(uint8_t t, Quantize q, uint32_t clock) const {
    if (q == Quantize::LoopEnd && x_->playing(t) && x_->contentOf(x_->playingSlot(t)) != kNoManifest)
      return atLoopStart(t, clock);
    return grid_ != 0 && clock % grid_ == 0;
  }

  void startRecord(uint8_t t, uint32_t clock, uint32_t nowUs, uint32_t usPerTick) {
    Pending& p = pending_[t];
    if (x_->writingTrack() != kNone) {  // one writer: wait for it
      ++stats_.recordsWaited;
      return;
    }
    Emit buf[T];
    if (x_->playing(t)) out_->route(buf, x_->stopTrack(t, clock, buf, T), nowUs, usPerTick);
    if (x_->startRecord(t, p.recordSlot, clock) == Result::Ok) {
      p.recordSlot = kNone;
      ++stats_.recordsStarted;
    }
  }
  void commitSwitch(uint8_t t, uint32_t clock, uint32_t nowUs, uint32_t usPerTick) {
    Pending& p = pending_[t];
    const uint8_t slot = p.switchSlot;
    p.switchSlot = kNone;
    if (x_->writingTrack() == t) stopNow(clock, nowUs, usPerTick);  // a take on the track ends first
    Emit buf[T];
    uint8_t n = 0;
    if (x_->contentOf(slot) == kNoManifest) {
      if (x_->playing(t)) n = x_->stopTrack(t, clock, buf, T);
      x_->assign(t, slot);
    } else {
      if (x_->launchingSlot(t) != slot) x_->launch(t, slot);
      x_->commitLaunch(t, clock, buf, T, n);
    }
    out_->route(buf, n, nowUs, usPerTick);
    ++stats_.switches;
  }
  void applyAudible(uint32_t nowUs) {
    bool anySolo = false;
    for (bool s : soloed_) anySolo |= s;
    for (uint8_t t = 0; t < Tracks; ++t) out_->setAudible(t, !muted_[t] && (!anySolo || soloed_[t]), nowUs);
  }

  Transport* x_ = nullptr;
  Output* out_ = nullptr;
  uint32_t bar_ = 768, grid_ = 48;
  uint8_t selected_ = 0;
  Pending pending_[Tracks];
  bool muted_[Tracks] = {};
  bool soloed_[Tracks] = {};
  bool wasPlaying_[Tracks] = {};
  Stats stats_{};
};

}  // namespace ContentEngine
