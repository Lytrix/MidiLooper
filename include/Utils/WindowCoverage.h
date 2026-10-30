//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

/// **Coverage currency** for a window-scoped derived view (one rule, two consumers).
///
/// `realtime-idle-owner-admission-bound` design D3/D4: an established view may remain authoritative for
/// overlap evaluation **only while its coverage still contains the interval currently required**. Once
/// the requested interval moves outside that coverage the view must be refreshed — stale geometry is
/// never served as though it were current, and a deferral is only legitimate while coverage holds.
///
/// Both the owner-admission contract (`MidiServiceDrain::idleOwnerSourceViewCovers`) and the overdub
/// source cache (`Loop::overdubSourceSpanCacheCoversWindow`) answer with this function, so the rule
/// cannot drift between the scheduler and the content owner.
namespace WindowCoverage {

/// True when the loop-relative interval `[coveredStartTick, +coveredLengthTicks)` contains
/// `[requiredStartTick, +requiredLengthTicks)`. Both intervals may wrap the loop; the covered offset is
/// taken modulo the loop, so a window that straddles the loop end is covered by a window that straddles
/// it too. A zero required length is always covered; a zero covered length covers nothing.
inline bool windowCoversWindow(uint32_t coveredStartTick, uint32_t coveredLengthTicks,
                               uint32_t requiredStartTick, uint32_t requiredLengthTicks,
                               uint32_t loopLengthTicks) {
  if (loopLengthTicks == 0) {
    return false;
  }
  if (requiredLengthTicks == 0) {
    return true;
  }
  if (coveredLengthTicks >= loopLengthTicks) {
    return true;
  }
  if (coveredLengthTicks == 0 || requiredLengthTicks >= loopLengthTicks) {
    return false;
  }
  const uint32_t offset =
      (requiredStartTick + loopLengthTicks - (coveredStartTick % loopLengthTicks)) % loopLengthTicks;
  return offset + requiredLengthTicks <= coveredLengthTicks;
}

}  // namespace WindowCoverage
