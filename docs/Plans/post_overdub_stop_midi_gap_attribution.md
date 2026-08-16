# Post-overdub-stop MIDI gap attribution

**Status:** CLOSED — scheduling experiment in [`post_overdub_playing_midi_drain_bugfix.md`](post_overdub_playing_midi_drain_bugfix.md)  
**Date:** 2026-08-16  
**Kind:** investigation  
**Evidence:** [`031229`](../../captures/session_20260816_031229.log) (workspace CRC shipped, LoopPersist finalize not landed)

## Acceptance

Identify the first operation after overdub stop that accounts for the MIDI gap, with timestamps around each stop-side phase.

## Out of scope

Do not touch `LoopPersist` CRC, lazy load, `LoadLoopJob`, `runDeferredLoadAndDisplayFrame`, 6.3, or LCR architecture in this slice.

Parked separately:

- 915 ms boot `load_frame` — [`032803`](../../captures/session_20260816_032803.log) / [`032542`](../../captures/session_20260816_032542.log)
- 72–83 ms loop-temp CRC — [`persist_loop_slot_finalize_slice_bugfix.md`](persist_loop_slot_finalize_slice_bugfix.md) (reverted; RAM1)

## How `midi_gap` is measured

`RuntimeTimingTelemetry::noteMidiInputEnter` records exit→next-enter of `handleMidiInput()`. Work inside `handleMidiInput()` is `midi_input`, not `midi_gap`.

After overdub stop the track is PLAYING. `loop()` mid-frame MIDI drain runs only while RECORD/OVERDUB (`captureActiveForMidiDrain`). The next poll is after `processDeferredIdleMaintenance`, `runDeferredLoadAndDisplayFrame`, and `processDeferredSaveState`.

## One stop — [`031229`](../../captures/session_20260816_031229.log) 78.404 s

`MIDI Button A: Stop Overdub` → `Track::stopOverdubbing` from the button path inside `handleMidiInput()`.

```text
t=78404264  ODUB,stop,enter       +57 µs
t=78406985  PERS,request          admit (inside seal)
t=78407032  ODUB,stop,seal        +2825 µs elapsed, duration 2697 µs, published
t=78407089  ODUB,stop,finalize    +2882 µs (duration 0 — already in seal)
t=78421503  ST OVERDUBBING→PLAYING
t=78421531  ODUB,stop,set_state   +17324 µs elapsed, duration 14409 µs
t=78422346  ODUB,stop,flush       +18138 µs elapsed, duration 778 µs
t=78425154  ODUB,stop,display     +20947 µs elapsed
            ← stopOverdubbing returns; handleMidiInput exits
t=78514404  loop_rem,idle_maint   71710 µs
t=78515361  LoopPersist start
t=78941922  LoopPersist done
t=78941973  loop_rem,persist_save 72569 µs
t=78942303  midi_gap              99482 µs
```

Same 5 s window maxes: `midi_input` 17107, `idle_maint` 73154, `load_frame` 20850, `persist_save` 72569.

Repeat stops in the same capture:

| Stop | `ODUB,stop,display` elapsed | `midi_input` | `midi_gap` | `idle_maint` | `persist_save` |
|------|----------------------------:|-------------:|-----------:|-------------:|---------------:|
| 78.404 s | 21 ms | 17 ms | **99 ms** | 73 ms | 73 ms |
| 71.728 s | 25 ms | 19 ms | **110 ms** | 75 ms | 83 ms |
| 89.366 s | 22 ms | 17 ms | **132 ms** | 78 ms | 82 ms |

## What owns which clock

| Phase | Owner | Duration | In `midi_gap`? |
|-------|--------|----------|----------------|
| Temp edit companions + undo + LCR publish + persist admit | `finalizeCommitSideEffects` → `sealPendingNoteChangesToEditPasses`, `pushOverdubSessionOnStop`, `publishPreparedOverdubPass`, `admitLoopPersist` | **2.7–5.9 ms** (`ODUB,stop,seal`) | No — inside `midi_input` |
| `sendAllNotesOff` + `resetPlaybackState` + `setState` | `Track::stopOverdubbing` | **14 ms** (`set_state`) | No — inside `midi_input` |
| Viewport + snapshot | `refreshViewportAfterOverdubStop` | remainder of 21 ms stop | No |
| First MIDI-blocking remainder | `processDeferredIdleMaintenance` → `rebuildVisualCacheIdleSlice` then `runDeferredLoadAndDisplayFrame` | **idle 72 ms + load_frame 21 ms** | **Yes** — first gap ≈ 99 ms |
| Later MIDI-blocking remainder | `processDeferredSaveState` last LoopPersist step (`finalizeDeferredLoopSlotTemp` one-shot CRC) | **73–83 ms** | Yes — second event; not the first gap |

`seal` includes the temporary-edit-pass conversion. It does not account for the 99–132 ms `midi_gap`.

## First operation that accounts for the MIDI gap

After `stopOverdubbing` returns, the first MIDI-blocking work is **`Track::processDeferredIdleMaintenance` → `Loop::rebuildVisualCacheIdleSlice`** (~72 ms), then `runDeferredLoadAndDisplayFrame` (~21 ms), with no capture MIDI drain. That sum matches the first `midi_gap` (99 ms at 78.404 s).

The 73–83 ms `persist_save` is a later LoopPersist finalize on the same stop, not the first gap, and not temp-edit / LCR.

## Debugging boundary

```text
stopOverdubbing / finalizeCommitSideEffects / temp-edit / LCR publish
    ← trusted: 21 ms inside midi_input; seal 2.7–5.9 ms
processDeferredIdleMaintenance (visual cache) + load_frame + no PLAYING drain
    ← first midi_gap
processDeferredSaveState LoopPersist last slice
    ← later persist_save; CRC reland blocked
```

## Next

1. **Now:** [`post_overdub_playing_midi_drain_bugfix.md`](post_overdub_playing_midi_drain_bugfix.md) — scheduling only.
2. Visual-cache idle grain — parked until the drain experiment reports its ceiling.
3. LoopPersist finalize grain — blocked until RAM1-safe reland.
