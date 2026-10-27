# P-D — the prepared-index producer is not live-lane-safe

**Outcome: C5.2's premise is falsified by the code. The enablement was implemented, built, and then
reverted before it could ship. Nothing is enabled; the tree is byte-identical to baseline for every file
this slice touched.**

This is the gate working, not a delay: the planned continuation would have moved whole-pass sorting and
copying onto the audible playback lane — the exact defect P-D exists to remove.

## What was attempted

Per [P-D step 1](../packets/P-D-VISUAL.md) and [C5.2](../CONTRACTS.md):

- `Track::stepDerivedViewPreparedWindow()` — one phase step per turn, admitted for the owning track,
  never displacing a session another loop owns, rebuilding only when the kept index is not ready for the
  current `playbackRevision`.
- Called from the derived-view branch of `Track::processDeferredIdleMaintenance` immediately before
  `Loop::rebuildVisualCacheIdleSlice`, so the slice could find a prepared window.

It compiled and linked: **shipping build SUCCESS at unchanged pins** (variables 96,192 / code 392,120 +
padding 1,096 / locals 34,880), and the full native suite stayed at **3,454 / 7 skipped / 3,447 / 0**.

## Why it was reverted

`DeviceGateSession::runOneSlice` (`src/LoopContentResolution.cpp:2120`) advances one phase per call, but
many of its steps declare themselves **indivisible**, and those steps are content-sized. At the sites that
pass `indivisible = true` to `continueSlice(step, units, resume, indivisible, elapsedUs)`
(`src/LoopContentResolution.cpp:1872`):

| Line | Step | Work in one call |
|---:|---|---|
| 2139 | by-NoteId sort | `index.sortAndUniqueByNoteId(...)` over the whole index |
| 2149 | tick-event sort | `index.sortTickEventEntriesByTick(...)` over the whole tick-event vector |
| 2253 | `win` | copies `window.size()` rows |
| 2347 | `ecopy` | copies `prepScratch.size()` rows |
| 2367 | `ecopy` | copies `rebuildEvents.size()` rows |
| 2397 | `recon` | `reconBuild.spanCount()` spans |
| 2426 | dedup | `rebuildNotes.size()` rows |
| 2451 | channel index | `checkpoints.channelByNoteId.size()` entries |
| 2533 | `state` complete | `presentNotes.size()` rows |

And the constant that looks like a bound is not one: **`kDeviceGateSliceBudgetUs = 50,000`**
(`include/LoopContentResolution.h:285`) is **never read by `runOneSlice`**. It is measurement
documentation, not an enforced per-step cap. Only `kDeviceGateEventsPerSlice` (8) bounds the *row-appending*
steps; the sorts and vector copies are unbounded.

So promoting this producer onto the derived-view lane would put a 33-bar/17-pass whole-index sort and
several whole-vector copies on the lane that also carries MIDI input service. That is precisely what
[execution bounds](../../../../docs/Authority/Architecture/ExecutionBounds.md) rows 6/7 forbid promoting, and
what C5.4 promised would not be promoted.

## Consequence for the contract

C5.2 named the prepared-index session as the continuation **because its shape exists** — phases, cursors,
a stamped result, public gates. Shape was necessary and not sufficient: two of its steps are whole-index
sorts, and it was designed for the transport-idle lane where a 50 ms class of work is acceptable.

C5.2 is therefore amended: the continuation may be the live-lane producer **only after** its indivisible
steps are bounded or replaced, and the packet that does so is the one that may enable it. Enabling first
and bounding later would regress the very timing the change is for.

Two forward options, both contract-compatible:

- **A (preferred): bound the continuation's indivisible steps.** Split the two sorts into bounded merge
  units, and replace the whole-vector copies (`win`, `ecopy`, dedup, channel index, `state`) with
  incremental appends against pre-reserved capacity. Then C5.2 holds as written and the slice's prepared
  attempt starts hitting.
- **B: window-scoped resolution in the slice.** Resolve only the requested window through
  `LoopContentResolution::resolveWindow(...)` with a declared per-window cap, accepting per-pass filtering
  cost and proving it against the same oracle. Cheaper to build, weaker claim.

## What this does not change

- No firmware behaviour changed: the shipping image is unchanged, the pins are unchanged, and no
  acceptance profile moved.
- C5.1 (the slice must resolve a window, never the pass set) still stands and is still the goal.
- The P-D oracle table is unchanged; `P-D-1` still requires that a real-time source miss does not report
  `PassesResolve`.

## Reproduce

```sh
pio run -e teensy41-capture-serial        # baseline: code 392,120 + padding 1,096, floor 840
pio test -e native                        # 3,454 / 7 skipped / 3,447 passed / 0 failed
grep -n "indivisible" src/LoopContentResolution.cpp | head
grep -rn "kDeviceGateSliceBudgetUs" src/  # no read site
```

The attempted patch is not in the tree: both edited files report no diff against `HEAD` apart from the
pre-existing DEC-158 change in `include/Track.h`.
