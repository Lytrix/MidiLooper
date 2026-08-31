# Display visual-cache STOPPED paint current fill

**Status:** Implemented — native **1519/1519**; RAM1 locals **6944**. Device FAIL [`220337`](../../captures/session_20260831_220337.log), [`222349`](../../captures/session_20260831_222349.log)  
**Date:** 2026-08-31  
**Kind:** bugfix  
**GitHub:** [#41](https://github.com/Lytrix/MidiLooper/issues/41) Stage 1f  
**Parent:** [`display_visual_cache_snapshot_reuse_refinement.md`](display_visual_cache_snapshot_reuse_refinement.md)  
**Debugging boundary:** idle Overlay reject is closed (`b186db42`). Do not reopen 1b–1e. Do not start Stage 2 snapshot-note copy. Do not make the snapshot current earlier. Do not change Overlay return on `tryGatherPublishedSnapshotEventsInWindow`.  
**Evidence:** [`213448`](../../captures/session_20260831_213448.log)

**Does not authorize:** Overlay → false on `tryGatherPublishedSnapshotEventsInWindow`; Stage 2 snapshot-note copy; assigning dirty `visualCache` as STOPPED paint; `rebuildDisplayNotesInWindow`; forcing STOPPED idle; OpenSpec `3.5` / `3.6`.

---

## Debugging boundary

```text
rebuildVisualCacheIdleSlice Overlay reject
    ← closed (1e). Idle cache stays off Overlay.
resolveDisplayNotesCommitted last-resort gather
    ← this slice
rebuildDisplayNotesInWindow / playback Overlay
    ← unchanged
LoopSnapshot publication timing / Stage 2 note copy
    ← unchanged
```

---

## Problem

[`213448`](../../captures/session_20260831_213448.log) is not wrap-tick merge and not idle Overlay fill.

- Wrap 1/2/3: `idle_src,OverlayRejected` → Fallback → `slice_clean` 203 / 228 / 231. Cache stays **231** through STOPPED.
- Last OVERDUBBING: `DISP` **260 / 231 / 260** (live overlay on cache).
- Stop marks dirty (`stale_range` 231 notes). First STOPPED `DISP` **274 / 231 / 274**.
- This loop has no `idle_src` during ~3 s STOPPED. PLAYING idle Fallback → `slice_clean` 231 → `DISP` **231 / 231**.

Owner: `DisplayManager::resolveDisplayNotesCommitted` last-resort. When cache is dirty it calls `tryGatherPublishedSnapshotEventsInWindow` (true whenever a root exists). Overlay (`root_rev=1`, `playback_rev=9`) plus `reconstructDisplayNotes` plus `appendOverdubPassDisplayNotes` writes the 274-note frame. 1e does not apply to this caller.

DNTE `39@192` length 256 is deferred stored `reconstructNotes` (wrap-aware). That dump is not the piano-roll list. Out of scope.

## Invariant

```
overdub stop → first STOPPED DISP → paint == current committed content
```

Play must not be the first time the piano roll shortens. Required: **frame == visualCache** (that capture: 231/231), not Overlay reconstruct. Must not show `39/45/51 @ 192` length **256** on the roll.

`paint == cache` while both are Overlay-wrong remains the dangerous state. 1e already keeps cache off Overlay. This slice stops paint from using Overlay when the last-resort gather runs.

| `tryGather` path | STOPPED last-resort paint |
|---|---|
| `Snapshot` | Accept (current root) |
| `Overlay` | Discard `liveDisplayEventBuffer`; `usedSnapshot=false`; continue to `gatherCommittedEvents` / `gatherCommittedEventsWithCapture` |
| `None` | Existing fallback |

## Layering (do not collapse)

```
Playback:      stale snapshot + overlay → valid
Idle cache:    stale Overlay → reject (1e, shipped)
STOPPED paint: stale Overlay → reject (this slice)
```

Do not assign a dirty `visualCache` as a shortcut (RC5b / RC5a: dirty cache is not committed authority). RC5a handoff and clean-cache assign stay.

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | NO — last-resort gather already exists in `resolveDisplayNotesCommitted` |
| State transition change? | NO |
| Unbounded interval owner? | NO — last-resort gather already exists; this only changes which buffer it keeps |
| Reuse | YES — extend `resolveDisplayNotesCommitted` last-resort only |

## Implementation

- After last-resort `tryGather` / `tryGather...WithCapture`, Overlay clears `liveDisplayEventBuffer` and leaves `usedSnapshot = false` so the existing `gatherCommittedEvents` / `gatherCommittedEventsWithCapture` branches run.
- `tryGatherPublishedSnapshotEventsInWindowWithCapture` forwards optional `pathOut` to `tryGather`. Overlay return stays true.
- Capture line: `#CAP,...,DISP,paint_src,<path>,src,<source>,root_rev,...,playback_rev,...` (`OverlayRejected` when rejected).
- Keep `appendOverdubPassDisplayNotes` and `applyLiveOpenTails(..., extendHeldNotesToPlayhead=false)` after the authoritative buffer.
- `rebuildDisplayNotesInWindow` and playback Overlay contract unchanged.

## Verification

Native (`test_overdub_source_view`): reuse the 1e stale-root fixture. Overlay reconstruct (pairing off) ≠ `gatherCommittedEvents` reconstruct (pairing off). Discard Overlay; `gatherCommittedEvents`; reconstruct pairing off matches the committed oracle.

Device FAIL [`220337`](../../captures/session_20260831_220337.log) (8-bar first overdub):

- Wrap 1/2 and stop: `stale_range` is always `first=0,last=7,total=8,dsz=8,dcnt=8`. Idle then `slice_clean` of the whole loop (`127→206`, `206→276`).
- Last OVERDUBBING `DISP` `276 / 269`. First STOPPED `DISP` **276 / 344**. No `paint_src` on that stop. No `idle_src` during ~38 s STOPPED.
- PLAYING idle `Snapshot` `root_rev=7` / `playback_rev=7` → `slice_clean` **334** → `DISP` **334 / 334**. Play is still the first shorten/match.
- Third overdub stop (2-bar): `paint_src,OverlayRejected` then `DISP` **80 / 91**. 1f reject ran; frame still ≠ cache.

Do not use DNTE 256 as the paint oracle.

## Explicitly out of scope

- Stage 2 snapshot-note copy
- Making the snapshot current earlier
- Overlay return value / playback
- `rebuildDisplayNotesInWindow`
- Assigning dirty visualCache as STOPPED paint
- Forcing STOPPED idle to run; changing `applyLiveOpenTails`; source-view consume; N/pages; OpenSpec `3.5` / `3.6`
