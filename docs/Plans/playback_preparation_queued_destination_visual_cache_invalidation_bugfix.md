# Queued-destination visual-cache invalidation

Status: Firmware shipped on `feature/immutable-loop-snapshot-passes` — host gates next; HITL still open. Independent of maintained-LCR / dest-idle readiness work.

## Problem

At slot-queue time, `DisplayManager::invalidateForSlotChange` invalidated the destination loop’s visual cache. Idle visual-cache slices in `Track::processDeferredIdleMaintenance` service only `getActiveLoop()`. A queued destination therefore had no rebuild owner until after LoopEnd commit.

Measured on [`222539`](../../captures/session_20260824_222539.log):

- `44044724` — `VCACHE,stale_all` slot 6 `notes=0` `total=20` `dirty=1` (queue)
- `44323763` — `DISP,6,PLAYING,15360` `visualNotes=0` (queued paint from `liveDisplayNotes`)
- `45240900` — `slot_commit,0,5,6,1,15360,1`
- `47018780` — `VCACHE,slice_clean` `notes=621` `dirty=0` (rebuilt 1.78 s after commit)

Same shape on a 4-bar dest, ~110 ms ([`220553`](../../captures/session_20260824_220553.log) `16246220` → `16356243`).

This is not MIDI first-consume readiness latency. Do not use this repair to change dest-idle apply or LCR.

## Current owner

`DisplayManager::invalidateForSlotChange` ([`DisplayCacheLifecycle.cpp`](../../src/DisplayManager/DisplayCacheLifecycle.cpp)):

- Always `invalidateLiveDisplayCache()` (live preview must not treat another slot’s note list as authority).
- When `playbackActive` and the slot is the **active** playing loop, `Loop::invalidateDisplayCaches()` **clears** `visualCache.notes`.
- A queued destination (`newSlot != activeSlot`) is left in place.
- Otherwise `Loop::markDisplayCachesStale()` — keeps notes, dirties every bar (`stale_all`).
- STOPPED track/slot switch must keep notes ([`161804`](../../captures/session_20260821_161804.log) / [`165107`](../../captures/session_20260821_165107.log)). Do not reopen that wipe.

## Authorized repair

Do not wipe or full-bar-dirty a queued destination cache that no owner will service.

- Queued dest: `newSlot != activeSlot` while the destination is not yet `getActiveLoop()`.
- Leave that loop’s `visualCache` in place (no `invalidateDisplayCaches`, no `markDisplayCachesStale` on that dest).
- Keep `invalidateLiveDisplayCache()` at function entry.
- Keep the existing PLAYING wipe/stale rules for the **active** slot and for STOPPED.
- No new cache owner. Do not extend idle slices to queued non-active slots (execution-bound change; design session required).
- No LCR changes.

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** — same `invalidateForSlotChange` owner; narrower dest invalidation |
| State transition change? | **NO** — queue and commit transitions unchanged |
| Unbounded interval owner? | **NO** — no new idle claimant |

## Verification

Host: `pio run -e teensy41-capture-serial` (locals >= 4000) then `pio test -e native`.

HITL: queue a long dest while PLAYING (`5→6` or `1→6`). After `slot_queue` and before `slot_commit`, dest must not show `VCACHE,stale_all` / empty `visualCache.notes` caused by this path. After commit, display may refresh from the existing owner. MIDI first-consume latency is **not** a pass criterion for this repair.

## Out of scope

Dest-queued LCR, LoopEnd history apply, horizon, `K`, new visual-cache servicer, `playbackOrderDirty` reanchor.
