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
// * Stop (queued): the take ends at the next bar. A Record take stopped at most half a bar after a bar line ends at that
//   line instead (E9.8i, legacy `RecordStopLength::quantizeTransportRecordLength`): it stops on the next tick, its loop
//   one bar shorter than the next bar would make it, the notes played since kept as the loop's wrap stream
//   (`SlotTransport::stop`). `stopNow` ends a take at once.
// * Pre-roll (E9.8j, owner 2026-11-06: with an external clock the first note was missed; the note-off wrap in
//   reverse): while no take writes, the last `kHeardNotes` live notes are kept with their arrival time. When a Record
//   take starts on the track: a note that arrived within half a tick of tick 0 is recorded at tick 0, as is one held
//   since longer than `kPreRollTicks` (legacy armed pre-roll); one that began 1..`kPreRollTicks` ticks before tick 0 is
//   a pickup — its release is followed during the take (or a new NoteOn on its pitch ends it), and Stop writes it
//   before the loop's end (`SlotTransport::stop`). A note released before tick 0 whose start was out of reach is not
//   recorded.
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
    uint32_t pickups;         // notes begun before a take's tick 0, written at its Stop (E9.8j)
    uint32_t preRollAtStart;  // notes held at a take's start, recorded at its tick 0
  };

  void init(Transport& transport, Output& output, uint32_t ticksPerBar, uint32_t gridTicks) {
    x_ = &transport;
    out_ = &output;
    bar_ = ticksPerBar;
    grid_ = gridTicks;
    x_->setBarTicks(bar_);
    selected_ = 0;
    for (auto& p : pending_) p = Pending{};
    for (auto& m : muted_) m = false;
    for (auto& s : soloed_) s = false;
    for (auto& w : wasPlaying_) w = false;
    for (auto& h : heard_) h = Heard{};
    pickupCount_ = 0;
    pickupTrack_ = kNone;
    startCount_ = 0;
    stats_ = Stats{};
  }
  /// The runtime bar and grid (time signature, launch grid).
  void setGrid(uint32_t ticksPerBar, uint32_t gridTicks) {
    bar_ = ticksPerBar;
    grid_ = gridTicks;
    x_->setBarTicks(bar_);
  }

  /// E9.8j: how long before a Record take's tick 0 a note may begin and still be recorded (a 16th at 192 PPQN).
  static constexpr uint32_t kPreRollTicks = 48;
  static constexpr uint8_t kHeardNotes = 16;

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
    Pending& p = pending_[t];
    p.stop = true;
    p.stopAt = clock;
    p.trimEnd = 0;
    if (x_->mode() == Transport::Mode::Recording && bar_ != 0) {  // E9.8i: half a bar past a bar line ends there
      const uint32_t into = clock % bar_, start = clock - x_->writePosition(clock);
      if (into != 0 && into <= bar_ / 2 && clock - into > start) p.trimEnd = clock - into;
    }
    return Result::Ok;
  }
  void cancelQueued(uint8_t t) {
    pending_[t].recordSlot = kNone;
    pending_[t].stop = false;
    pending_[t].trimEnd = 0;
    if (pending_[t].switchSlot != kNone) x_->stopLaunch(t);
    pending_[t].switchSlot = kNone;
  }
  /// `trimEnd` (a Record take; 0: none): the loop ends at that earlier bar line (E9.8i).
  Result stopNow(uint32_t clock, uint32_t nowUs, uint32_t usPerTick, uint32_t trimEnd = 0) {
    const uint8_t w = x_->writingTrack();
    if (w == kNone) return Result::NotWriting;
    pending_[w].stop = false;
    pending_[w].trimEnd = 0;
    const uint32_t start = clock - x_->writePosition(clock);
    const bool pickups = x_->mode() == Transport::Mode::Recording && pickupTrack_ == w;
    if (pickups)  // still held at Stop: it ends there, as the take's own notes do
      for (uint8_t i = 0; i < pickupCount_; ++i)
        if (pickupOpen_[i]) pickups_[i].off = static_cast<int32_t>(clock - start);
    Emit buf[T];
    uint8_t n;
    const Result r = x_->stop(clock, buf, T, n, trimEnd > start && trimEnd < clock ? trimEnd - start : 0,
                              pickups ? pickups_ : nullptr, pickups ? pickupCount_ : 0);
    pickupCount_ = 0;
    pickupTrack_ = kNone;
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
    const uint8_t channel = static_cast<uint8_t>((status & 0x0F) + 1);
    if (x_->writingTrack() != kNone) {
      endPickup(clock, channel, pitch);  // a new note on a pickup's pitch ends the pickup
      Emit buf[T];
      uint8_t n;
      x_->noteOn(clock, channel, pitch, velocity, buf, T, n);
      out_->route(buf, n, nowUs, usPerTick);
    } else {
      hear(target(), channel, pitch, velocity, nowUs);
    }
    out_->thruNoteOn(target(), status, pitch, velocity, nowUs);
  }
  void inputNoteOff(uint32_t clock, uint8_t status, uint8_t pitch, uint32_t nowUs) {
    const uint8_t channel = static_cast<uint8_t>((status & 0x0F) + 1);
    if (x_->writingTrack() != kNone) {
      if (!endPickup(clock, channel, pitch)) x_->noteOff(clock, channel, pitch, 0);
    } else {
      for (Heard& h : heard_)
        if (h.used && h.held && h.track == target() && h.channel == channel && h.pitch == pitch) {
          h.held = false;
          h.offUs = nowUs;
        }
    }
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
      if (p.stop && clock != p.stopAt && (p.trimEnd != 0 || onBar(clock))) {
        p.stop = false;
        stopNow(clock, nowUs, usPerTick, p.trimEnd);
      }
      if (p.switchSlot != kNone && clock != p.switchAt && switchDue(t, p.switchQ, clock)) commitSwitch(t, clock, nowUs, usPerTick);
      if (p.recordSlot != kNone && clock != p.recordAt && recordDue(t, clock)) startRecord(t, clock, nowUs, usPerTick);
    }
  }

  /// E9.8b (the surface): the track stops at once, its queued gestures dropped.
  void stopTrack(uint8_t t, uint32_t clock, uint32_t nowUs, uint32_t usPerTick) {
    cancelQueued(t);
    if (!x_->playing(t)) return;
    Emit buf[T];
    out_->route(buf, x_->stopTrack(t, clock, buf, T), nowUs, usPerTick);
  }
  /// E9.8b: the track's queued Record starts now (a second press on an armed slot punches in), as at its boundary —
  /// the one writer still waits. DeniedSlot: none queued.
  Result punchIn(uint8_t t, uint32_t clock, uint32_t nowUs, uint32_t usPerTick) {
    if (pending_[t].recordSlot == kNone) return Result::DeniedSlot;
    startRecord(t, clock, nowUs, usPerTick);
    return x_->writingTrack() == t ? Result::Ok : Result::Busy;
  }
  uint8_t queuedRecordSlot(uint8_t t) const { return pending_[t].recordSlot; }
  uint8_t queuedSwitchSlot(uint8_t t) const { return pending_[t].switchSlot; }
  bool soloed(uint8_t t) const { return soloed_[t]; }
  bool recordQueued(uint8_t t) const { return pending_[t].recordSlot != kNone; }
  bool stopQueued(uint8_t t) const { return pending_[t].stop; }
  bool switchQueued(uint8_t t) const { return pending_[t].switchSlot != kNone; }
  bool muted(uint8_t t) const { return muted_[t]; }
  /// E9.8j, for the display: the running Record take's pre-roll — notes recorded at its tick 0 (pitch, velocity) and
  /// its pickups (held ones `pickupOpen`).
  uint8_t pickupTrack() const { return pickupTrack_; }
  uint8_t startNoteCount() const { return startCount_; }
  uint8_t startNotePitch(uint8_t i) const { return startNotes_[i].pitch; }
  uint8_t startNoteVelocity(uint8_t i) const { return startNotes_[i].velocity; }
  uint8_t pickupCount() const { return pickupCount_; }
  const typename Transport::Pickup& pickup(uint8_t i) const { return pickups_[i]; }
  bool pickupOpen(uint8_t i) const { return pickupOpen_[i]; }
  const Stats& stats() const { return stats_; }

 private:
  struct Pending {
    uint8_t recordSlot = kNone;
    uint32_t recordAt = 0;
    bool stop = false;
    uint32_t stopAt = 0;
    uint32_t trimEnd = 0;  // a queued Record Stop that ends the loop at this earlier bar line (E9.8i; 0: none)
    uint8_t switchSlot = kNone;
    Quantize switchQ = Quantize::NextGrid;
    uint32_t switchAt = 0;
  };
  struct Heard {  // a live note while no take writes (E9.8j)
    uint32_t onUs, offUs;
    uint8_t track, channel, pitch, velocity;
    bool used, held;
  };
  Heard heard_[kHeardNotes] = {};
  typename Transport::Pickup pickups_[Transport::kMaxPickups] = {};
  bool pickupOpen_[Transport::kMaxPickups] = {};
  uint8_t pickupCount_ = 0, pickupTrack_ = kNone;
  struct StartNote {
    uint8_t pitch, velocity;
  };
  StartNote startNotes_[kHeardNotes] = {};
  uint8_t startCount_ = 0;

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
      gatherPreRoll(t, clock, nowUs, usPerTick);
    }
  }
  /// E9.8j: the notes heard on `t` before its take's tick 0 (`clock`, at `nowUs`).
  void gatherPreRoll(uint8_t t, uint32_t clock, uint32_t nowUs, uint32_t usPerTick) {
    pickupCount_ = 0;
    startCount_ = 0;
    pickupTrack_ = t;
    const uint32_t tickUs = usPerTick != 0 ? usPerTick : 1;
    for (Heard& h : heard_) {
      if (!h.used || h.track != t) continue;
      h.used = false;
      const uint32_t before = nowUs - h.onUs > 0x7FFFFFFFu ? 0 : (nowUs - h.onUs + tickUs / 2) / tickUs;
      if (h.held && (before == 0 || before > kPreRollTicks)) {  // at tick 0: just before it, or held since long before
        Emit buf[T];
        uint8_t n;
        if (x_->noteOn(clock, h.channel, h.pitch, h.velocity, buf, T, n) == Result::Ok) {
          ++stats_.preRollAtStart;
          if (startCount_ < kHeardNotes) startNotes_[startCount_++] = {h.pitch, h.velocity};
        }
        continue;
      }
      if (before == 0 || before > kPreRollTicks || pickupCount_ >= Transport::kMaxPickups) continue;
      const uint32_t length = h.held ? 0 : (h.offUs - h.onUs + tickUs / 2) / tickUs;
      pickups_[pickupCount_] = {before, h.held ? 0 : static_cast<int32_t>(length) - static_cast<int32_t>(before),
                                h.channel, h.pitch, h.velocity};
      pickupOpen_[pickupCount_++] = h.held;
      ++stats_.pickups;
    }
  }
  /// While no take writes: a live note on `track` (the oldest kept one makes room).
  void hear(uint8_t track, uint8_t channel, uint8_t pitch, uint8_t velocity, uint32_t nowUs) {
    Heard* slot = nullptr;
    for (Heard& h : heard_) {
      if (h.used && h.held && h.track == track && h.channel == channel && h.pitch == pitch) {
        h.held = false;  // struck again: the earlier one ends
        h.offUs = nowUs;
      }
      if (!h.used) {
        if (slot == nullptr || slot->used) slot = &h;
      } else if (slot == nullptr || (slot->used && static_cast<int32_t>(h.onUs - slot->onUs) < 0)) {
        slot = &h;
      }
    }
    *slot = Heard{nowUs, nowUs, track, channel, pitch, velocity, true, true};
  }
  /// A pickup still held on the take's track ends at `clock` (input after that tick). False: none.
  bool endPickup(uint32_t clock, uint8_t channel, uint8_t pitch) {
    if (pickupTrack_ != x_->writingTrack()) return false;
    const uint32_t start = clock - x_->writePosition(clock);
    for (uint8_t i = 0; i < pickupCount_; ++i)
      if (pickupOpen_[i] && pickups_[i].channel == channel && pickups_[i].pitch == pitch) {
        pickupOpen_[i] = false;
        pickups_[i].off = static_cast<int32_t>(clock - start);
        return true;
      }
    return false;
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
