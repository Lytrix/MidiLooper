# Execution — the launch-path trace (why the switch target gets no lane work)

Slice: **6.4/6.5 exercise, blocker re-trace.** Follows the switch-leg runs
(01/04/05/06) and *corrects* the classification committed in `4d3c8957` / `fa0726cc`.

## Why this slice exists

The previous slice classified the blocker as "the demoted slot's missing whole-loop
root -- a prerequisite outside the lane". That classification was reviewed and
rejected on two concrete grounds, both reproduced here from the run's own rows:

1. The launch path already contains an **on-demand root producer**
   (`Track::preparePlaybackLaunchForSlot` → `Loop::requestCommittedPassesRepublish`,
   `src/Track/TrackPlaybackWindowBuild.cpp:2151-2152`), covered by the passing
   regression `test_rootless_restored_launch_requests_its_source_once_and_becomes_ready`
   (`test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp:8865`).
   An absent root therefore does not prove that no producer exists.
2. This run shows the lane **serving a demoted (24-bar) slot** on hardware
   (finding 5 below), so "no driver shape can give the lane work" is false.

## Method (bounded, read-only)

Existing rows only: the drain/leg logs of `captures/s64-switch-06`
(plus 01/04/05 for the `#MEM_DENIED` and `RRQ` cross-checks) and
`captures/s64-window-census-01/census.json` for slot identity. No firmware edit, no
new instrumentation, no device action, no rebuild. Every claim below carries either a
row or a file:line.

## Result

The trace resolves the switch to the **launch path**, not the root and not the lane:

- target identity, length, and the branch it selects — findings 1-2;
- `provisionSlotForSwitch` did **not** refuse — finding 3;
- the target's root publication **did** commit, and the target then produced no row of
  any family — finding 4;
- the lane **does** serve 2-bar windows for a 24-bar slot in this very run — finding 5;
- therefore the failing transition is one of the **silent exits inside**
  `Track::preparePlaybackLaunchForSlot` — finding 7.

What the existing observations **cannot** distinguish: *which* of those exits fired
(every one of them returns without emitting a row; the function's only row is the
legacy-dense one at `:2163`). That is the single place a narrow diagnostic would be
justified — see EVIDENCE.md "Next discriminating step", which prefers a no-firmware
driver variation first.

## Retractions recorded here

- R1 — "the demoted target's missing whole-loop root is the cause; a prerequisite
  outside the lane": **retracted** (finding 4 + finding 5).
- R2 — "no leg shape the driver can drive will ever give the lane work": **refuted**
  (finding 5).
- R3 — label: earlier notes called the 24-bar slot "slot 5" in operator display
  numbering; the lane's own rows index it **4**. Same slot; the rows' 0-based index is
  canonical in this change's evidence from here on.

## Bounds

No production line changed (`git diff --stat -- src include test` empty); no gate,
capacity, span or RAM1 baseline touched; no device run; the change's 6.1/6.2/6.6/6.7
checkoffs are untouched, and 6.4/6.5 stay open with a narrowed, measured reason.
