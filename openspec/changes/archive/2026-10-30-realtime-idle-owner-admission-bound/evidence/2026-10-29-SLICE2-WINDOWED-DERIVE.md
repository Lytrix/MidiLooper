# Slice 2 — windowed overdub source view (tasks 4.2/4.3/4.4)

**Date:** 2026-10-29
**Change:** `realtime-idle-owner-admission-bound`
**Status:** implemented, native verified; **not wired to admission, not device-verified, not committed.**

## What changed

`Loop::rebuildOverdubSourceSpanCache` no longer derives the whole pass. Both whole-pass
`LoopContentResolution::resolveDisplayNotes(passes, …)` calls are gone:

1. the `Passes` seed now calls `resolveDisplayNotesInWindow` over a **loop-aware padded window**
   (`Loop::resolvePaddedOverdubSourceWindow`: one bar each side, pad taken modulo the loop); and
2. the `sourceOrigin != Passes` `NoteId` merge (`Prior`/`Visual`/`Prepared` — the capture's `from=prior`
   shape) merges from the same padded windowed derive.

The cache now records **what it actually covers** (`overdubSourceSpanCacheCoverageStart_/_Length_`), and
a new predicate `Loop::overdubSourceSpanCacheCoversWindow(start, length)` answers reuse. Both consumers
use it — the wrap/idle refresh (`rebuildOverdubSourceView`) and the note-hold merge
(`mergeOverdubSourceViewForHoldContext`) — so neither re-derives content it already holds nor serves
content it does not. `overdubSourceSpanCacheReusable()` is now "covers the whole loop"; `ready()`
(full-loop) and therefore the canonical-preparation priority gate are **unchanged**.

The coverage rule itself moved to one shared function, `WindowCoverage::windowCoversWindow`
(`include/Utils/WindowCoverage.h`), used by both the owner-admission predicate
(`MidiServiceDrain::idleOwnerSourceViewCovers`) and the content owner. It cannot drift between the two.

## Why the pad had to change

Measured earlier in this change: with the visual-cache slice's clamped pad a wrap window resolved **0 of
3** notes. The pad is loop-aware here, and `RIAO-2.B` pins it.

## Structural cost

`sizeof(Loop)` **1,848 → 1,856 B** (+8: the two coverage words). Shells are `extmem_malloc`ed and the
pin is re-based explicitly with the reason in `src/Track/TrackPlaybackRuntime.cpp`. RAM1 is unchanged
(`variables 96,192`); ITCM unchanged at `392,056 + 1,160`.

## Verification

| check | result |
|---|---|
| `test_overdub_source_window_parity` | **11/11** |
| `test_shared_capture_finalization` | 400/400 |
| `test_overlap_candidate_lookup` | 13/13 |
| **full `pio test -e native`** | **3668 cases / 7 skipped / 3661 succeeded / 0 failed** |
| `pio run -e teensy41-capture-serial` | **SUCCESS**, RAM1 `96,192`, ITCM `392,056 + 1,160` |

### The production path is provably window-bounded (task 4.3 acceptance)

A test-only seam reports the production rebuild's own resolution cost (`RIAO-2.Q`, DMAMEM counter, no
RAM1 cost). On a 31-bar / 310-note pass:

| request | derive ops | share of whole pass |
|---|---|---|
| whole loop | 311 | 100 % |
| 16-bar window | 181 | 58 % (18 of 31 padded bars) |
| 4-bar window | 61 | 20 % (6 of 31 padded bars) |

Before this slice all three were 311. The assertion fails if the request is answered by a whole-pass
resolve.

### Cost profile for task 4.5 (`RIAO-2.P`, host, relative only)

| derive | notes | ops | host µs |
|---|---|---|---|
| whole pass | 310 | 311 | 523 |
| 16-bar window | 160 | 161 | 221 |
| 4-bar window | 40 | 41 | 52 |

Cost is **near-linear in the window's note count** (1.3–1.7 µs/note host), not superlinear. Applying the
device/host ratio from the measured 201 ms whole-pass span (≈375×) puts a 16-bar derive at ~83 ms and a
4-bar derive at ~20 ms.

## The 4.5 fork this measurement exposes

Sub-windowing bounds **steady-state** refresh: a 4-bar grain is ~20 ms on device, inside the 25 ms owner
budget. It does **not** bound a **coverage transition**: when the requested window moves outside the
cache's coverage (a wrap with a displaced viewport), the view may not be used until the derive completes,
and a 4-turn derive cannot complete at the wrap. The coverage rule forbids serving the old window, so a
transition either needs the derive to finish inside one turn or needs the window itself to be smaller.

That is a correctness decision, not a scheduling tweak — shrinking `kOverdubSourceWindowBars` shrinks the
overlap candidate domain. **Not implemented; needs an owner decision.**
- **Task 3 — the report-only `persist_save` named-step row.**
- **Slice-1 admission wiring** (`runDeferredIdleMaintenance`, `SEVT`/verification denial), which by D3
  lands in the same commit.
- No device run; nothing flashed or committed.
