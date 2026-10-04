# Task 3.10 — activation audit: evidence

Packet: [`EXECUTION.md`](EXECUTION.md) (written before the fixtures). Slice: **verification-only** (no production line
edited, no gate flipped). Contract: **DEC-108** §13c (an unqualified demand fails closed) and §13b (the enforced derived
admission).

## What the audit found

**The production demand that exists today carries a qualified stamp, and the two other paths that reach window state are
deliberately exempt.** That is what activation was waiting on, and it is now pinned rather than assumed:

| Path | Reaches window state via | Position | Verdict |
|---|---|---|---|
| the lane turn's demand | `Track::serviceRollingPlaybackWindow` → `requestCommittedPlaybackWindow(slot, coverage, clock)` — the **only** production window request | `clockManager.getPlaybackDeadlineClock()`, read in `main.cpp`'s `runDeferredIdleMaintenance(PlaybackViewMaintenance)` | **stamped**; refuses with `Excluded` *before* promising anything when the epoch is zero (AA-1) |
| the launch | `TrackManager::servicePendingSlotProvision` → `Track::preparePlaybackLaunchForSlot` → the retry admission | none: `clockEpoch == 0`, `deadlineTick == 0` by construction | **exempt by design** — it waits for readiness instead of owing it by a tick, is never judged late, and prepares and binds with the interference budget enforced (AA-2) |
| the capture edge | `Track::captureIntervalAdmissionForSlot` | no clock is taken at all | **accounting only** — it is a capacity answer about the interval being captured, not a promise |
| preview / undo requesters | `Track::requestPreviewWindow`, the undo refresh | — | **no production caller yet**; their clock wiring belongs to 3.13 / 3.14 when those callers land |

**Recorded code inventory** (a fixture cannot assert this, so it is recorded with its command):

```
$ grep -rn "requestPlaybackViewMaintenance\|requestCommittedPlaybackWindow\|requestPlaybackWindow\|requestPreviewWindow" src \
    | grep -v "^src/Track/TrackPlaybackWindowBuild.cpp"
src/Track/TrackPlaybackRuntime.cpp:1615  bool LoopPlaybackRuntime::beginCoverageBuild( ... )   # definition, not a caller
src/Track/TrackPlaybackRuntime.cpp:1633  bool LoopPlaybackRuntime::beginCoverageBuild( ... )   # definition, not a caller
$ grep -rn "serviceRollingPlaybackWindow\|serviceAdmittedDeadlineMisses\|stepPlaybackViewMaintenance\|servicePendingSlotProvision" src/main.cpp
src/main.cpp:466  (void)trackManager.servicePendingSlotProvision();
src/main.cpp:487  (void)trackManager.getTrack(i).serviceRollingPlaybackWindow(windowClock);
src/main.cpp:489  trackManager.getTrack(i).serviceAdmittedDeadlineMisses(windowClock);
src/main.cpp:508  trackManager.getTrack(chosenTrack).stepPlaybackViewMaintenance(windowPhase, nullptr, false, windowClock, …);
```

So `requestPlaybackWindow` (the caller-owned-scratch entry the suite's fixtures use) and `requestPreviewWindow` have no
production caller, and every production call site above takes its clock from `ClockManager`.

## The fixtures

- `test_activation_audit_the_production_demand_is_positioned_and_an_unqualified_clock_is_refused` (**AA-1**) — on a
  playing demoted 24-bar target: an unqualified stamp returns `Excluded`, leaves the rolling frame `Idle` and retains **no**
  demand; the qualified stamp returns `Requested` and the retained demand and the frame agree on `clockEpoch == 1` and on
  an absolute `deadlineTick > origin`.
- `test_activation_audit_the_launch_demand_is_unpositioned_and_never_judged` (**AA-2**) — on the same shape's launch: the
  demand *and* its in-flight build carry `clockEpoch == 0` / `deadlineTick == 0`; eight deadline services at ticks far
  beyond any deadline name **no** `one_pass_late`, `lag_breach`, `promise_epoch_invalidated` or
  `pending_position_invalidated` row; and with `sInterferenceBudgetEnforced` on, the launch still prepares and binds (the
  flag is restored by the fixture).

### Mutations (each red-proved, restored byte-identically)

| ID | Mutation (`src/Track/TrackPlaybackWindowBuild.cpp`) | Failure it produced | Restore |
|---|---|---|---|
| **MA1** | drop `!clock.epoch \|\|` from `serviceRollingPlaybackWindow`'s guard | AA-1: `Expected 1 Was 3. an unqualified clock is refused before a demand is made` (`Excluded` → `Requested`) | `75c23a3f1849d15a` before and after |
| **MA2** | give the launch demand a position (`request.clockEpoch = 1; request.deadlineTick = 1000000`) | AA-2: `Expected 0 Was 1. the launch demand is unpositioned: it is not a promise by a tick` (+ five pre-existing launch fixtures) | same file, same hash |
| **MA3** | drop `work.clockEpoch == 0 \|\|` from `serviceAdmittedDeadlineMisses`'s guard | AA-2: `Expected 0 Was 1. nor epoch-invalidated` (a far-future clock mints an invalidation for an unpositioned promise) | same file, same hash |

`git diff --stat -- src include` is empty after the round: this slice changes no production line.

## Verification (final tree)

| Check | Command | Result |
|---|---|---|
| Focused | `pio test -e native -f test_runtime_working_set_ownership` | **137 / 137 passed** (was 135; +2 cases) |
| Full native | `pio test -e native` | **2791 cases / 2784 succeeded / 7 skipped / 0 failed** (was 2789 / 2782 / 7) |
| Capture build | `pio run -e teensy41-capture-serial` | **SUCCESS**, `variables 96,192` / `code 391,576 + padding 1,640` (floor 840, 800 B headroom), no re-base |
| Strict OpenSpec | `openspec validate windowed-playback-generation --strict` | valid |
| Whitespace | `git diff --check` | clean |
| Device | — | **NOT RUN** (nothing is enabled; the device leg is 6.5's) |

Artifact: the suite TU `f6a0fe3b45b0df66`.

## One observed anomaly, recorded rather than smoothed

While the two fixtures were being landed, **three consecutive runs failed** at a *pre-existing* helper's assertion —
`assert_s4b_7_a_demoted_target_through_the_turn`: `Expected 2 Was 0. the root-backed demoted switch must commit` — with the
audit fixtures registered last in `main()`. The same **byte-identical source** then passed:

- 20+ subsequent focused runs, including 13 without any rebuild and 5 with `MALLOC_PERTURB_` ∈ {1, 42, 85, 170, 254};
- a **clean rebuild** after deleting the whole native build cache;
- the **full native suite** above.

Recorded facts, not a conclusion: the failing runs each followed an incremental rebuild in a session that had, earlier,
compiled four deliberate production mutations and restored them; the failure was never in an assertion this slice added,
and no failing run could be reproduced afterwards. Two candidate explanations remain **open and unproven** — a stale
object in the incremental build (the source-restore path used a file copy) and an uninitialised read somewhere in the
launch/commit path whose contents my sweep's larger allocations changed — and neither was established. Nothing here
claims the helper is sound; what is claimed is exactly what was run: green focused/full/build results on this tree, with
the anomaly above reproducible only in that earlier build state.

## What stays open

- **Activation itself is still the owner's decision**, not this slice's: flipping `sInterferenceBudgetEnforced` in
  production (or tying it to the qualification build) plus the device leg (6.5) is what would make the enforced admission
  live. What this slice removes is the *"the callers do not pass stamps"* objection for the demand that exists.
- Task 3.10's service qualification, 6.3's remaining sweep axes and 3.7's fallback removal stay open; nothing here checks
  a task off.
