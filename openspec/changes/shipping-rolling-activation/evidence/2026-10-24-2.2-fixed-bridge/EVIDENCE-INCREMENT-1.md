# 2.2 fixed/bridge — increment 1 evidence (rolling launch seed landed)

Date: 2026-10-24
Branch: `feature/unified-rolling-playback-path`

## Production change (rolling path; cold wrapper retained)

- `Track::commitQueuedPlaybackStart`: when the **bind of this commit** made a launch (`Bound` and
  `launchBoundTick == commitTick`), seed the rolling frame from `launchBoundTick` (the 3.7e adopt).
- `Track::resolveRollingPlaybackWindow`: allow the just-seeded frame through the queued-start guard
  (`launchedThisTick`), so the commit tick's consumer sees `Serving`.
- `Track::bindPlaybackLaunchForSlot`: clear the adopted window's `viewBuildWork`; the served launch is no longer
  charged as an outstanding sibling against the next interval's admission.
- The rolling resolver (`resolveQualificationPlaybackSourceForTurn`) no longer falls back to `boundLaunchView`.
  The **cold `ROLLING=0` hot path keeps `boundLaunchView`** as the compatibility source until 2.5 (owner decision).

## Test fixture re-point (not a waiver)

- `setUp` resets the test-global `sInterferenceBudgetEnforced` (a failing test aborts before its restore, which
  otherwise leaves the budget enforced for every later fixture — the cross-test `11`).
- The shared `assert_s4b_7` helper: stamps the turn's deadline clock and asserts the seeded frame is **Serving** at
  the first target tick (the transition RED). It fails with the wrapper removed and no seed.
- `dropLaunchBinding` also clears the seeded rolling frame; the demand-path fixtures call it. Seven demand tests
  (selector/empty/refused/budget/reuse/interval-ab/closure) drop the seed explicitly.
- The closure fixture releases the launch's LEDGER notes (`sendAllNotesOff`) after the drop, so the whole-loop
  closure names exactly the fixture's own obligation; the shared drop stays note-neutral for the emission fixtures.
- The refused-shape fallback fixture expects the dense fallback's **600** events (the launch is dropped in the
  demand fixture); the launch-seed emission is covered by the 3.7e fixtures.

## Gates

| Gate | Result |
|---|---|
| RWO | **243/243** |
| Full native | **3432 cases, 7 skipped, 3425 passed** |
| Shipping build | RAM1 **96,192** / ITCM **392,040 + 1,176** / locals **34,880**; FLASH 1,197,632 |
| Qualification build | same pins; FLASH 1,202,816 |
| Shipping HEX | `14e2192e0ff3868a` |
| Qualification HEX | `77a8f5622aad36ad` |

## Still open

- **3.7e device launch/handoff gate** (shipping image) and the rollback record. 2.2 stays unchecked until it lands.
- The 2.2 family's bridge‑storage removal and the cold‑compatibility retirement remain the rest of 2.2/2.5.
