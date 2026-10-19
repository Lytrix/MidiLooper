// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once
#include "Utils/MidiIngressHooks.h"
#if MIDI_INGRESS_COST_QUALIFICATION
#include "Utils/MidiIngressCost.h"
namespace MidiIngress {
// Hardware boundaries only. Native qualification supplies deterministic clocks.
namespace CostPlatform {
uint32_t cycles();
uint32_t cpuHz();
uint32_t configuration();
}
// One coherent fixed-size read-only snapshot; false before cold provisioning.
bool snapshotCosts(IngressCostAggregate (&result)[3]);
// Bits: 1 current600MHz/DWT enabled;2 Ready physical source;4 conditional
// Run-mode/no unseen interruption premise (ASSUMED, not endpoint-proven).
uint32_t costContractBits();
}
#endif
