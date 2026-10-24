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

## Device gate (3.7e launch/handoff) — PASS on the qualification image

- Image: 2.2 qualification `77a8f5622aad36ad` (the `DIAG,win_gate` rows are qualification-only; the shipping image
  never compiles them, which is why the earlier shipping rerun reported “no win_gate row”).
- Run: `windowed_content_parity`, track 8 / slot 4 / channel 8, 32 bars, switch target slot 5, settle 170 s;
  report `captures/2026-10-24-2.2-s37e-device-qual/host_midi_hitl_windowed_content_parity_20261024_231358.json`.
- `switch_target_span_alignment`: **PASS** (target t=7 s=4 span-aligned under transport; the 2.2 commit seed was
  adopted and served).
- `snapshot_owner_continues_after_stop`: session_exit_rows=170 eligible_rows=170 (the qualification snapshot term).
- `capture_evidence_ring_intact`: one host Tier-A eviction (`tier_a_evicted=1`, stats `tierA_evict=3`). **Excluded by
  the owner's explicit decision** on this run: it has a separate host capture-ring root cause and is not a 2.2
  firmware/launch term. The exclusion applies only to this gate; the host ring root cause is tracked separately.

The 2.2 **launch-seed increment** is now native- and device-qualified. The 2.2 family's remaining work is the
`FixedPlaybackBridgeStorage` / `resolveFixedPlaybackSourceForTurn` removal and the cold-wrapper retirement; the 2.2
checkbox stays open until those land.
