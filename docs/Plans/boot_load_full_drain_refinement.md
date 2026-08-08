# Boot load full drain — refinement

**Kind:** refinement  
**Date:** 2026-07-18  
**Status:** In tree — device gate pending

## Goal

Keep OSTINATIX title until every queued loop slot is Published, then USB + piano roll. Cut wall time by draining the **entire** restore queue in one stretch under the title instead of one slot per `loop()`.

## Baseline

[`session_20260718_015532.log`](../../captures/session_20260718_015532.log) — 26 slots, ~11 s host `load_ok` → `usb_host,begin` with one-slot-per-loop scheduling.

## Change

In [`src/main.cpp`](../../src/main.cpp), while `bootSlotLoadRefreshPending`:

```cpp
while (StorageManager::hasPendingLoopSlotRestore()) {
  StorageManager::processDeferredLoopSlotRestore();
}
```

- No `displayManager.update()` between slots during boot drain (title already held).
- After `bootInteractiveReady()`: `finishBootSetup` + USB + first piano-roll paint (unchanged).
- Post-boot idle: still **one** `processDeferredLoopSlotRestore()` per loop.

## Out of scope

Multi-slot time budget; Phase 3b/4; load while PLAYING; changing which slots are queued.

## Verification

```bash
pio run -e teensy41-capture-serial
# upload after confirm; restart capture_session.py
.venv/bin/python scripts/verify_boot_restore_timing.py --follow-current-session
```

Pass: drain-before-USB; no `Boot audible`; `load_ok` → `usb_host,begin` shorter than ~11 s on same set; title until last deferred restore.
