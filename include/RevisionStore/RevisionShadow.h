//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
#pragma once

// DEC-246 Stage C (C2b + C3a): the firmware-resident shadow RevisionStore. Built only with REVISION_STORE_PROBE=1.
//
// - One store, pool and tables ~0.47 MiB, placed in PSRAM once at boot (`initAtBoot`); a refusal is named and the
//   shadow stays off. No allocation after that.
// - C3a: while the transport is STOPPED, the selected track's active slot is imported (`RevisionStore::importLoop`)
//   whenever its frozen source identity (track, slot, loopId, playbackRevision, committed-pass revision) differs from
//   the imported one. While playing nothing is imported; the shadow stays at the identity it recorded (stepwise
//   import under transport is C3b).
// - C2b: the edit-index worker is the registered idle-owner child `RevisionEditIndexGrain`, one call site
//   (`serviceIdleTurn`), bounded by `kRevisionEditIndexMaxUs`; nothing builds an index from playback, display or tick.
// - The shadow is read-only with respect to production: it never changes any production owner.

#include <cstdint>

namespace RevisionShadow {

void initAtBoot();

/// One deferred idle-owner opportunity (the Snapshot owner turn). `timingCritical` follows the turn's regime.
void serviceIdleTurn(bool timingCritical, bool transportRunning);

/// Stopped-only `!REVSHADOW_STATUS`: import identity/result/timing, grain statistics, and a parity check of the
/// shadow's `IndexOnly` `fullRebuildSnapshotCompat()` view against production's effective view of the same slot.
void printStatus();

/// Stopped-only `!REVSHADOW_WINDOWS` (C4): compare each produced window's digest (labelled with the identity it read)
/// with production's passes-derived effective view of the same interval.
void printWindows();

}  // namespace RevisionShadow
