//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// DEC-184 — **boot title release at focus commit**.
//
// The boot title/USB hold exists so the panel does not become interactive before the set is restorable. Until
// DEC-184 it ended only when the *whole* boot restore queue was empty (`StorageManager::bootInteractiveReady()`),
// which put the first paint behind every queued restore. On device that is 4 s of black panel after the slot the
// user is looking at is already committed (`captures/session_20261028_214829_paintgate5.log`: selected slot
// committed 6.85 s, title released 10.89 s, restore queue 24 -> 0 in between).
//
// The hold now also ends when the **selected** slot is settled; the remaining restores keep draining behind
// DEC-182's post-boot sweep gate, which is the path that paints first. The predicate is deliberately
// conservative — "settled" means content present and nothing mid-flight — so the release never lands inside the
// commit it is releasing on.

#ifndef BOOT_GATE_H
#define BOOT_GATE_H

namespace BootGate {

/// True when the selected slot has content and no load work for it is outstanding: the panel can show it.
/// An empty selected slot never settles, so such a boot keeps the original whole-queue contract below.
inline bool focusSlotSettled(bool focusHasContent, bool focusRestoreWorkOwed,
                             bool slotLoadSessionActive, bool loadLoopJobActive) {
  return focusHasContent && !focusRestoreWorkOwed && !slotLoadSessionActive && !loadLoopJobActive;
}

/// The hold ends on either condition: the whole queue drained (original contract, and the only release for a
/// boot whose selected slot is empty), or the selected slot settled (DEC-184).
inline bool bootTitleMayRelease(bool wholeQueueReady, bool focusSettled) {
  return wholeQueueReady || focusSettled;
}

}  // namespace BootGate

#endif  // BOOT_GATE_H
