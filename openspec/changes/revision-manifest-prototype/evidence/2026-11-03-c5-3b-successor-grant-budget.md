# C5.3b: the deadline successor's grant budget (owner ruling 2026-11-03)

## Rule

A rolling deadline successor's next grant is **due** at:

```
due = from + (N.end - guard - from) / remaining
  from      = its last grant (or its request)
  remaining = bounded remaining grants:
                before admission: G(E) of the manifest's densest bound, less the grants served
                after admission:  the grants at admission + the priced remainder, less the grants served
  guard     = 64 ticks (one measured lane-turn gap is 32-56)
```

- A due successor is served at that opportunity. Neither another holder nor the competing owner (the device
  injector) may withhold it (`PlaybackHolder::grantDue`, `selectPlaybackHolderBounded`, `playbackHolderMustBeServed`).
- Slack earned by early grants lets a later gap be longer. A successor that falls behind tightens its own next due.
- The 3-denial cap stays as a secondary bound for now; the budget subsumes it.
- Successor pipeline: measurement image only, as before.

## Host

- `test_5_1_a_due_successor_grant_is_never_withheld`: a due successor takes the opportunity from a rotation as far
  from it as possible, and the competing owner cannot withhold it. Not due: ordinary round-robin.
- `test_5_1_a_tight_successor_is_served_by_its_grant_budget`: `main.cpp`'s lane (bounded selection, the injector,
  opportunity notes), a routed successor at a 192-tick lead under deny 7.
  - **With the budget:** 52 of 53 handed, 0 missed, 1,033 grants withheld.
  - **Mutation (cap only):** 0 of 7 handed, 7 missed.
- FULL (escalated by `MidiServiceDrain.h`): 147/147 suites, 3,961 cases, 0 failed, 0 errored.

## Device

succprobe HEX sha256 `aa9578e3011ef79fcd98aab76604b1eef45ac2827a5b86575e9e4b6337c379c5`,
`captures/session_20261103_112121.log`, boot `crash=0`. 32 dense bars, a 32-bar overdub, stop, restart, then
`0:1536:20` and `7:1536:20`. Routed `win_blocked` 0, bridge ticks 0. The first window after the restart served at
13.3 s.

| point | handed / missed | min margin | grants withheld | grants served because due | lateness vs due | lane-opportunity gap while owed |
|---|---|---|---|---|---|---|
| deny 0 | 20 / 0 | 848 | 0 | 0 | 0 | 40-48 |
| deny 7 | 21 / 0 | **24** | 796 | 16-20 of 16-20 | **23-46** | 40-64 |

New per-seam row: `succ_due,<outcome>` — actual lead, estimated grants, grants served, max lateness against the due
tick, grants served because due, max opportunity gap while owed, N.end.

**Reading.**

- **Deny 7, the case the 3-denial cap alone cannot hold, passes at a full lead.** Every grant was served because it
  was due, and none was more than one lane-opportunity gap late. The 64-tick guard absorbs that lateness, which is why
  the margin bottoms out at 24.
- **The outer bound is the lane turn itself.** Before the restart (the post-overdub commit phase, probe on from boot,
  deny 0) two seams missed with 376-384-tick opportunity gaps: the lane was not offered for about 0.4 s, so no rule
  could serve it.
  - 111.2 s: lead 1,160, 4 of 20 grants, lateness 353.
  - 125.1 s: lead 1,168, 5 of 20 grants, lateness 318.
  - This is the evidence that a one-window lookahead needs either earlier preparation or an outer scheduling guarantee
    while that phase's work runs.
  - The previously unexplained pre-restart `succ_seam,missed` rows are very likely the same shape.
