# Execution packet — phase 1 (tick route and miss telemetry)

From `docs/Templates/EXECUTION_PACKET.md`. Tasks 1.1–1.7. Report-only: no route, admission or emission change.

## Identity and scope

- **Owners.** Counters live in `PlaybackTickContext::gState` (RAM2, DEC-241). The increment sits at route selection
  in `Track::playCommittedLoopMidi`. The row is emitted by `RuntimeTimingTelemetry::emitWindow` through a
  `DebugSessionCapture::tickRouteRow` sink, whose format is in the carved `StopDiagRows.cpp`. The Tier-A prefix is a
  `PROGMEM` named literal in `isTierATextLine`.
- **Route classification.** Counted per slot-tick that reaches route selection, by **what is emitted**, in emission
  priority:
  - `bridge` (`fixedBridge`), then `view` (`fixedView`), then `paged` (admitted paged delivery);
  - `merged`: the merged fallback is consumed, i.e. `containPlaybackTurnIfNoRouteForSlot` returns `NotNeeded`;
  - `none`: containment applied a last resort, or a DEC-241 interrupt miss.
- **Miss counts.** `miss_int` is DEC-241's interrupt miss. `miss_ext` stays 0 until phase 5 gives the main-loop tick a
  miss path. Both are reported so the split exists from the first baseline.
- **Not counted.** Ticks that return before route selection (capture-only or no-content turns).
- **Non-goals.** Any routing change; the first-failed-predicate reason (phase 2).
- **Must preserve.** RAM1 variables 96,192; ITCM padding ≥ 840; existing rows.

## Oracle (fixed before the edit)

`test_runtime_profiler_sites` cases, driven on the production tick:

| Case | Expected route / miss deltas |
|---|---|
| main-loop tick on a stale merged fallback | `merged +1` (it builds and consumes) |
| interrupt tick on a stale fallback | `none +1`, `miss_int +1` |
| after the owner, interrupt tick | `merged +1`, no miss |

The row is pinned in `test_capture_ring`: format, Tier-A in the source list only, and fit in the 47-byte head read.

## Verification

- Focused suites, then full native.
- Three env builds with RAM1 and ITCM lines.
- Marker contract and `--audit`.
- Device baseline: one chain with a 45 s internal-clock hold.

## Completion record (2026-11-01)

- **Deviation (recorded).** `tickRouteRow` lives in `DebugSessionCapture.cpp` next to `envelopeInstrRow`. Its format is
  `SC_FMT` (`PSTR`, flash) rather than in a carved TU, which is the same placement and delivery rule as `envelope_instr`.
  The Tier-A prefix is the `PROGMEM` `kTickRouteTierPrefix`.
- **Native.**
  - RED: `test_tick_route_and_miss_counters_follow_the_consumed_route` failed with `Expected 1 Was 0`, then passed.
  - `test_tick_route_row_is_tier_a_and_fits_the_head_read` covers the format, Tier-A in the source list only, and the
    ring fallback.
  - Full native: **3,829 / 8 skipped / 3,821**.
- **Markers.** `check_marker_contract.py 'DIAG,tick_route,'` reports OK (16/31 bytes); `--audit` exits 0.
- **Build / RAM1.**
  - Default and no-grain: variables 96,192, code 391,944, padding 1,272. This is **unchanged**: the increments fit
    within existing alignment.
  - Profile: padding 872, only 32 B above the floor. That limits further profiler scopes.
- **Decoder.** `scripts/parse_tick_routes.py`, with tests 4/4.
- **Device baseline.** `evidence/2026-11-01-baseline-routes.md`.
