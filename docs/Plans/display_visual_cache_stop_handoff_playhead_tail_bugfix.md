# Display visual-cache stop handoff playhead tail

**Status:** Implemented — native **1526/1526**; RAM1 locals **6944**  
**Date:** 2026-08-31  
**Kind:** bugfix  
**GitHub:** [#41](https://github.com/Lytrix/MidiLooper/issues/41) Stage 1j  
**Parent:** [`display_visual_cache_snapshot_reuse_refinement.md`](display_visual_cache_snapshot_reuse_refinement.md)  
**Debugging boundary:** 1i wrap/stop first paint is closed (`42b00cde`). Do not reopen 1b–1i hold/gather. Do not start Stage 2. Do not close overdub held tails at `L-1`. Do not change Overlay return.  
**Evidence:** [`225243`](../../captures/session_20260831_225243.log)

**Does not authorize:** Overlay → false on `tryGather`; Stage 2 snapshot-note copy; `openTailCloseTick = L-1`; widening `isWrappedLoopNotePair`; OpenSpec `3.5` / `3.6`.

---

## Debugging boundary

```text
rebuildOverdubCommittedDisplayLayer / STOPPED last-resort hold
    ← closed (1i)
applyCapturePlayheadTails writes endTick=currentTick onto liveDisplayNotes
    ← this slice
finishCanonicalSpansOpenNotes unmatched On → loopLength (Off@0 wrap)
    ← next RC, not this slice
```

---

## Problem

[`225243`](../../captures/session_20260831_225243.log) first STOPPED `DISP` is **147 / 198** (RC5a handoff, same slot). Only D#2 (39) is held at seal: ON abs **12912** (phase **624**), stop abs **12992** (phase **704**). `applyCapturePlayheadTails` updates that capture-region note **in place** (`endTick = playhead`). `preservedOverdubStopDisplayNoteCount` drops extra rows only. Capture is already cleared before `refreshViewportAfterOverdubStop`, so preview lengths cannot be restored at refresh. Handoff freezes the live tail at **704**.

A second D#2 length (unmatched `N@2928` + `F@0` drawn to `loopLength`) is reconstruct `finishCanonicalSpansOpenNotes` — not this owner.

## Invariant

```
live OVERDUBBING paint may grow open notes to currentTick
liveDisplayNotes stays at capture-preview lengths
STOPPED RC5a handoff does not freeze playhead-extended ends
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | NO |
| State transition change? | NO |
| Unbounded interval owner? | NO — same O(open notes); copy already used for occupy paint |
| Reuse | YES — apply tails to `liveDisplayPendingPaintNotes_` |

## Implementation

- Restore open preview notes onto `liveDisplayNotes` (compose base).
- Copy compose base to `liveDisplayPendingPaintNotes_` when tails or occupy paint run.
- `applyCapturePlayheadTails` writes the copy only (`capturePlayheadTailsArePaintOnly`).
- Occupy pending changes apply on the same copy after tails.

## Verification

Native (`test_display_window_utils`): `capturePlayheadTailsArePaintOnly` is true. Full suite **1526/1526**. RAM1 locals **6944**.

Device ([`225243`](../../captures/session_20260831_225243.log) shape): first STOPPED handoff must not show the held D#2 ending at stop phase **704** unless storage reconstruct later seals that Off. Recapture required.

## Explicitly out of scope

- Unmatched `N@2928` / `F@0` → `loopLength` (reconstruct)
- Stage 2 snapshot-note copy
- Overlay return
- 1h pairing / `isWrappedLoopNotePair`
- Closing overdub tails at `L-1`
