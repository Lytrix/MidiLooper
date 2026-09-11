# Occupy hold phase must use the shared capture phase frame

**Status:** Host pin in tree. Device HITL occupy_paint required in both transport conditions.  
**Date:** 2026-09-05  
**Kind:** bugfix  
**Evidence:** [`162325`](../../captures/session_20260905_162325.log) (`startLoop,0`, occupy holds PASS) vs [`162652`](../../captures/session_20260905_162652.log) (`startLoop,247`, missed / wrong-frame / spurious occupy)  
**Does not authorize:** RC1d-c 1-tick leftovers (`52@2180`); display realization; new capture-phase owner

---

## Root cause

`Track::snapshotOverlapHoldCandidates` computed occupy `holdStart` with phase origin **0**:

```
IntervalProjection::tickPhaseInLoop(pending.startNoteTick, 0, loopLength)
```

Every other consumer of `pending.startNoteTick` converts through `Track::capturePhaseTick`, which while overdubbing is `tickPhaseInProjectionCycle(absTick, projectionCycleStartTick, loopLength)`. That is capture-ownership invariant #4 in [`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) ("Shared capture phase"). Occupy On was the remaining consumer that bypassed that owner.

The two frames differ by exactly `startLoopTick` (mod `loopLength`).

## Evidence

`162325` recorded with `startLoop,0`. `162652` recorded with `startLoop,247` because transport was already running.

In `162652`, `projStart,3319` and `startLoop,247` with `loopLength 3072`, so `3319 = 247 + 3072`. Storage phase is `(t-247) mod 3072`; occupy hold was `t mod 3072`. Every OD2 `DIAG,lcr,part,why=on` `hs=` is storage + 247.

Consequences in that run:

- **Missed occupy** — 16 of 20 holds report `n=0,a=0`. Example `pitch=56,hs=543` is storage `296`; source `56@83–469` covers 296, but present-at-hold is asked about 543.
- **Wrong-frame Shorten** — `pitch=56,as=1339,ae=1745,hs=1607` Shortens A to `1606` instead of `1359`.
- **Spurious occupy** — `pitch=48,as=570,ae=955,hs=736` transforms a note that does not cover storage start `489`.

`162325` (`startLoop=0`) produced none of these.

## Fix

Route both occupy hold sites through `Track::capturePhaseTick`. No new function, no new owner.

- `Track::snapshotOverlapHoldCandidates` — `holdStart = capturePhaseTick(pending.startNoteTick)`. This also corrects the phase handed to `catchUpCommittedPlaybackLedgerToPhase`.
- `Track::collectOverlapHoldPlaybackNoteOn` — same substitution; keep the `loopLengthTicks == 0` guard.

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** — `capturePhaseTick` already owns capture phase |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** — same O(1) modulo |
| Reuse | **YES** — extend the existing owner at both call sites |

## Out of scope

The remaining 1-tick leftovers (`48@2335`, `52@2180`) are the RC1d-c display-realization class on [`overdub_paint_effective_frame_parity_bugfix.md`](overdub_paint_effective_frame_parity_bugfix.md).

## Verification

- `pio run -e teensy41-capture-serial`; RAM1 locals >= 4000.
- `pio test -e native` (pre-existing `test_same_pass_playhead_tails_keep_closed_bar_four_end` stays red).
- Host pin `test_occupy_nonzero_start_loop_shortens_at_storage_hold` in `test_overdub_source_view`.
- HITL occupy_paint in both transport conditions: transport stopped first (`startLoopTick == 0`) and transport already running (`startLoopTick != 0`).
