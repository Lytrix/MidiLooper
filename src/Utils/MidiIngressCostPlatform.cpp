// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "Utils/MidiIngressCostProbe.h"
#if MIDI_INGRESS_COST_QUALIFICATION && defined(__IMXRT1062__)
#include <Arduino.h>
#include "Utils/ClockPhysicalReferencePlatform.h"
static_assert(F_CPU == 600000000, "Ingress-cost bench image requires fixed600MHz build");
namespace MidiIngress::CostPlatform {
FLASHMEM uint32_t cycles() {
  return ClockPhysicalReferencePlatform::readCycleCounterForObservation();
}
FLASHMEM uint32_t cpuHz() {
  return ClockPhysicalReferencePlatform::cpuClockHzForObservation();
}
FLASHMEM uint32_t configuration() {
  // Enabled sampled endpoints are not proof against unseen reset/halt/rewrite.
  return cpuHz() == 600000000u ? 1u : 0u;
}
}
#endif
