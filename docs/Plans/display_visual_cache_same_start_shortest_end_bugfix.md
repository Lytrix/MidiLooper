# Display visual-cache same-start shortest end

**Status:** Implemented — native **1531/1531**, RAM1 locals **6848**. Device recapture required.  
**Date:** 2026-09-01  
**Kind:** bugfix  
**GitHub:** [#41](https://github.com/Lytrix/MidiLooper/issues/41) Stage 1b (same-start stacks)  
**Parent:** [`display_visual_cache_snapshot_reuse_refinement.md`](display_visual_cache_snapshot_reuse_refinement.md)  
**Debugging boundary:** wrap-pair length append skip is already in `appendOverdubPassWrapPairedNotes`. This slice is reconstruct/display unique-key pitch+start. Do not reopen 1h. Do not start Stage 2.  
**Evidence:** [`231953`](../../captures/session_20260831_231953.log) reconstruct 33 same-start multi-length stacks

**Does not authorize:** Overlay → false on `tryGather`; Stage 2 snapshot-note copy; OpenSpec `3.5` / `3.6`.

---

## Debugging boundary

```text
appendOverdubPassWrapPairedNotes skip existing pitch+start
    ← closed (Stage 1 wrap-pair length)
dedupeProjectedDisplayNoteVec unique key pitch+start (shortest end)
    ← this slice
idle splice skip when pitch+start already in visualCache
    ← this slice
```

---

## Problem

[`231953`](../../captures/session_20260831_231953.log) reconstruct dump has 33 same-start multi-length stacks (`39@48` 144+368, `45/51@1136` 32+320). Frame dump has 6, including `45/51@1136` 0+320. `dedupeProjectedDisplayNoteVec` previously unique-keyed pitch+start+end, so both lengths survived.

## Invariant

```
same pitch + startTick → one DisplayNote (shortest endTick)
idle splice does not push a second length at that pitch+start
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | NO |
| State transition change? | NO |
| Unbounded interval owner? | NO — same reconstruct unique pass |
| Reuse | YES — `dedupeProjectedDisplayNoteVec`, `rebuildVisualCacheIdleSlice` splice |

## Implementation

- After sort by note, start, end, index, unique key is **pitch+start** — shortest end wins.
- Idle splice after `removeDisplayNotesOverlappingBars` skips `displayNotePitchStartPresent`.

Native: `test_reconstruct_same_pitch_start_keeps_shortest_end` — two `N@48` + `F@192` + `F@416` → only `48→192`.

## Explicitly out of scope

- 1h / `isWrappedLoopNotePair`
- 1l leftover Off@0
- Stage 2
