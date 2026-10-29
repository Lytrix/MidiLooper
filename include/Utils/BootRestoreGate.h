#pragma once

/// **DEC-194 — a root is an accelerator, never a prerequisite.** While the boot restore sweep owns the shared storage
/// session, root recovery yields: measured on device 2026-10-29, letting snapshot construction share the boot window
/// took the sweep from 26 slots in 17.1 s to 3 slots in 21.7 s, with per-slot execution unchanged (0.19 s) and the rest
/// queue wait (3.8-27 s). The root arrives a few seconds later; a restore that never runs leaves a track with no
/// content at all.
inline bool rootRecoveryAllowedDuringBoot(bool restoreSweepPending) { return !restoreSweepPending; }

/// **DEC-194 — boot drain cadence: more opportunities, same quantum.** While the boot title drain is active and a
/// restore is owed, one bounded load slice may run on **every** main-loop frame instead of once per deferred-owner
/// turn. The 5 ms slice itself is unchanged: the measured defect was 38 cheap slices spread over ~3 s because owner
/// turns are sparse (device 2026-10-29: ~0.19 s of work per slot, ~3 s of wall time). Widening the slice was tried and
/// harmed playback prewarm; widening *opportunities* does not change any single interruption's bound.
inline bool bootLoadAdmitsEveryFrame(bool bootTitleDrainActive, bool loadOwed) {
  return bootTitleDrainActive && loadOwed;
}

/// Exactly one load/display service per frame: the boot exception adds frames, never a second service in one frame.
/// When the normal deferred owner is already `LoadAndDisplay`, the boot hook and the owner describe the same single
/// call, so the union is a boolean, not a count.
inline bool bootLoadServicesThisFrame(bool bootExceptionApplies, bool ownerIsLoadAndDisplay) {
  return bootExceptionApplies || ownerIsLoadAndDisplay;
}
