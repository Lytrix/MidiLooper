# Wrap-head vs last-Off pairing

**Status:** Closed — native RC1–RC3 shipped; 174646 device recapture waived (2026-09-08)


**Date:** 2026-09-08  
**Kind:** bugfix  
**Evidence:** [`session_20260908_174646.log`](../../captures/session_20260908_174646.log)  
**Does not reopen:** 4.4e, close bake, Overlay-as-display, hydrate Stage 4a

## Debugging boundary

```text
4.4e snapshot → NOTE_EDIT / close bake
    ← closed; do not reopen
USB / SEVT NoteOn membership for overdub D#
    ← complete 14/14; not a missing overdub On
NoteUtils wrap-tail + head-Off pairing / reconstruct spans
    ← this plan RC1–RC2
Track::prepareRecordStop last On vs stop Off
    ← RC3 (same capture, different owner)
Live grow-until-Off between On and Off
    ← out of scope (logged; not this pairing slice)
```

## Problem

Display treats a tail-window NoteOn and a later same-pitch head-window NoteOff as one wrap-held note, even when that On already has a linear Off, or when a second note already owns that Off. The painted wrap head starts at tick 0 and runs to that last Off. Record stop produces the same tick-0 head.

## Record stop — same issue

Yes. First record stop in [`174646`](../../captures/session_20260908_174646.log) (`RECORDING → STOPPED_RECORDING` at 24.032 s).

Last USB is pitch **81** On at 23.956 s, still held at stop (no USB Off). After 7 bars the unwrapped On sits past quantized length 5376; `SEVT` has no On near phase 153.

| Store / paint | Pitch 81 |
|---|---|
| `SEVT` | `N,144` `F,168` `F,192` … `N,4752` `F,4800` |
| reconstruct / effective `DNTE` | `81@144` length **24** (Off 168); `81@4752` length **48** |
| frame `DNTE` | `81@144` length **48** (Off **192**) |
| `WRAP` diagnostic | `WRAP,4752,168` (`isWrappedLoopNotePair`, first matching Off) |
| Live `draw_src` 25.088 s | `id,100` `old,81,4752,4800` `new,81,0,192` |

Tick-0 head `81@0–192` is the last Off of that pitch (stop Off 192) paired as wrap-head for `N,4752`.

`Track::stopRecording` calls `setState(TRACK_STOPPED_RECORDING)` before `prepareRecordStop`. `capturePhaseTick` then uses `tickPhaseInLoop` (Off **192**). Live record stored the On as unwrapped `currentTick - startLoopTick` (~5529). `dropEventsAtOrBeyondTick(5376)` removes that On and keeps `F,192`.

`CLN,record,wrap_synth,0` — this Off is stop finalize, not wrap synth.

## Overdub D# — same pairing class

USB and `SEVT` both have 14 D# Ons. No missing overdub On. Bar-1 long D# is the wrap split of `N,5368` (`WRAP,5368,576`).

| Source | D# @0 | D# @192 |
|---|---|---|
| occupy 52.252 s | wrap span `as=5368,ae=576` occupied at `hs=192` | incoming On |
| `SEVT` | no `N,0`; `N,5368`; two `F,576` | `N,192` |
| effective `DNTE` | `0,191` | `192,384` |
| frame `DNTE` | `0,576` | `192,0` |
| Live after wrap Off | first `draw_src` 39.622 s `id,139` `old,63,0,576` | — |

Frame keeps the wrap head through the last Off at 576 and zeros the 192 note. Effective already has the occupy split.

## Code owners

| Stage | Owner | What it does on this capture |
|---|---|---|
| RC1 | `NoteUtils::isPreferredWrapTailForHeadOff` / `isPreferredWrapTailForHeadOffInSpanBuild` / `tryPairWrappedTailOn` / `buildCanonicalSpansFromMidi` | `isHeadTailWrappedPair` is true for `4752+192` and `4752+168` (1-bar window, delta > L/2). Preferred tail is the last On in the tail window. A linear `F,4800` on that On is not a veto. |
| RC2 | same reconstruct span build; frame adopt of wrap exclusive end | Two `F,576` (D#) or `F,168`+`F,192` (81): wrap head exclusive end uses the last matching head Off. |
| RC3 | `Track::stopRecording` → `prepareRecordStop` + `capturePhaseTick` + `LoopEventStore::dropEventsAtOrBeyondTick` | State already `STOPPED_RECORDING`; stop Off is loop-phase; record On is unwrapped; drop removes the On. |

`WRAP` CAP in `Track` deferred stored verification uses `isWrappedLoopNotePair` and the first matching Off. That line is diagnostic only. Reconstruct `DNTE` for 81@4752 stayed linear 48; live/frame used the preferred wrap pair.

Do not replace LIFO with FIFO (DEC-023). Consume each Off once. A tail On that already has a linear Off is not a wrap tail for a later head Off.

Existing reconstruct fixture `test_reconstruct_wrap_pair_blocked_by_intervening_note_on` blocks wrap when another On sits between tail and head Off. It does not block wrap when the tail On already has its own linear Off.

## Architecture checkpoint

| Question | RC1–RC2 | RC3 |
|---|---|---|
| Ownership change? | NO — extend `NoteUtils` pairing | NO if finalize tick source stays on `Track::prepareRecordStop` |
| State-transition change? | NO | **YES if** `setState` / finalize order is swapped — stop and pick an option first |
| Unbounded MIDI-interval owner? | NO | NO — stop path, already bounded |

RC3 option that reorders `setState` vs finalize is a formal stop. Prefer keeping transition order and fixing tick identity of the last record On/Off pair.

## Invariant

```text
A head Off wrap-closes a tail On only when that On has no linear Off
and that Off is not already the closer of another same-pitch On.
Record stop must not leave an in-loop Off whose matching On was dropped
past loopLengthTicks.
```

## Stages

### RC1 — linear Off vetoes wrap-tail

Native fixture from 81: `N,4752` `F,4800` plus `F,168` and/or `F,192` at L=5376.

After reconstruct:

- `81@4752` end 4800 (linear)
- no wrap-held `4752 → 168` or `4752 → 192`
- no display head `81@0–192` from that On

Extend `isPreferredWrapTailForHeadOffInSpanBuild` (and the non-span twin) so a linear Off for that tail On returns false. Do not add a painter workaround in `drawNoteBar`.

### RC2 — each Off closes one On

Native fixture from D#: `N,5368` `N,192` two `F,576` at L=5376.

After reconstruct / frame:

- wrap split `5368` tail + head ending **191** (incoming On), or occupy-equivalent
- `192–576` kept
- no `0,576` + `192,0`

Host from the 174646 event list. Device recapture only after RC1+RC2.

### RC3 — record stop On/Off tick identity

Native fixture: record-relative On past quantized length, `setState` already stopped, `prepareRecordStop`.

Must not keep `F,192` without the matching On. Do not swap `setState` / finalize without a design session.

Candidate that stays on the existing owner: map the last record On into the final loop before drop, or drop the stop Off when its On was dropped. Pin which in the fixture before firmware.

## Out of scope

- Live grow-until-Off: first record `84` On 9.579 / Off 9.745 / first `draw_src` 9.860 as `84,0,48`; wrap D# first paint after Off. Different owner (`applyCapturePlayheadTails` / capture preview).
- 4.4e snapshot oracles, close bake, hydrate 4a, Overlay-as-display
- Raising snapshot idle `N`
- Changing DEC-023 LIFO to FIFO
- Treating `WRAP` CAP as reconstruct authority

## Tests

| Gate | What |
|---|---|
| Native RC1 | `test_noteutils_reconstruct` — 4752/4800 + extra head Off must stay linear |
| Native RC2 | same suite — 5368 wrap + 192 + two Offs at 576 |
| Native RC3 | `test_loop_event_store` — `N,5529` + `F,192` → `N,153` + `F,192` |
| Device | recapture after RC1+RC2: no `draw_src` `81,0,192` from `4752`; D# frame `0,191` + `192,384` (or occupy-equivalent), not `0,576` + `192,0` |

`pio run -e teensy41-capture-serial` (RAM1 locals ≥ 4000) then `pio test -e native` before any stage commit.

## Pre-implementation review (plan time)

### Ready

- Owners named; 174646 pins record stop and overdub D# as one pairing class
- RC3 owner and transition hazard named

### Resolved

| Topic | Decision |
|---|---|
| Missing overdub D# On? | No — USB and `SEVT` 14/14 |
| Record stop same visual? | Yes — `81@0–192` from last Off 192 + `N,4752` |
| LIFO | Keep; consume each Off once |

### Open before coding

1. RC3 tick mapping: remap last On into the loop vs drop orphan Off — **resolved: remap** (`LoopEventStore::mapNoteOnsAtOrBeyondTickIntoLoop`)
2. D# expected head end after occupy: **191** inclusive (`0–191` + `192–576`)

## Implementation (2026-09-08)

| Stage | Status | Owner | Native fixture |
|---|---|---|---|
| RC1 | shipped | `isPreferredWrapTailForHeadOffInSpanBuild` + `isWrapHeldOpenNote` | `test_reconstruct_linear_off_vetoes_wrap_tail_174646_pitch81` |
| RC2 | shipped | `occupiedWrapHeadOffTick` / `pushWrapHeldCanonicalSpan` | `test_reconstruct_shared_head_off_shortens_wrap_head_174646_pitch63` |
| RC3 | shipped | `prepareRecordStop` → `mapNoteOnsAtOrBeyondTickIntoLoop` | `test_map_overflow_note_on_keeps_in_loop_stop_off` |

Linear-closer veto is reconstruct-only (`InSpanBuild` + `isWrapHeldOpenNote`). Public `isPreferredWrapTailForHeadOff` stays unchanged so wrap-home edit apply can still find the wrap Off when a leftover linear Off exists (`test_201446_wrap_home_move_drops_false_linear_off`).

RC3 remaps overflow NoteOns (`tick % loopLength`) then drops remaining overflow. `setState` / finalize order is unchanged.

Capture-serial RAM1 locals **6848**.

## Device recapture

### [`185428`](../../captures/session_20260908_185428.log)

Loop **3840** (not 174646’s 5376). Transitions: `ARMED → RECORDING → STOPPED_RECORDING → PLAYING → OVERDUBBING → STOPPED`.

| Gate | Result |
|---|---|
| RC1 `draw_src` `81,0,192` from `4752` | **Not reproduced.** Last USB 81 is at 22.517 s and is released before stop. No `4752`. Only 81 `draw_src` is record grow `912–960`. |
| RC3 overflow On at record stop | **PASS (analog).** Last USB 77 held through stop. `raw=4176` `final=3840` `phase=336`. `SEVT` `N,336` + `F,336` + `F,384`. reconstruct / effective / frame `77@336` length **48**. No `draw_src` `77@0`. |
| RC2 D# frame `0,576` + hidden 192 | **Frame occupy-equivalent PASS; 174646 store shape not reproduced.** `SEVT` has `N,0` (174646 did not). One `F,576`, not two. reconstruct `63@0` length **576**. effective + frame `63@0` length **95** plus `63@96` length **96** and `63@288` length **240**. Not `0,576` + `192,0`. |

174646-specific RC1 recapture **waived** (user accepted native + `185428` RC3 analog). Reopen only if a later capture matches `N,4752` + leftover head Off + `draw_src` `81,0,192`.
