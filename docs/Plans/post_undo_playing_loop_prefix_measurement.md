# Post-undo PLAYING loop prefix measurement

**Status:** Attribution OPEN — prefix rem **landed**, device gate open  
**Date:** 2026-08-16  
**Kind:** measurement (not a fix)  
**Parent:** [`post_overdub_playing_midi_drain_bugfix.md`](post_overdub_playing_midi_drain_bugfix.md) (overdub-stop drain **shipped**)  
**Evidence:** [`035822`](../../captures/session_20260816_035822.log) 135.508–140.515 s

## Problem (different from the shipped drain)

The post-overdub drain solved **transition-specific** starvation after `stopOverdubbing`. The same protection is **not** restored after an undo that returns the system to PLAYING with a dirty cache.

This is a **drain-lifecycle / state-machine** question, not a visual-cache or persist change.

## Attribution: OPEN — prefix owner unknown

Known from [`035822`](../../captures/session_20260816_035822.log):

- 402 ms `midi_gap` is real (5 s window max at 140.515 s).
- It occurs after double-press undo (136.696 s, `kind=1` OverdubPassAdded).
- Undo itself is ~3 ms inside `handleMidiInput`.
- The following short overdub (138.699–139.544 s) is not responsible.
- `idle_maint` (80 ms), `load_frame` (90 ms), and `persist_save` (16 ms) in that window do not account for it. `midi_input` is 37 ms.
- ~330 ms occurs before `idle_maint` (CAP silence 137.867–138.277 s; next rem is idle 76 ms).
- No CAP lines occur during that ~330 ms.
- PLAYING drain is not armed after undo. `slice_clean` at 134.852 s already cleared the overdub-stop arm. Undo does not call `armPlayingMidiDrainAfterOverdubStop()`.
- Missing drain is a **plausible scheduling mechanism**. The exact prefix cost is still **unattributed**.

## State transitions

```text
OVERDUBBING
   │
   └─ stopOverdubbing()
        │
        └─ arm PLAYING MIDI drain
              │
              ▼
           PLAYING
              │
              └─ cache maintenance
                    │
                    └─ slice_clean
                         └─ clears arm
```

```text
PLAYING
   │
   └─ double-press UNDO
        │
        ├─ OverdubPassAdded removed
        ├─ visual cache dirtied
        ├─ LoopPersist admitted
        └─ state remains PLAYING
             │
             └─ NO drain re-arm
                  │
                  ▼
             next loop pass
                  │
                  ├─ ~330 ms untimed prefix
                  ├─ ~76 ms idle
                  └─ ...
```

## Next slice — measure, do not fix

Instrument **one** remainder around the untimed `loop()` prefix on the **post-undo PLAYING** path only. Whole prefix as one bounded region. Do not put timers on children until this rem is ~330 ms.

```text
post-undo loop pass
    │
    ├── prefix enter          ← one rem, this slice
    │     processDroidUsbHostOutbound
    │     clockManager.checkClockSource
    │     SC_UPDATE
    │     looperState / buttons / faders / barStep
    │     controlSurfaceManager.update
    │     maybeUpdateDisplayForNoteEditSelection
    │     gpio / looper.update
    │     updateMidiLedsDeferred
    │     processDeferredSummary
    │     SC_CAPTURE_FLUSH
    │     updateForOverdubbing
    │
    ├── idle_maint            (already timed)
    ├── load_frame            (already timed)
    └── persist_save          (already timed)
```

Bounds in `loop()`: after the top `handleMidiInput()` exit, before `processDeferredIdleMaintenance` (before the post-overdub around-idle poll).

Reuse `recordLoopRemainderSpan` / `DIAG,loop_rem,<span>,…` (50 ms one-shot). Proposed span: `loop_prefix` (same owner as `idle_maint` / `load_frame` / `persist_save`; not a new domain noun). Gate emission to the post-undo PLAYING window so PLAYING-as-usual is not flooded.

Target read:

```text
loop_prefix   = ~330 ms
idle_maint    = ~76 ms
load_frame    = ~90 ms
persist_save  = ~16 ms
```

If `loop_prefix` is ~330 ms, **then** instrument its children. If it is not, the 330 ms hole is elsewhere — stop and re-read.

## Architecture question (do not decide this slice)

After the prefix is attributed, ask:

> Should PLAYING MIDI drain be armed based on the **reason PLAYING became active**, or based on the fact that **PLAYING has become MIDI-starved while dirty work is pending**?

Current implementation is transition-specific: `overdub stop → arm`.

The new failure suggests a broader invariant (`PLAYING` + pending expensive/deferred work + MIDI must stay responsive → drain available). **Do not change to that yet.** The prefix measurement may justify a much more specific trigger.

## Architecture checkpoint (measurement only)

1. Ownership change? **NO** — extend `recordLoopRemainderSpan` in `loop()`.
2. State transition change? **NO** — do not re-arm drain. Do not change undo.

## Out of scope

Re-arming PLAYING drain on undo. All-PLAYING drain. Visual-cache grain. LoopPersist CRC. LoadLoopJob. 6.3. LCR. Interval reservation. Child timers on the prefix. RING overflow on overdub stop.

## Device [`105516`](../../captures/session_20260816_105516.log)

No `loop_prefix` rem (measurement not landed). `clockrate` 47–48 while PLAYING. RING on every overdub stop. ANO only at 119.038 s transport stop.

Planned cluster reproduced twice while PLAYING:

| t | What |
|---|------|
| 64.835 s | stop. Drain armed. `slice_clean` 65.160 s clears arm. First dump `midi_gap` **84 ms** = `idle_maint` 84 ms. |
| 65.684 s | undo `kind=1`. Cache dirty, persist queued. First rem idle **74 ms**. `load_frame` **101 ms** at +293 ms. Next overdub 67.301 s. |
| 73.836 s | stop. `slice_clean` 74.057 s. Persist rem 70 ms at 74.301 s. |
| 75.170 s | undo `kind=1`. First rem idle **72 ms**. `load_frame` **98 ms** at +176 ms. Dump at 75.580 s `midi_gap` **320 ms**. |

Same 5 s window as that 320 ms: `idle_maint` 81 ms, `load_frame` 98 ms, `persist_save` 70 ms, `midi_input` 24 ms. Those four do not equal 320 ms.

Larger PLAYING `midi_gap` also occurs **without** undo in this capture:

| Dump | `midi_gap` | `idle_maint` | `load_frame` | `persist_save` | Context |
|------|-----------:|-------------:|-------------:|---------------:|---------|
| 70.571 s | **292 ms** | 80 ms | 101 ms | 79 ms | after 65.684 s undo |
| 75.580 s | **320 ms** | 81 ms | 98 ms | 70 ms | after 75.170 s undo |
| 90.940 s | **346 ms** | 87 ms | 22 ms | 25 ms | overdub 86.915–92.161 s |
| 106.064 s | **401 ms** | 95 ms | 23 ms | 17 ms | after stop 104.359 s, **no undo** |
| 116.065 s | **410 ms** | 99 ms | 22 ms | 0.3 ms | still OVERDUBBING (started 109.347 s) |

The 104.359 s stop arms drain. First rem is idle 85 ms. `slice_clean` at 106.027 s. Window persist 17 ms. **401 ms is not idle+load+persist stacked.** Drain polls around idle and after load; the `loop()` prefix is still between the top poll and the around-idle poll.

STOPPED `idle_maint` **338 ms** at 166.092 s (`midi_gap` 342 ms, `clockrate` 0) is the LCR device-gate, not this slice.

Attribution stays **OPEN**. This capture adds: a ~400 ms PLAYING `midi_gap` exists with drain armed and rem too small. Prefix rem is still the next measurement.

## Implementation (landed, not scored)

`TrackUndo::undoForLoop` arms `armLoopPrefixMeasureAfterUndo()` after a successful `applyUndoEntry`. Does **not** call `armPlayingMidiDrainAfterOverdubStop()`.

Window: `PLAYING AND armed by undo AND (visualCacheDirty OR first prefix not noted)`. Cleared by `startOverdubbing` and `Track::clear`. `noteLoopPrefixMeasureAfterUndo()` runs after idle, same pass as the drain idle note, so a `slice_clean` in that idle can drop the arm.

`loop()` (`SESSION_CAPTURE` only): `micros()` after the top `handleMidiInput()`, `recordLoopRemainderSpan("loop_prefix", …)` immediately before the around-idle poll. Measure helpers are `FLASHMEM` / `TRACK_COLD_MEM` so they do not cross the RAM1 32 KB ITCM page (HEAD locals 6528; this build 6496).

## Pre-implementation review

### Ready
- Prefix bounds in `loop()` are after the top `handleMidiInput()` and before `processDeferredIdleMaintenance`.
- Rem owner is existing `recordLoopRemainderSpan` / `DIAG,loop_rem`.
- Undo arm site is `TrackUndo::undoForLoop` after a successful `applyUndoEntry`.

### Resolved
| Topic | Decision |
|-------|----------|
| Drain | Do not re-arm |
| Span | `loop_prefix` |
| Window | PLAYING + armed by undo + (`visualCacheDirty` OR first pass not noted) |
| Children | Not timed |

### Open before coding
None.

### Proceed?
YES

## Device gate

Same cluster as [`035822`](../../captures/session_20260816_035822.log): short overdub → stop → double-press undo → stay PLAYING until the next overdub. Score `DIAG,loop_rem,loop_prefix` against the CAP hole before `idle_maint`. Recapture without the rem: [`105516`](../../captures/session_20260816_105516.log).
