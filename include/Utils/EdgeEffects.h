//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file EdgeEffects.h
 * @brief What the **MIDI tick edge** is allowed to do about a decision it just made: record it, and let the
 *        main loop do the logging, the LED refresh and the display invalidation.
 *
 * Timing-path review, item 1. `TrackManager::updateAllTracks` runs in the `IntervalTimer` ISR when the clock
 * source is internal, and its commit paths called `logger.info` (three sites, through the one shared 128-byte
 * `Logger` format buffer), `displayManager.invalidateForSlotChange` (a cold display-cache walk) and
 * `forceMidiLedUpdate` (a MIDI send) inline -- none of which is part of "advance the playhead and emit the
 * notes that are due". The cleaner partition is the one this header implements:
 *
 *   - the edge **applies** the O(1) state changes it owns -- the commit, the cancel, the active-slot write, the
 *     notes-off, the queued start -- so the switch still lands on its boundary tick; and
 *   - it **records** what a consumer should say or refresh about that decision, in a fixed-size queue the main
 *     loop drains beside the other deferred outbound work (`TrackManager::drainEdgeEffects`, called from
 *     `loop()` next to `processDroidUsbHostOutbound`).
 *
 * The split is by *what the work is*, not by convenience: a log line, an LED refresh and a display invalidation
 * are all work whose ordering relative to the tick does not matter, while the commit's state change is exactly
 * the thing that must stay on the edge.
 *
 * The queue is a single-producer/single-consumer ring with **one position per context** (the same shape, and
 * for the same reason, as `Utils/DroidHostFeedbackRing.h`): the edge can be the tick ISR or the main loop
 * (external clock, MIDI Start), so two contexts can produce, and neither side ever reads-modifies-writes the
 * other's index. A record that does not fit is **counted, never overwritten**, and the drain reports the count
 * -- bounded and named, not silent.
 *
 * Dependency-free on purpose, so a native suite can drive the interleaving and the drain's decisions directly.
 */

#pragma once

#include <cstddef>
#include <cstdint>

/// What happened at the edge. The drain decides how to *say* it, which is why this is a name and not a message.
enum class EdgeEffectKind : uint8_t {
  /// The queued slot switch committed at the outgoing loop's end.
  LoopEndCommit = 0,
  /// The queued switch was cancelled because the target has no committed MIDI (only a metadata length).
  SlotSwitchCancelledNoCommittedMidi = 1,
  /// The queued switch was cancelled because the target no longer has loop data in RAM.
  SlotSwitchCancelledNoRamData = 2,
  /// The multi-hold selection committed (enabled set + active slot).
  MultiSlotCommit = 3,
  /// The "select single slot" gesture replaced the enabled set at that commit.
  EnabledSetReplacement = 4,
};

/// The records' `work` bits: what the drain must *do* beyond the row.
inline constexpr uint8_t kEdgeEffectLog = 1u << 0;
inline constexpr uint8_t kEdgeEffectLedRefresh = 1u << 1;
inline constexpr uint8_t kEdgeEffectDisplayInvalidate = 1u << 2;

/// One recorded edge decision. `fromSlot`/`toSlot` and the geometry are carried rather than re-read at drain
/// time: they are what the row names, and the loop they describe may be replaced before the drain runs.
struct EdgeEffectRecord {
  uint8_t kind = 0;
  uint8_t work = 0;
  uint8_t track = 0;
  uint8_t fromSlot = 0;
  uint8_t toSlot = 0;
  uint8_t reserved[3] = {0, 0, 0};
  uint32_t loopStartTick = 0;
  uint32_t loopLengthTicks = 0;
};

class EdgeEffectQueue {
 public:
  /// 16 records: a slot switch, its enabled-set replacement and a multi-slot commit are the only producers, and
  /// a main loop that has not drained in that long is already a case the drain's own row must name.
  static constexpr uint16_t kCapacity = 16;

  /// Boot only, before any producer/consumer starts. DMAMEM is not cleared by
  /// Teensy startup; equal positions hide retained payload until publication.
  void initializeForBoot() {
    write_ = 0;
    read_ = 0;
    droppedCount_ = 0;
  }

  /// **Producer** step (the tick edge): O(1), no formatting, no MIDI, no display. Returns false when the queue
  /// is full, in which case the caller has lost an effect and `droppedCount()` has grown.
  bool record(const EdgeEffectRecord& effect) {
    uint32_t savedMask = 0;
    maskEnter(savedMask);
    const uint16_t write = write_;
    const uint16_t read = read_;
    if (static_cast<uint16_t>(write - read) >= kCapacity) {
      ++droppedCount_;
      maskExit(savedMask);
      return false;
    }
    records_[static_cast<uint16_t>(write % kCapacity)] = effect;
    publishFence();
    write_ = static_cast<uint16_t>(write + 1u);
    maskExit(savedMask);
    return true;
  }

  /// **Consumer** step (the main loop): the oldest record, in the order the edge produced them.
  bool takeOldest(EdgeEffectRecord& out) {
    if (depth() == 0u) {
      return false;
    }
    out = records_[static_cast<uint16_t>(read_ % kCapacity)];
    read_ = static_cast<uint16_t>(read_ + 1u);
    return true;
  }

  uint16_t depth() const { return static_cast<uint16_t>(write_ - read_); }
  /// Effects the edge could not record. Read once per drain so the loss is reported exactly once per growth.
  uint32_t droppedCount() const { return droppedCount_; }

 private:
  /// The producer's index update is the only shared state either side modifies, and both the tick ISR and the
  /// main loop can be the producer here, so it runs under a mask that *restores* the caller's mask state rather
  /// than unconditionally re-enabling interrupts.
  static void maskEnter(uint32_t& savedMask) {
#if defined(__IMXRT1062__)
    __asm__ volatile("mrs %0, primask" : "=r"(savedMask));
    __asm__ volatile("cpsid i" ::: "memory");
#else
    savedMask = 0u;
#endif
  }
  static void maskExit(uint32_t savedMask) {
#if defined(__IMXRT1062__)
    __asm__ volatile("msr primask, %0" ::"r"(savedMask) : "memory");
#else
    (void)savedMask;
#endif
  }
  /// The record is stored before the position that publishes it; see the same fence in the DROID ring.
  static void publishFence() {
#if defined(__IMXRT1062__)
    __asm__ volatile("dmb" ::: "memory");
#endif
    __asm__ volatile("" ::: "memory");
  }

  EdgeEffectRecord records_[kCapacity];
  volatile uint16_t write_ = 0;  ///< Producer-owned; published after the record's store.
  volatile uint16_t read_ = 0;   ///< Consumer-owned.
  uint32_t droppedCount_ = 0;
};
