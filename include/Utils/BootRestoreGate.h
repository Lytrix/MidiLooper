#pragma once

/// **DEC-194 — a root is an accelerator, never a prerequisite.** While the boot restore sweep owns the shared storage
/// session, root recovery yields: measured on device 2026-10-29, letting snapshot construction share the boot window
/// took the sweep from 26 slots in 17.1 s to 3 slots in 21.7 s, with per-slot execution unchanged (0.19 s) and the rest
/// queue wait (3.8-27 s). The root arrives a few seconds later; a restore that never runs leaves a track with no
/// content at all.
inline bool rootRecoveryAllowedDuringBoot(bool restoreSweepPending) { return !restoreSweepPending; }
