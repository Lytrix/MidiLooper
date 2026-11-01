// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

// C5 (DEC-246 amendment 19): a RevisionManifest window read through the committed-window contract of
// `LoopSnapshot::EventWindowCursor`, so the playback view fill and its later phases are the snapshot path's own.
//
// One `step` is one read unit: it either hands out one row the store window job produced or advances the job by one
// row. The accepted coverages are the snapshot cursor's own: at most two bars, inside the loop, never wrapping its end
// (`EventWindowCursor::begin` refuses the same shapes, so the lane never requests one).
//
// Ranks are part of the consumer contract (`RuntimePlaybackBridge` indexes canonical-position masks by them), so the
// numbering is the snapshot's own: pass-major in merge order, then the row's position within its pass
// (`canonicalOrder = origin << 16 | ordinal`, `eventId` the same). The playback order's equal-tick tie therefore still
// resolves earlier pass first, which is DEC-244's run order. Each window is begun against the expected manifest
// identity and revision; a moved manifest ends the read as `Inactive` (named `result()` SourceMoved), never as a
// window of a different identity. The cursor never imports, builds an index or scans: an owed index is `IndexOwed`.

#include <cstdint>

#include "CommittedWindowRowSource.h"
#include "MidiEvent.h"
#include "RevisionStore/RevisionStore.h"

namespace RevisionStore {

template <typename Cap>
class ManifestWindowCursor final : public CommittedWindowRowSource {
 public:
  ManifestWindowCursor(PassEvent* staging, uint32_t capacity) : staging_(staging), capacity_(capacity) {}

  /// Begin reading `[startTick, startTick + lengthTicks)` modulo `loopLengthTicks` from manifest `id`. Answers
  /// `Pending` when the read is under way; otherwise the named reason and the cursor stays idle.
  WindowResult begin(StoreCore<Cap>& store, ManifestId id, uint32_t expectedIdentity, uint32_t expectedRevision,
                     uint32_t startTick, uint32_t lengthTicks, uint32_t loopLengthTicks) {
    cancel();
    if (loopLengthTicks == 0 || lengthTicks == 0 || lengthTicks > LoopSnapshot::EventWindowCursor::kMaximumWindowTicks ||
        startTick >= loopLengthTicks || lengthTicks > loopLengthTicks - startTick) {
      return result_ = WindowResult::CapacityDenied;  // not a committed window shape
    }
    store_ = &store;
    manifest_ = id;
    identity_ = expectedIdentity;
    revision_ = expectedRevision;
    start_ = startTick;
    length_ = lengthTicks;
    loopLength_ = loopLengthTicks;
    produced_ = 0;
    handed_ = 0;
    readUnits_ = 0;
    for (uint16_t& n : perOrigin_) n = 0;
    ++traversalGeneration_;
    jobDone_ = false;
    result_ = store.beginWindow(id, expectedIdentity, expectedRevision, startTick, startTick + lengthTicks, staging_,
                                capacity_);
    if (result_ != WindowResult::Pending) {
      phase_ = Phase::Idle;
      return result_;
    }
    phase_ = Phase::Reading;
    return result_;
  }

  /// End the read and release the manifest pin. Idempotent.
  void cancel() {
    if (phase_ == Phase::Reading && store_ != nullptr && store_->windowActive()) store_->cancelWindow();
    if (phase_ != Phase::Idle) ++traversalGeneration_;
    phase_ = Phase::Idle;
  }

  bool idle() const override { return phase_ == Phase::Idle; }
  bool complete() const override { return phase_ == Phase::Complete; }
  bool retiring() const override { return false; }  // nothing to release in steps: the pin ends with the job
  uint64_t traversalGeneration() const override { return traversalGeneration_; }
  uint32_t startTick() const override { return start_; }
  uint32_t lengthTicks() const override { return length_; }
  uint32_t loopLengthTicks() const override { return loopLength_; }
  size_t readUnits() const override { return readUnits_; }
  /// The named reason a read ended without `Complete` (Pending while reading, Ready once complete).
  WindowResult result() const { return result_; }
  /// The manifest identity and revision every row of this read came from (stamped by the store).
  uint32_t identity() const { return identity_; }
  uint32_t revision() const { return revision_; }

  ReadResult step(LoopSnapshotEvent& out) override {
    if (phase_ != Phase::Reading) return phase_ == Phase::Complete ? ReadResult::Complete : ReadResult::Inactive;
    ++readUnits_;
    if (handed_ < produced_) {
      const PassEvent& row = staging_[handed_];
      out.loopId = kInvalidLoopId;
      const uint32_t key = (static_cast<uint32_t>(row.origin) << 16) | perOrigin_[row.origin]++;
      out.canonicalOrder = key;
      out.eventId = key;
      out.value = toMidi(row);
      ++handed_;
      return ReadResult::Event;
    }
    if (jobDone_) {
      phase_ = Phase::Complete;
      result_ = WindowResult::Ready;
      return ReadResult::Complete;
    }
    const WindowResult r = store_->stepWindow(1);
    produced_ = store_->windowStamp().count;
    if (r == WindowResult::Ready) {
      if (store_->windowStamp().identity != identity_ || store_->windowStamp().revision != revision_) {
        result_ = WindowResult::SourceMoved;
        return fail();
      }
      jobDone_ = true;
    } else if (r != WindowResult::Pending) {
      result_ = r;
      return fail();
    }
    return ReadResult::Progress;
  }

 private:
  enum class Phase : uint8_t { Idle, Reading, Complete };

  ReadResult fail() {
    if (store_ != nullptr && store_->windowActive()) store_->cancelWindow();
    phase_ = Phase::Idle;
    ++traversalGeneration_;
    return ReadResult::Inactive;
  }

  static MidiEvent toMidi(const PassEvent& p) {
    MidiEvent e;
    e.tick = p.tick;
    e.type = static_cast<decltype(e.type)>(p.status & 0xF0);
    e.channel = static_cast<uint8_t>((p.status & 0x0F) + 1);
    e.data.noteData.note = p.data1;
    e.data.noteData.velocity = p.data2;
    e.noteId = p.noteId;
    return e;
  }

  PassEvent* staging_;
  uint32_t capacity_;
  StoreCore<Cap>* store_ = nullptr;
  ManifestId manifest_ = kNoManifest;
  uint32_t identity_ = 0;
  uint32_t revision_ = 0;
  uint32_t start_ = 0;
  uint32_t length_ = 0;
  uint32_t loopLength_ = 0;
  bool jobDone_ = false;
  uint32_t produced_ = 0;
  uint32_t handed_ = 0;
  uint16_t perOrigin_[256] = {};  // rows handed out per origin pass: the within-pass ordinal
  size_t readUnits_ = 0;
  uint64_t traversalGeneration_ = 0;
  Phase phase_ = Phase::Idle;
  WindowResult result_ = WindowResult::Idle;
};

}  // namespace RevisionStore
