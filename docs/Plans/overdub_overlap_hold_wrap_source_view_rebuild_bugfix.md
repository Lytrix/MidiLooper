# Overdub wrap source-view rebuild (RC6)

**Status:** Native in this commit; device gate open  
**Date:** 2026-08-17  
**Kind:** bugfix  
**Parent:** [`overdub_overlap_hold_same_start_bugfix.md`](overdub_overlap_hold_same_start_bugfix.md)  
**Evidence:** [`005745`](../../captures/session_20260817_005745.log) 1-wrap PASS; [`004947`](../../captures/session_20260817_004947.log) multi-wrap stale source view

---

## Invariant

Every committed overdub wrap becomes part of the source geometry for the next wrap.

## Fix

`Track::commitOverdubWrapAtSessionStart` after seal + `publishPreparedOverdubPass`:

`Loop::rebuildOverdubSourceView` — `tryResolvePreparedWindow` then `reconstructDisplayNotes`; miss uses windowed `LoopContentResolution::resolveWindow(passes)` (RC5 per-pass). Not visual cache. Not `establishOverdubSourceView`. Consume unchanged.

## CAP tokens

| Token | Meaning |
|-------|---------|
| `DIAG,lcr,vch` | Idle prepared window → visual cache (was `6a`) |
| `DIAG,lcr,src,why=open,from=prep\|win\|span` | Session-start rebuild (RC7; was `6c` / `why=open` without `from`) |
| `DIAG,lcr,src,why=wrap,from=prep\|win\|span` | Wrap rebuild |

Historical captures keep `6a` / `6c`.

## Device gate

1-bar same-start-longer two wraps (64–240 then 64–288). Select at tick 64: one 60, longest. Wrap emits `src,why=wrap`; wrap `beginCapture` must not emit `why=open`.
