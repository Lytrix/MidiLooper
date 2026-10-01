//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

/// S4b.7 (Rev 20.3/21.7): the **bounded hold** on a pending slot switch.
///
/// A switch's inbound occupancy is taken by the off-clock `SlotProvision` turn, and the commit then waits for
/// that turn plus its own readiness term. Both waits are legitimate — a predecessor's finalization retires
/// first, and a long (demoted) loop's merged window is gathered from its published root off-clock — but
/// neither may wait forever: the contract gives this hold an **age/bound** and a named terminal, so a switch
/// that is still pending past the bound is *reported* rather than silently parked.
///
/// The terminal is a **report, not a decision**: the pending switch and whatever retains it (the accepted
/// interaction latch, the enabled-set replacement, the retained record start) stay exactly as they were, so a
/// later turn can still satisfy the switch, and DEC-078 item 1's "the user never re-issues" still holds.
///
/// The bound is expressed in bars because the firmware, the fixtures and the device share one tick domain;
/// the caller converts it with `Config::TICKS_PER_BAR`.
namespace SlotProvisionHold {

/// Four bars. Comfortably longer than either legitimate wait this holds for — the predecessor finalization
/// window (~1.2 s measured on device) and one off-clock merged-window build — and short enough that an
/// operator sees a report rather than having to wonder whether the press was taken.
constexpr uint8_t kBoundBars = 4;

/// Named terminal, reported once per episode (per track, per pending switch).
constexpr const char* kTerminalName = "slot_switch_hold_exceeded";

/// Wrap-safe age test. A switch with no queued tick (`UINT32_MAX`, the state machine's "no request" sentinel)
/// never fires, and a zero bound means "unbounded" rather than "immediately".
inline bool holdExceeded(uint32_t nowTick, uint32_t queuedAtTick, uint32_t boundTicks) {
  if (boundTicks == 0 || queuedAtTick == UINT32_MAX) {
    return false;
  }
  return static_cast<uint32_t>(nowTick - queuedAtTick) >= boundTicks;
}

/// The bound in ticks for a tick domain whose bar is `ticksPerBar`.
inline uint32_t boundTicksFor(uint32_t ticksPerBar) {
  return static_cast<uint32_t>(kBoundBars) * ticksPerBar;
}

#if defined(PIO_UNIT_TEST_NATIVE)
/// Host seam. On the device the report is an operator row; this is the same fact, recorded, so a fixture can
/// assert that it fired exactly once per episode and named the switch it fired for.
struct ReportRecord {
  uint32_t count = 0;
  uint8_t trackIndex = 0xFF;
  uint8_t slotIndex = 0xFF;
  uint32_t ageTicks = 0;
};

inline ReportRecord& reportRecordForTest() {
  static ReportRecord record;
  return record;
}

inline void resetReportRecordForTest() { reportRecordForTest() = ReportRecord{}; }

inline void recordReportForTest(uint8_t trackIndex, uint8_t slotIndex, uint32_t ageTicks) {
  ReportRecord& record = reportRecordForTest();
  ++record.count;
  record.trackIndex = trackIndex;
  record.slotIndex = slotIndex;
  record.ageTicks = ageTicks;
}
#endif

}  // namespace SlotProvisionHold
