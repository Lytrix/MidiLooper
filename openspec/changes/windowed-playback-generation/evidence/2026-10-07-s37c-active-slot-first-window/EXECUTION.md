# Task 3.7c — the active slot's first window (G2 execution packet and completion record)

## Identity and scope

- **Task:** `windowed-playback-generation` **3.7c**, added by this slice and declared in `CURRENT_WORK.md`
  § Now implementing at the owner's direction (2026-10-07, *"do a"* — the active slot's first window as the next
  implementation slice, with a device run as its first fixture).
- **Owner/path:** the existing window authority — `Track::serviceRollingPlaybackWindow` (the demand's entry) and the
  off-clock owner `Track::stepPlaybackViewMaintenance`; the requester stays `Track::requestCommittedPlaybackWindow`
  and publication stays the publisher's. **No new owner, scheduler, queue, authority, state or constant.**
- **Baseline:** `32555274` (the closeout of the 2026-10-07 demand device run).
- **Contract:** the served window stops being a *precondition of requesting* a window; the request is the same
  positioned interval the roll requests, judged by the same admission, built by the same owner, published as the
  `ready` successor and adopted by the resolver at its own boundary. Refusals stay named.
- **Explicit non-goals:** no launch change, no fallback removal (3.7b), no capacity/grain/lead/hold change, no timing
  or envelope claim, no `ExecutionBounds.md` row change, no new row family.

## The gap this closes (measured, not inferred)

`captures/s64-demand-01` (2026-10-07, authorized device run, qualification image): every demoted, span-aligned,
**playing** slot left the lane as `win_not_servable` with `sv=0` — including where its root **was** current (`rt=1`)
and its coverage role free (`rl=1`):

```
t=7 s=3  len=19968 av=1 al=1 pl=1 -> win_not_servable sv=0 rt=0
t=7 s=3  len=18432 av=1 al=1 pl=1 -> win_not_servable sv=0 rt=1 rl=1
t=0 s=0  len=15360 av=1 al=1 pl=1 -> win_not_servable sv=0 rt=1
```

The entry required a window that was already *served*, and only a **switch's launch** could produce one
(`preparePlaybackLaunchForSlot` refuses the active slot by construction) — so a slot that reached playback without a
switch (a freshly recorded body, or a restored one) could never be served from a window at all. The spec's first
requirement states the opposite: *the demoted case is served by interval publication*, and playback SHALL NOT require
whole-loop publication (`spec.md`, § Active playback is served from an immutable window).

## The change

In `Track::serviceRollingPlaybackWindow`, the two clauses that required the current view to be complete and published
are removed from the entry guard. What still refuses, by the same name, is having no view set to publish into, or no
source to build from (`playbackWindowSourceIsCurrentForSlot`: a current root, or the retained revision the loop
carries). The comment in place carries the device evidence and the reason.

## Transition oracle (pre-state → post-state → the mutation that must fail)

| Field | Statement |
|---|---|
| **Pre-state** | A slot that is demoted (past the demotion bound), span-aligned, playing, with a **current root** and prewarmed views, and **no published/served window** (`sv=0`, the device's own shape). |
| **Post-state** | The lane **opens the positioned demand** for the two-bar interval the consumer is about to use: `RollingOutcome::Requested`, the `win_requested` row, a retained request carrying the caller's epoch and the interval; the off-clock owner then builds it and the completed build publishes as the **ready successor**; preparation is *not* serving (the resolver adopts it at its own boundary). |
| **Named refusals kept** | No view set to publish into, or no current source → `win_not_servable`; an existing promise/owner/grain → `win_busy`; a tick outside the projection → `win_reprojected_tick`; the declared lead cannot place a boundary → `win_boundary_rejected`. |
| **Mutation that must fail** | Restoring the removed clauses (the pre-3.7c guard) → the fixture fails at the request assertion (`Expected 3 Was 1`: `Requested` vs `Excluded`), i.e. the device's own refusal. |

## Controls and coverage

- **Production path only:** the fixture drives `Track::serviceRollingPlaybackWindow` and
  `Track::stepPlaybackViewMaintenance` on a slot built through production entries (`commitBarSpacedPassIntoTrackSlot`,
  `publishBarSpacedRootForSlot`, `Track::prewarmPlaybackForSlot`); no stub, no reset, no synthesized publication.
- **Preserved invariants:** identity `(slot, playbackGeneration, playbackRevision, interval)`, the admission's lead
  judgement, the one-grain-per-turn owner contract, the successor lane's publication, and the merged fallback's role
  for the traversals in between.
- **Cost:** the fixture drives the owner **directly** for its build half (bounded turns, no clock advance, no transport
  update) and does **not** walk the ~29-bar lead tick by tick; the serving boundary is the resolver's own step and is
  covered by the serving fixtures.

## Verification recipe

- Focused: `pio test -e native -f test_runtime_working_set_ownership`.
- Broad: `pio test -e native`; skipped cases reported separately.
- Builds: `pio run -e teensy41-capture-serial` and `pio run -e teensy41-capture-serial-window-qualification`, each
  reported against the approved RAM1 floor.
- Hygiene: `openspec validate windowed-playback-generation --strict`, `git diff --check`.
- Host tooling: `python3 -m unittest test_windowed_content_parity_verify` (the verifier's `win_not_servable`
  explanation is corrected with this change, since the exit's meaning moved).
- **Device:** a repeat of the 2026-10-07 windowed leg on the same authorized target, read through the same registered
  profile — the slice's own first fixture.

## Completion record

- Focused: **177/177** (`test_runtime_working_set_ownership`; was 176) — the new fixture registered and passing.
- Red-proof: restoring the pre-3.7c guard fails **only** the new fixture (`Expected 3 Was 1. the active slot's first
  window is requested rather than refused`), the other 176 passing; the file was restored **byte-identically**
- Full native: **2,856 cases / 7 skipped / 2,849 succeeded / 0 failed** (the pre-slice tree was 2,855 / 7 / 2,848 / 0:
  the +1 is this fixture). No suite ERRORED or FAILED.
- Builds: `teensy41-capture-serial` **SUCCESS** — `variables 96,192 / code 392,248 + padding 968` (128 B above the
  840 B floor); `teensy41-capture-serial-window-qualification` **SUCCESS** — `variables 96,192 / code 392,312 +
  padding 904` (64 B above the floor). **Both lines are byte-identical to the pre-slice figures**: the relaxation costs
  no ITCM and no RAM1, and no budget was re-based.
- Hygiene: `openspec validate windowed-playback-generation --strict` valid; `git diff --check` clean.
- Device: recorded in the run's own evidence — a repeat of the 2026-10-07 windowed leg on the same authorized target,
  read through the same registered profile.
