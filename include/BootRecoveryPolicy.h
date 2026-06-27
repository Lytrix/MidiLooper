//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

namespace BootRecoveryPolicy {

struct RevisionRecoveryPlan {
  uint16_t setId = 0;
  uint16_t derivedRevisionId = 0;
  uint16_t latestRevisionId = 0;
};

/// Revision recovery targets after Current workspace load fails (steps 2–3).
RevisionRecoveryPlan buildRevisionRecoveryPlan(uint16_t workspaceDerivedSetId,
                                               uint16_t workspaceDerivedRevisionId,
                                               uint16_t catalogLatestRevisionId);

/// Latest revision to try when exact derived revision fails; 0 when redundant or unavailable.
uint16_t resolveLatestRevisionFallback(uint16_t derivedRevisionId, uint16_t latestRevisionId);

/// Whether a candidate epoch is complete enough to load (no partial successor files).
bool isWorkspaceEpochBootCandidate(uint32_t candidateEpoch, bool runtimeBundleComplete,
                                   bool anySlotEpochAboveCandidate, bool allRequiredSlotsValid);

using EpochCompleteProbe = bool (*)(uint32_t candidateEpoch, void* context);

/// Walk scanStartEpoch downward until probe reports a complete epoch; 0 when none.
uint32_t resolveHighestValidWorkspaceEpoch(uint32_t scanStartEpoch, EpochCompleteProbe probe,
                                           void* context);

}  // namespace BootRecoveryPolicy
