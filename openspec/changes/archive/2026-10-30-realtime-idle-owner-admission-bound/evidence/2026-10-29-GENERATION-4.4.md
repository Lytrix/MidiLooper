# Task 4.4 — coverage state wired to the real session and generation

**Date:** 2026-10-29
**Change:** `realtime-idle-owner-admission-bound`
**Status:** wired, native verified, build green. Worker and prerequisites still unwired.

## Where the state lives, and why

`CoverageState` **cannot** live in `Track`: `sizeof(Track) == 384` is a hard DTCM pin for the eight-way
array, and adding a member is not available at any price. The overdub session is per loop, so the state
belongs in the per-slot `Loop`, whose shells are `extmem_malloc`ed.

`sizeof(Loop)` **1,856 → 1,888 B** (+32 with alignment). The pin is re-based explicitly with the reason in
`src/Track/TrackPlaybackRuntime.cpp`. RAM1 `variables` is unchanged at `96,192`; ITCM `code 392,056 →
392,120` (+64 B) with padding `1,160 → 1,096`, leaving **256 B above the approved 840 B floor**.

## The two wiring points

| edge | call | why there |
|---|---|---|
| `Loop::openOverdubSession` | `overdubCoverage_.beginSession(loopLengthTicks, playbackRevision)` | a new session starts with no required tiles for its generation; nothing is derived |
| `Loop::notifyCommittedContentChanged` | `overdubCoverage_.changeGeneration(playbackRevision)` | the single committed-content edge already used to retire the private scan and stale derived views — the one place a source generation legitimately moves |

`notifyCommittedContentChanged` is the right hook rather than a new call site: it is reached on every
commit path (overdub commit, record commit, undo/redo re-materialisation), and it already carries the
C3.5 "committed revision moved" contract.

## Verification

`RIAO-2.U` drives the **production** edge, not the primitive:

1. a 31-bar loop with content at bar 2 and bar 27;
2. `beginSession`, two `markNoteOn` calls, both tiles published prepared → `owedCount() == 0`;
3. `notifyCommittedContentChangedForTest()` (the same private function every commit reaches);
4. asserts `owedCount() == 2` (both re-owed), both tiles still **required**, both **not prepared**.

| check | result |
|---|---|
| `test_overdub_source_window_parity` | **15/15** |
| `test_coverage_prerequisite` | 20/20 |
| **full `pio test -e native`** | **3692 cases / 7 skipped / 3685 succeeded / 0 failed** |
| `pio run -e teensy41-capture-serial` | **SUCCESS**, RAM1 `96,192`, ITCM `392,120 + 1,096` |

## Not done

- Nothing **marks** tiles yet: no capture path calls `markNoteOn` / `markHeldAdvance` / `markSpanSealed`
  (task 4.7). Until that lands, required coverage is empty and the worker has nothing to do.
- `stepTilePreparation` still has no owner turn.
- The obligation set is not yet resident (it is deliberately out of `Loop` until 4.7 justifies the
  footprint).
- `resolveOverdubSourceWindow` is still the view's correctness mechanism (task 4.5/4.8).
