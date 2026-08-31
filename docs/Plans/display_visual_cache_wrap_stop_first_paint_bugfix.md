# Display visual-cache wrap and stop first paint

**Status:** Implemented — native **1525/1525**; RAM1 locals **6944**  
**Date:** 2026-08-31  
**Kind:** bugfix  
**GitHub:** [#41](https://github.com/Lytrix/MidiLooper/issues/41) Stage 1i  
**Parent:** [`display_visual_cache_snapshot_reuse_refinement.md`](display_visual_cache_snapshot_reuse_refinement.md)  
**Debugging boundary:** 1h held-note wrap pair is closed (`4238b422`). Do not reopen 1b–1h pairing. Do not start Stage 2. Do not change Overlay return.  
**Evidence:** [`222349`](../../captures/session_20260831_222349.log)

**Does not authorize:** Overlay → false on `tryGather`; Stage 2 snapshot-note copy; assigning dirty cache as **clean** authority (RC5b); OpenSpec `3.5` / `3.6`.

---

## Debugging boundary

```text
appendCanonicalSpansFromMidiRange Off@0 vs linear Off
    ← closed (1h)
rebuildOverdubCommittedDisplayLayer gather-before-hold
resolveDisplayNotesCommitted last-resort after slot clear
    ← this slice
applyCapturePlayheadTails onto liveDisplayNotes
    ← Stage 1j
Idle Overlay reject / 1f Overlay reject / Stage 2
    ← unchanged
```

---

## Problem

[`222349`](../../captures/session_20260831_222349.log) wrap first `DISP` drops (`102→67`, `119→101`, …) then `slice_clean` restores. `rebuildOverdubCommittedDisplayLayer` snapshot-gathers when the paint window leaves the last gather, **before** the dirty-cache hold.

First selected-slot STOPPED `DISP` is **159 / 31** after slot 1 painted `31`. Slot mismatch clears `liveDisplayNotes`; last-resort gather paints 31 events while dirty cache still has 159. Later `paint_src,OverlayRejected` is **159 / 166**.

## Invariant

```
wrap first OVERDUBBING DISP → hold pre-wrap committed count while cache dirty
STOPPED first selected-slot DISP → dirty nonempty visualCache or last handoff,
                                    not a foreign last-resort gather
```

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | NO |
| State transition change? | NO |
| Unbounded interval owner? | NO — skips gather |
| Reuse | YES — extend hold helpers and existing resolve functions |

## Implementation

- `shouldHoldDirtyOverdubLayerBeforeWindowGather`: dirty + canHold + not grown → hold **before** snapshot window gather.
- `shouldHoldDirtyVisualCacheUntilIdle`: dirty nonempty cache + incremental + no handoff → assign cache, skip last-resort. 1f Overlay reject stays for empty cache.

## Verification

Native (`test_display_window_utils`): both hold predicates. Full suite **1525/1525**. RAM1 locals **6944**.

Device ([`222349`](../../captures/session_20260831_222349.log) shape): wrap first frame ≥ last-before-wrap. First selected-slot STOPPED `visual` ≈ `frame` (not `159/31`). Recapture required — [`225243`](../../captures/session_20260831_225243.log) first STOPPED **147 / 198** kept RC5a handoff (same slot), so this hold did not run.

## Explicitly out of scope

- Stage 2 snapshot-note copy
- Overlay return
- 1h pairing
- Changing `isWrappedLoopNotePair`
