//  Copyright (c)  2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

// S7.5a distribution instrument — the two sink rows that carry a window's own distribution.
//
// They live in their own translation unit **on purpose**: their format strings and channel names are ~130 B of new
// string literals, and adding them to `DebugSessionCapture.cpp` pushed that unit's `.rodata` across the DTCM align
// edge in `linker/imxrt1062_t41_lcr.ld` (RAM1 `variables` 96,192 -> 100,288 B, locals 34,880 -> 30,784 B, ITCM code
// unchanged) — the documented 4 KB align snap of `.cursor/rules/Teensy-RAM1-Bank-Overflow.mdc`, which is not a
// reason to re-base the budget. This unit is carved out of DTCM in that script, so its literals stay in
// memory-mapped FLASH and the approved budget holds. The ring itself stays with the sink: these rows are formatted
// here and handed to `DebugSessionCapture::emitEnvelopeRowLine`.
//
// Both emitters are `SC_MEM_ATTR` (FLASHMEM): the sink is called from the window emitter, never from the tick ISR.
#include "Utils/DebugSessionCapture.h"

#ifdef SESSION_CAPTURE

#include <Arduino.h>

#include <cstdio>

namespace DebugSessionCapture {
namespace {

// The sink's own text-line limit (`DebugSessionCapture.cpp`, documented as 192 in this header). A row that would
// not fit is truncated by the sink, exactly as the sink's own formatted rows are.
constexpr size_t kRowBufferBytes = 192;

// The channel code -> name mapping the row carries. A code with no name is dropped rather than emitted under a
// wrong label (see `RuntimeTimingTelemetry::DistChannel`).
const char* distChannelName(uint8_t channel) {
  switch (channel) {
    case 0:
      return "midi_gap";
    case 1:
      return "midi_input";
    case 2:
      return "late_on";
    case 3:
      return "late_off";
    default:
      return nullptr;
  }
}

}  // namespace

// The Tier-A prefixes these rows carry. They are defined at namespace scope, not in the anonymous namespace above:
// the classifier in `DebugSessionCapture.cpp` asks for them (`isTierATextLine`), and the literals must stay in this
// DTCM-carved unit — two more of them in the classifier's own unit crossed the RAM1 align edge (`variables`
// 96,192 -> 100,288 B with ITCM code unchanged).
SC_MEM_ATTR const char* envelopeDistTagPrefix() {
  return "DIAG,env_dist,";
}

SC_MEM_ATTR const char* envelopeRouteTagPrefix() {
  return "DIAG,late_route,";
}

SC_MEM_ATTR void envelopeDistRow(uint8_t channel, uint32_t p50LoUs, uint32_t p50HiUs, uint32_t p99LoUs,
                                 uint32_t p99HiUs, uint32_t samples) {
  const char* name = distChannelName(channel);
  if (name == nullptr) {
    return;
  }
  // The quantiles are **bucket bounds** (`RuntimeTimingTelemetry::kEnvelopeDistBuckets`), so each one is stated as
  // the interval it lies in; the channel's exact maximum stays on its own `late_*`/`midi_*` row.
  char line[kRowBufferBytes];
  const int len = std::snprintf(line, sizeof(line), "#CAP,%lu,DIAG,env_dist,%s,%lu,%lu,%lu,%lu,%lu\r\n",
                                static_cast<unsigned long>(micros()), name, static_cast<unsigned long>(p50LoUs),
                                static_cast<unsigned long>(p50HiUs), static_cast<unsigned long>(p99LoUs),
                                static_cast<unsigned long>(p99HiUs), static_cast<unsigned long>(samples));
  if (len <= 0) {
    return;
  }
  emitEnvelopeRowLine(line);
}

SC_MEM_ATTR void envelopeRouteLatenessRow(uint8_t type, uint8_t ch, uint32_t maxUs, uint32_t overCount,
                                          uint32_t samples, uint32_t spill) {
  // `spill` is the number of samples whose route found no slot in the bounded table, so a split truncated at that
  // bound is visible in every row of it.
  char line[kRowBufferBytes];
  const int len = std::snprintf(line, sizeof(line), "#CAP,%lu,DIAG,late_route,%u,%u,%lu,%lu,%lu,%lu\r\n",
                                static_cast<unsigned long>(micros()), static_cast<unsigned>(type),
                                static_cast<unsigned>(ch), static_cast<unsigned long>(maxUs),
                                static_cast<unsigned long>(overCount), static_cast<unsigned long>(samples),
                                static_cast<unsigned long>(spill));
  if (len <= 0) {
    return;
  }
  emitEnvelopeRowLine(line);
}

}  // namespace DebugSessionCapture

#endif  // SESSION_CAPTURE
