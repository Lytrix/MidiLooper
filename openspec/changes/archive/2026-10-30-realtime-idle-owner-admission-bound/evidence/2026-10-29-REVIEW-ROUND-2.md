# Review round 2 — contract holes closed

**Date:** 2026-10-29
**Change:** `realtime-idle-owner-admission-bound`
**Status:** artifacts corrected; one code correction applied and verified. **No architecture code yet.**

## Corrections applied

| # | Hole | Resolution |
|---|---|---|
| 1 | 4.2 assumed the final span is known at acceptance | Required coverage is **"phases touched so far"** with three cheap, non-deriving transitions: NoteOn marks the pressed tile, held advancement marks each newly crossed tile, NoteOff/stop seals the span. "Repeated wraps add nothing" is now precise: nothing **unless a held note continues into an as-yet-untouched phase**. |
| 2 | `private_hold` had no replacement task | Third prerequisite **`HeldParticipationAtPhase`** added and wired in 4.7 in place of `ensureOverdubSourceNotesForHold`'s synchronous derive (the 94.9 ms breach), with a fixture that proves the path can no longer derive. |
| 3 | A tile was treated as an execution bound | Split into two concepts: the **tile** is the spatial unit of correctness/publication atomicity; the **grain** is a resumable event/operation cursor *inside* a tile. Core state machine recorded in `design.md`. 4.1b now sizes both separately, **not** against the owner budget; 4.6 gains a RED for a worst-case dense tile + long closure. |
| 4 | Closure had no composition rule | Composition is by stable **note identity**, never concatenation; `maxCommittedNoteSpan >= loopLengthTicks` **saturates to the whole loop** instead of ambiguous modular ranges. In the spec and in 4.10. |
| 5 | 4.4 invalidated the wrong thing | `requiredCoverage` is session state and **survives** a generation change; `preparedCoverage` and in-flight preparation are invalidated and **every required tile becomes pending again**. The contradiction with monotonicity is gone. |
| 6 | `OccupancyAtPhase` characterized but unwired; ordering loop | Wired in 4.7 with an explicit order: **accept the note → mark the pressed tile → consult the ledger**. "Fallback refused" is never read as "not occupied"; an unanswerable ledger answer becomes a named owed prerequisite. |
| 7 | Stale remnants; **25 ms budget correction never applied** | `2.3`'s stale "see 4.3/4.4" now points at 4.6/4.8. Completion-mask, window-transition and assembled-cache text removed from Risks/Migration/Open Questions. **`kOwnerWorkBudgetUs` corrected 25 ms → 8 ms**, derived from the measured MIDI clock service interval (~20.8 ms), not from half the alarm. |

## The budget correction (code, verified)

`kOwnerWorkBudgetUs` was 25 ms — larger than one MIDI clock service interval (measured
`DIAG,service_period,20832`, 24 PPQN at ~120 BPM), so an uninterrupted child could spend it and still
miss the next clock edge. That is exactly the tempo wobble this change exists to remove.

- New value **8 ms**, with `kMidiClockServiceIntervalAnchorUs = 20832` recorded beside it and the rule
  stated: `budget + turn overhead < minimum clock interval`.
- `test_riao1_budget_fits_inside_one_midi_clock_interval` asserts `budget × 2 ≤ anchor` and
  `budget < alarm`; the old "half the alarm" reasoning is called out and rejected in the comment.
- The turn-budget accounting also had a real bug this exposed: a finer-grain child that took the
  remaining deadline was still charged its **declared maximum**, so a plan could over-charge past the
  budget it was judged against. It now charges **what was granted**. Caught by the permutation fixture.

Verification: `test_realtime_idle_owner_admission` 10/10, `test_midi_service_drain` 42/42, build SUCCESS,
RAM1 `variables 96,192` and ITCM `392,056 + 1,160` unchanged.

## The state machine now recorded in `design.md`

```text
NoteOn
  → mark pressed tile required
  → occupancy prerequisite
  → no derive

Held advancement
  → mark newly touched tile(s) required
  → held-participation prerequisite
  → no derive

Deferred owner
  → resumable event/operation grain
  → build one tile privately
  → atomically publish tile prepared

NoteOff / stop
  → mark complete span required
  → SpanFinalization waits for all required tiles
  → compute overlap once ready
```

## Not done

Nothing of the architecture is implemented. `tasks.md` §4 remains the plan (4.1a three prerequisites,
4.1b two envelopes, 4.2 transitions, 4.4 generation, 4.6 worker + dense-tile RED, 4.7 wiring, 4.10–4.15
parity/acceptance). No device run, nothing flashed or committed.
