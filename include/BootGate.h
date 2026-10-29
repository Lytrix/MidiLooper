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
// DEC-196 — **the release also waits for the playback set.** DEC-184 released at the *focused* slot alone, so the
// panel became interactive while seven of the eight tracks still had no content; a Play pressed there played the
// focused track and nothing else (`captures/session_20261029_113133.log`: Play at 11.67 s, one slot restored, the
// remaining restores then blocked in `waiting_commit_gate` because the transport was already running). The hold now
// also requires that no track still owes the restore of the slot a first Play sounds; the whole-queue drain stays
// the fallback, so an empty or unrecoverable set still releases.

#ifndef BOOT_GATE_H
#define BOOT_GATE_H

namespace BootGate {

/// True when the selected slot has content and no load work for it is outstanding: the panel can show it.
/// An empty selected slot never settles, so such a boot keeps the original whole-queue contract below.
inline bool focusSlotSettled(bool focusHasContent, bool focusRestoreWorkOwed,
                             bool slotLoadSessionActive, bool loadLoopJobActive) {
  return focusHasContent && !focusRestoreWorkOwed && !slotLoadSessionActive && !loadLoopJobActive;
}

/// DEC-196: the panel's slot is settled **and** no track is still owed its playback slot restore, so a Play pressed
/// from this point sounds every track that has content. `playbackSetRestoreWorkOwed` is the storage owner's answer
/// for one slot per track (active, i.e. selected while stopped); an empty slot is never owed and so never holds.
inline bool playbackSetSettled(bool focusSettled, bool playbackSetRestoreWorkOwed) {
  return focusSettled && !playbackSetRestoreWorkOwed;
}

/// The hold ends on either condition: the whole queue drained (original contract, and the only release for a
/// boot whose playback set is empty or cannot be restored), or the playback set settled (DEC-184 + DEC-196).
inline bool bootTitleMayRelease(bool wholeQueueReady, bool playbackSetSettled) {
  return wholeQueueReady || playbackSetSettled;
}

/// DEC-210: the background restore sweep re-dirties one cache per restored slot and competes with the
/// VisualCache owner. It may start only once the focused slot paints **and** every playback-set slot has
/// its first paint window prepared, so a track switched to while PLAYING paints its prewarmed notes
/// instead of waiting for the idle lane. The bound keeps an unprepared slot from holding the sweep.
inline bool bootSweepMayRun(bool focusCacheHasNotes, bool playbackSetVisualsReady,
                            bool boundReached) {
  return (focusCacheHasNotes && playbackSetVisualsReady) || boundReached;
}

}  // namespace BootGate

#endif  // BOOT_GATE_H
