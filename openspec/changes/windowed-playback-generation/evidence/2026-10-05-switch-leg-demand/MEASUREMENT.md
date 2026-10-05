# The measurement, and what the switch-leg blocker actually is (classification)
> **SUPERSEDED (2026-10-05, later the same day).** This file's *measurement* stands
> (the target's length and the census root fields), but its **classification is
> retracted**: the switch target's root publication did in fact complete
> (`RRQ,7,4,…terminal,none,committed`), and the lane serves a demoted 24-bar slot in the
> same run — so "the missing whole-loop root is the cause, a prerequisite outside the
> lane" is wrong. The trace that replaced it, with citations, is
> [`../2026-10-05-launch-path-trace/EVIDENCE.md`](../2026-10-05-launch-path-trace/EVIDENCE.md):
> the first failed transition is a **silent exit inside
> `Track::preparePlaybackLaunchForSlot`**. Read the findings below as history.


The discriminating check the switch-leg slice named: **the switch target's runtime loop length**, read from the device's
own selection feedback, plus the root fields the census reports for the same slots. The result classifies the blocker — and
it is neither "closure work I have not done" nor a defect in this change.

## The measurement

Read-only probe (selection presses only; no clear, no record, no switch, no transport), after a fresh qualification flash:

```
select slot 4 (card: 2 bars, 3 passes)   -> Sent loop length CC feedback: length=0 -> CC=0 (1 bar default)
select slot 5 (card: 24 bars, 1 pass)    -> Sent loop length CC feedback: 24 bars (18432 ticks) -> CC=23
select slot 6 (card: 24 bars)            -> Sent loop length CC feedback: length=0 -> CC=0 (1 bar default)
```

and the library census for the same slots:

| slot | committed | length | passes | `has_root_content` | `root_is_current` |
|---|---|---|---|---|---|
| 4 | yes | 1 536 (2 bars) | 3 | **true** | **true** |
| 5 | yes | 18 432 (24 bars) | 1 | **false** | **false** |
| 6 | (cleared by the runs) | — | — | false | false |

So the two facts that decide the branch are now measured: the **2-bar** slot's runtime length is **0** (not resident), and
the **24-bar** slot's runtime length is **18 432** — which means run 06's switch *did* satisfy the routing condition
(`shouldAvoidFullVisualRebuild(18432)` holds, target ≠ active slot), so `servicePendingSlotProvision` **did** call the
lane's `preparePlaybackLaunchForSlot`. It was therefore not the branch condition, and not the leg, that stopped it.

## What stops it — the root, and it is required by *both* demand paths

The launch is not the only path that needs a current published root:

```
src/Track/TrackPlaybackWindowBuild.cpp:2374   !loop.publishedSnapshotIsCurrent()) return RollingOutcome::Excluded;
src/Track/TrackPlaybackWindowBuild.cpp:2861   return PlaybackViewRequestResult::SourceUnavailable;
src/Track/TrackPlaybackWindowBuild.cpp:959    "…a *current* published root (`requestPlaybackWindow` refuses
                                               `SourceUnavailable` / `SourceNotCurrent`)…"
```

and a **demoted** slot has no root at all — `has_root_content = false` for both 24-bar slots, `true` only for the 2-bar
one. The reason is the change's own policy: `shouldAvoidFullVisualRebuild` exists precisely so a long loop never builds a
full-loop representation, and the full-take publisher is the only thing that installs that root.

So the chain that fits **every row of all five runs** is: the lane is dispatched under transport (29–66 `win_gap` blocks
with `running=1` per run) → `serviceRollingPlaybackWindow` is called each of those turns on the **playing slot**, which is
itself a 24-bar (demoted) slot with no current root → it **refuses**, the frame never becomes eligible, and
`eligible = progress = 0`, no receipt, no publication follow. Run 06's switch reached the launch on the same input and hit
the same refusal. The switch hold then names the outcome at its bound — the device reporting a wait it cannot end.

**What is measured vs inferred, stated plainly:** the runtime lengths, the root fields and the code's requirement are
measured. That the refusals are the *cause* of `eligible=0` is an **inference** that fits every row and the exact input the
code refuses — I could not read the refusal itself, because the rolling outcome is a typed enum returned to its caller and
**no row names it** (recorded below as a gap).

## Classification — answered against the repo's own rules

**This is not closure work of 6.4/6.5, and it is not a defect introduced by this change.** It is the runtime change's
**already-recorded S4b.7 published-root question**, reproduced on hardware, plus a contract tension this change's own
claim makes visible:

* `runtime-working-set-ownership` recorded it as an open item: *"a demoted target (> 16 bars) still does not commit after
  provisioning (`ready=0 occ=1 pending=1`) because the long-loop hold needs a non-empty windowed gather, which is empty
  without a published root — an architect question or an S4c/F published-root fixture, recorded rather than claimed."*
  Its native suite works only because the fixture installs a *current* root through a test seam
  (`publishBarSpacedRootForSlot`); the device has no such seam.
* This change's **F-8** asserts the opposite direction as its own acceptance: *"a window is served without the whole root
  being resident"* — windows resident, **root demoted**. But every demand path this change has (`rolling` and `launch`)
  refuses a source whose **whole-loop** root is not current.

So the blocker is an **authority/contract question**, with exactly two coherent resolutions and neither of them a fix I may
make inside a verification slice:

1. **The restore edge installs the root as current** for a restored slot (the premise `S4C1` recorded: *"the restore edge
   installs the loaded body as that root marking the coverage outcome `kSnapshotCoverageCurrent`"*). Then a demoted slot
   would carry a current root and both demand paths would proceed. Owner: **RWO** (restore/provision). If the measured
   `has_root_content = false` for a *restored* slot contradicts that recorded premise, **that** is the defect — in RWO.
2. **The window demand paths stop requiring a whole-loop root** for a demoted interval — which is what F-8's claim implies.
   Owner: **this change**, but as a *design* decision under DEC-108 (it changes which source a window may be built from),
   not as a verification-slice edit.

Either way: **record, don't patch here.** Per the critical-path rule, discovery is not scope: this is not a
data-loss/safety risk, and it is not a lane defect (the lane's *service* correctness is proven natively). It blocks only
the *exercise* that 6.5's hardware evidence needs.

**And a finding of my own instrumentation (F-16):** the demand paths' refusals are **unobservable on device** — the
outcome is a typed enum returned to its caller (`RollingOutcome::Excluded`, `PlaybackViewRequestResult::SourceNotCurrent`,
`SourceUnavailable`) with no row naming it, so a device reader cannot tell "nothing was owed" from "the source was
refused". That is exactly the distinction five runs were spent on, and a row would have settled it in one.

## Consequence for the tasks

* **6.4 / 6.5 stay open**, and the reason is now a *prerequisite outside the lane*: **no window demand path can be
  satisfied on a demoted slot while its whole-loop root is not current.**
* Nothing is checked off, no firmware line is changed, and no oracle is adjusted to fit the finding.
* The next decision is the owner's: resolve (1) in RWO's restore path, or (2) as a DEC-108 design decision here. Until
  one of them is taken, the lane's hardware evidence for F-7/F-8 cannot be produced on any leg shape.
