# Tasks 4.1a / 4.1b — prerequisites, obligations, tile and grain envelopes

**Date:** 2026-10-29
**Change:** `realtime-idle-owner-admission-bound`
**Status:** contract implemented and tested. **No production wiring.**

## What landed

`include/Utils/CoveragePrerequisite.h` — the contract the production wiring must satisfy:

| element | content |
|---|---|
| tile model | 4-bar tiles, `tileCount`/`tileIndexForPhase`/`markSpanTiles` (wrap-aware: a span crossing the loop end marks the last and first tiles) |
| envelope | **64 tiles** (one 64-bit requirement/prepared word, 256 bars); above it the caller takes a named whole-loop fallback rather than inventing partial coverage |
| obligations | `ObligationKey{noteId, tile, kind}`, `ObligationSet` with **coalescing**, capacity **64**, overflow a named refusal, `requalify(generation)` and `dischargePreparedTiles(mask, generation)` |
| prerequisites | `occupancyAtPhase(tilePrepared, ledgerAnswered)`, `heldParticipation(tilePrepared)`, `spanFinalization(preparedMask, spanMask)` — each `Ready`/`Owed` |
| execution grain | `GrainCursor` + `grainExhausted` bounded by **events/operations (64/64)**, not by tile or microseconds |

The note-on ordering rule is encoded in `occupancyAtPhase`: ready requires *either* a prepared tile *or*
a ledger answer, so a refused source-view fallback can never be read as "not occupied".

## The four obligation REDs (review round 2, item 1)

All four are in `test/test_coverage_prerequisite` and pass:

1. **Held note leaves an unprepared tile, tile prepares later** → the consequence discharges **exactly
   once**; a replayed completion changes nothing.
2. **Occupancy owed, note completes before its tile prepares** → finalization is owed too, and both
   consequences discharge once when the tile prepares — the delta is computed once, not twice and not
   against partial coverage.
3. **Many hold ticks inside one unprepared tile** → 1000 `owe` calls stay **one** obligation; distinct
   kinds on the same note/tile stay distinct.
4. **Generation change** → obligations are retained, re-qualified to the new generation, and
   old-generation prepared bits cannot discharge them. Required coverage survives as session state.

The obligation set is also proven bounded (capacity 64, overflow refused by name), and the sparse case
(notes at bar 2 and bar 27 requiring exactly two tiles, middle untouched) passes.

## Budget contract corrected again (review round 2, item 2)

The earlier 8 ms figure was still wrong: it was justified against a measured **120 BPM** interval
(~20.8 ms), but the enforced tempo range caps at **300 BPM** (`ClockManager::setBpmFloat`,
`MidiConfig::TEMPO_MAX_BPM`), where 24 PPQN gives **8333 µs**. At 8 ms only ~333 µs of residual would
remain.

- `kMinimumSupportedClockIntervalUs = 8333` (300 BPM × 24 PPQN).
- `kOwnerWorkResidualUs = 4000` (provisional; measured on device **at the fastest tempo** in task 5.4).
- `kOwnerWorkBudgetUs = 4000`, so `budget + residual <= interval` holds with 333 µs to spare.
- Registry maxima rescaled to fit (source view/stop grain/wrap/provision 4 ms; cache/canonical/snapshot
  3 ms; playback window 2 ms), so no child is unbounded-by-construction against the budget.
- `test_riao1_budget_fits_inside_the_fastest_supported_clock_interval` asserts the contract; the
  25 ms and 8 ms drafts are both rejected in the comment.

A test bug surfaced and was fixed alongside: `IdleOwnerTurnBudget` charged a finer-grain child its
**declared maximum** even when it took the remaining deadline, so a plan could over-charge. It now
charges the grant.

## Verification

| check | result |
|---|---|
| `test_coverage_prerequisite` | **10/10** |
| `test_realtime_idle_owner_admission` | 10/10 |
| `test_midi_service_drain` | 42/42 |
| **full `pio test -e native`** | **3678 cases / 7 skipped / 3671 succeeded / 0 failed** |
| `pio run -e teensy41-capture-serial` | **SUCCESS**, RAM1 `96,192`, ITCM `392,056 + 1,160` |

## Not done

No production file consumes `CoveragePrerequisite` yet: `Loop` has no tile state, the worker does not
exist, and none of the three prerequisites is wired. Next is **4.2** (acceptance marks tiles; the three
transitions) and **4.6** (the worker with its dense-tile RED), then **4.7/4.7a** wiring with the
obligation REDs above becoming production-path assertions.
