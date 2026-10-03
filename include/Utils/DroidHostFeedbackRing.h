//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file DroidHostFeedbackRing.h
 * @brief The DROID control-surface feedback queue: **one position per context**, published in the ring's own
 *        order, with the coalescing rule owned by the drain.
 *
 * Timing-path review, items 2 and 3. The queue this replaces kept `head` and `count` and both sides
 * read-modify-wrote them: the MIDI tick ISR `++count`-ed on every playback event it queued while the main
 * loop `--count`-ed on every event it sent, on plain `uint16_t`s with no atomic and no barrier -- a lost
 * update leaves the count wrong for the rest of the boot. It also scanned up to the whole ring (256 entries)
 * inside the ISR for the fader-state coalesce, ran that scan from a `FLASHMEM` body although the ISR's own
 * call tree must stay in ITCM (`RuntimeTimingTelemetry.cpp` records the USB starvation that rule comes from),
 * and could `logger.log` an overflow *from the ISR*, over one shared 128-byte format buffer.
 *
 * What is here instead, and why each part is the way it is:
 *
 *   - **Monotonic producer/consumer positions.** `write_` is written by the producer alone and `read_` by the
 *     consumer alone, so neither side ever reads-modifies-writes the other's index; depth is their difference.
 *     Two contexts push (the playback ISR queues its events and the main loop queues direct sends), which is
 *     why the producer's own index update is the one thing that needs a mask -- a handful of instructions --
 *     and why the mask saves and restores the caller's mask state instead of unconditionally re-enabling
 *     interrupts the way a bare `noInterrupts()/interrupts()` pair would.
 *   - **An O(1) producer.** One struct store plus one index publish: no scan, no formatting, no logging, so
 *     the ISR's cost does not scale with what is queued. The caller's `coalesceKey` (0 = "always send") is
 *     what makes the drain's work possible without a scan here.
 *   - **The coalescing rule at the consumer**, where the ordered stream is actually built: the *oldest* queued
 *     update for a target is the one that keeps its position, and it is the one that carries the **newest**
 *     value, with the later duplicates in the published region marked superseded and recycled unsent. That is
 *     the rule the push-side scan used to apply ("keep the newest value for the same target while retaining
 *     the position of the first queued update"), now expressed where the queue is drained.
 *
 * Dependency-free (no Arduino, no `MidiConfig`) so a native suite can drive the interleaving directly; the
 * caller supplies the storage, the capacity, `queuedUs` and the coalesce key.
 */

#pragma once

#include <cstddef>
#include <cstdint>

/// One queued outbound DROID host event.
struct DroidHostFeedbackEvent {
  uint8_t type = 0;
  uint8_t channel = 0;
  uint8_t data1 = 0;
  uint8_t data2 = 0;
  int16_t pitchBend = 0;
  /// The caller's target identity for a **state** event (fader feedback), or 0 for a **gesture** (a note, a
  /// program change, LED feedback): equal non-zero keys mean "the same target, so only the newest value is
  /// worth sending". `0` is never coalesced.
  uint8_t coalesceKey = 0;
  /// Consumer-owned: set when a later entry already carries this entry's newest value for its target.
  uint8_t superseded = 0;
  /// Filled by the caller at enqueue (the ring has no clock): the age row's input.
  uint32_t queuedUs = 0;
};

class DroidHostFeedbackRing {
 public:
  /// Attach storage and capacity. Called once at setup, before any push; a ring with no storage refuses
  /// everything rather than corrupting memory.
  void init(DroidHostFeedbackEvent* storage, uint16_t capacity) {
    storage_ = storage;
    capacity_ = capacity;
    write_ = 0;
    read_ = 0;
    overflowCount_ = 0;
    coalescedCount_ = 0;
  }

  /// **Producer** step: O(1), allocation-free, log-free, safe from the ISR and from the main loop.
  /// Returns false when every slot is published and undrained (counted as an overflow, never overwritten:
  /// the newest value can be re-queued by its own owner, an older one cannot).
  bool push(const DroidHostFeedbackEvent& event) {
    uint32_t savedMask = 0;
    maskEnter(savedMask);
    const uint16_t write = write_;
    const uint16_t read = read_;
    if (storage_ == nullptr || capacity_ == 0u || static_cast<uint16_t>(write - read) >= capacity_) {
      ++overflowCount_;
      maskExit(savedMask);
      return false;
    }
    storage_[static_cast<uint16_t>(write % capacity_)] = event;
    publishFence();
    write_ = static_cast<uint16_t>(write + 1u);
    maskExit(savedMask);
    return true;
  }

  /// Published but not yet consumed.
  uint16_t depth() const { return static_cast<uint16_t>(write_ - read_); }
  bool empty() const { return depth() == 0u; }
  uint16_t capacity() const { return capacity_; }
  uint32_t overflowCount() const { return overflowCount_; }
  /// Entries recycled unsent because a later entry carried their target's newest value.
  uint32_t coalescedCount() const { return coalescedCount_; }

  /// Age of the oldest queued entry, 0 when empty. The caller supplies the clock, so the ring stays
  /// dependency-free.
  uint32_t oldestAgeUs(uint32_t nowUs) const {
    if (empty()) {
      return 0u;
    }
    return nowUs - storage_[static_cast<uint16_t>(read_ % capacity_)].queuedUs;
  }

  /// **Consumer** step: recycle the oldest entry if a later published entry already carries its target's
  /// newest value. Returns true when one was recycled. O(1); the O(n) half is `takeOldestForSend`.
  bool recycleSupersededOldest() {
    if (empty()) {
      return false;
    }
    DroidHostFeedbackEvent& oldest = storage_[static_cast<uint16_t>(read_ % capacity_)];
    if (oldest.superseded == 0u) {
      return false;
    }
    oldest = DroidHostFeedbackEvent{};
    read_ = static_cast<uint16_t>(read_ + 1u);
    return true;
  }

  /// **Consumer** step: hand back the oldest entry to send, with a state entry's value replaced by the newest
  /// queued value for the same target (the later duplicates are marked superseded, so they are recycled unsent
  /// when their turn comes). O(depth) for a state entry, O(1) for a gesture; bounded by the ring's capacity.
  /// Returns false when nothing is queued.
  bool takeOldestForSend(DroidHostFeedbackEvent& out) {
    if (empty()) {
      return false;
    }
    DroidHostFeedbackEvent& oldest = storage_[static_cast<uint16_t>(read_ % capacity_)];
    if (oldest.coalesceKey != 0u) {
      const uint16_t queued = depth();
      for (uint16_t offset = 1u; offset < queued; ++offset) {
        DroidHostFeedbackEvent& later = storage_[static_cast<uint16_t>((read_ + offset) % capacity_)];
        if (later.superseded != 0u || later.coalesceKey != oldest.coalesceKey) {
          continue;
        }
        oldest.data2 = later.data2;
        oldest.pitchBend = later.pitchBend;
        later.superseded = 1u;
        ++coalescedCount_;
      }
    }
    out = oldest;
    oldest = DroidHostFeedbackEvent{};
    read_ = static_cast<uint16_t>(read_ + 1u);
    return true;
  }

 private:
  /// The producer's index update is the only shared state either side modifies, and two contexts push into
  /// this ring, so the *reserve-seat-and-publish* pair runs inside a mask. The mask state is restored rather
  /// than unconditionally re-enabled: a push that happens inside a caller's own critical section must not
  /// open it.

  static void maskEnter(uint32_t& savedMask) {
#if defined(__IMXRT1062__)
    // Inline rather than CMSIS's `__get_PRIMASK`/`__disable_irq`: this header stays dependency-free, and the
    // pair is the two instructions those intrinsics are.
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
  /// The event store is written before the position that publishes it. On one core a compiler barrier is the
  /// correctness requirement (the ISR sees the same memory in program order) and the `dmb` is the conservative
  /// half of the Cortex-M7 ordering rules for PSRAM-backed storage. No-ops on the native build.
  static void publishFence() {
#if defined(__IMXRT1062__)
    __asm__ volatile("dmb" ::: "memory");
#endif
    __asm__ volatile("" ::: "memory");
  }

  DroidHostFeedbackEvent* storage_ = nullptr;
  uint16_t capacity_ = 0;
  volatile uint16_t write_ = 0;  ///< Producer-owned; published after the entry's store.
  volatile uint16_t read_ = 0;   ///< Consumer-owned; never touched by the producer.
  uint32_t overflowCount_ = 0;
  uint32_t coalescedCount_ = 0;
};
