//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "BootRecoveryPolicy.h"

namespace BootRecoveryPolicy {

RevisionRecoveryPlan buildRevisionRecoveryPlan(uint16_t workspaceDerivedSetId,
                                               uint16_t workspaceDerivedRevisionId,
                                               uint16_t catalogLatestRevisionId) {
  RevisionRecoveryPlan plan{};
  if (workspaceDerivedSetId == 0) {
    return plan;
  }
  plan.setId = workspaceDerivedSetId;
  if (workspaceDerivedRevisionId != 0) {
    plan.derivedRevisionId = workspaceDerivedRevisionId;
  }
  if (catalogLatestRevisionId != 0) {
    plan.latestRevisionId = catalogLatestRevisionId;
  }
  return plan;
}

uint16_t resolveLatestRevisionFallback(uint16_t derivedRevisionId, uint16_t latestRevisionId) {
  if (latestRevisionId == 0) {
    return 0;
  }
  if (derivedRevisionId == 0) {
    return latestRevisionId;
  }
  if (latestRevisionId == derivedRevisionId) {
    return 0;
  }
  return latestRevisionId;
}

bool isWorkspaceEpochBootCandidate(uint32_t candidateEpoch, bool runtimeBundleComplete,
                                   bool anySlotEpochAboveCandidate, bool allRequiredSlotsValid) {
  if (candidateEpoch == 0) {
    return false;
  }
  if (anySlotEpochAboveCandidate) {
    return false;
  }
  return runtimeBundleComplete && allRequiredSlotsValid;
}

uint32_t resolveHighestValidWorkspaceEpoch(uint32_t scanStartEpoch, EpochCompleteProbe probe,
                                           void* context) {
  if (scanStartEpoch == 0 || probe == nullptr) {
    return 0;
  }
  for (uint32_t candidate = scanStartEpoch; candidate > 0; --candidate) {
    if (probe(candidate, context)) {
      return candidate;
    }
  }
  return 0;
}

}  // namespace BootRecoveryPolicy
