# Routed chain cold start: design (owner request 2026-11-03, "Design cold start")

Status: design input, not implementation authority. No code changes.

Constraints from the owner's rulings:
- no legacy merged fallback;
- DEC-247, DEC-248 and amendment 23 unchanged;
- the acceptance oracle is the emitted MIDI for the full committed loop.

## 1. What a cold start is (from the Gate A reconciliation)

A routed manifest chain is silent only before its **first** window serves. With the measurement requester on, its
successors are continuous (host and device). There are three cold starts:

| trigger | measured silence |
|---|---|
| transport start of the routed slot | host: traversal 0 and bars 0-1 of traversal 1 |
| a revision move that ends the chain: the Overdub stop | device: 77.2 -> 97.9 s; host: traversal 3 and bars 0-1 of traversal 4 |
| a restart (transport stop, then start) | device: about 15 s |

## 2. Why the first window cannot cover the playhead today (code)

`Track::stepRollingFrame`, Idle frame (`src/Track/TrackPlaybackWindowBuild.cpp`):

1. **Shared-owner gate.** A pending or frozen view request, an unavailable coverage role, or outstanding grains
   gives `win_busy`: "existing promises and shared owners take precedence".
   - After the Overdub stop, the deferred stop job's frozen presentation holds the roles (device `rq=9`), for about
     7 s.
2. **Position.** The first window starts at the first two-bar boundary at or after `phase + lookahead`.
   - `lookahead = grains x (H + 1) x advance + lead + slack`, with `H = 7`, `advance = 64`, `lead = 768` and
     `slack = 128`. The grains are priced from the densest span of the manifest (Task 3.7h / DEC-147: the position
     pays for the work it asks for).
   - At the sweep's measured `max_grains = 13`, that is about 7,550 ticks before alignment: about 10 bars, about 8 s
     at the sweep tempo.
   - Measured reality is far smaller: successors were ready at most about 550 ticks after their request
     (`max_ready`). The price is a declared worst case, by design (DEC-147: "a declared permitted value, not a measured
     WCET").
3. **No fallback** (amendment 23). `[playhead, first boundary)` has no source, so it is silent.

On the shipping lane the same interval is covered by the merged fallback, which is why the default image shows no gap.

## 3. The existing predecessor mechanism, and where it fails

The architecture already answers "what serves until the first window" for the Record stop: **S4f.19's frozen return
window**.
- The stop job's own frozen source is served from a bar-1 return window and an onward window
  (`adoptFirstRecordingBarReturnWindow`, `adoptOrServiceOnwardRecordWindow`). The take is audible before its root
  publishes; device: `none=10` ticks at the Record stop.
- `LoopPlaybackRuntime::hasBoundRecordReturnWindowForPendingJob` checks only that a stop job owns the interval, not
  the stop's mode.
- On the Overdub stop the device attempts exactly this window for track 7, in `win_frozen_readiness`, 77-85 s:
  `route` -> `adopt_wait` -> `build_drop,qualification_started` (whole loop) -> `build_drop,handoff_unreachable` ->
  `released_without_site` -> `qualification_started` again -> `build_admitted`.
- The frozen return window for the Overdub interval never became servable, so the stop job held the roles (point 1)
  with nothing serving.

That is the gap to close. The mechanism exists; for an Overdub stop on the routed slot, the predecessor fails.

## 4. Proposed rule

> **A routed chain never cold-starts at an audible boundary.** Every transition that ends or begins a chain binds a
> predecessor window over the immutable source that is current at that moment. The predecessor serves the playhead
> until the first manifest window's priced boundary, and hands off to it there. The manifest window keeps its DEC-147
> price. The predecessor covers the price; it never cuts it.

| transition | predecessor (existing owner) | today |
|---|---|---|
| Record stop | the stop job's frozen return window (S4f.19) | works |
| **Overdub stop** | the same frozen return window, over the Overdub stop job's frozen source (committed passes plus this interval's sealed capture, DEC-159 / C3) | attempted, dropped `handoff_unreachable` |
| transport start / restart of a routed slot | the slot's launch window (Task 3.7e `seedRollingFrameFromBoundLaunch`: a phase-0 window bound before the playhead starts) | the routed slot binds no launch window: traversal 0 silent |

Both predecessors exist. The work is to make them hold for the routed slot. It adds no new lane, no new handoff
mechanism and no fallback.

## 5. What must be measured before choosing the fix (next step, read-only)

1. **Why the overdub-stop frozen return window drops.** Two possibilities:
   - its own lead price fails (`handoff_unreachable`: the same 10-bar price applied to a window that must start at
     the playhead);
   - its source qualification (whole loop) is the cost.
   - This needs the device trace of `win_frozen_readiness` with start, length, lead and grains for that interval, and
     a host reproduction in `test_revision_manifest_lane` (the Gate A case already stops an overdub on the routed
     slot).
2. **Whether the Record-stop return window is priced the same way.** It serves bar 1 *before* its build completes,
   from the bound bar-1 window. If so, the Overdub stop needs the same bound-before-stop shape: a window bound at the
   stop edge, not requested after it.
3. **The launch path for the routed slot:** why `seedRollingFrameFromBoundLaunch` finds no bound launch. It is
   expected to be bound only for a committed switch target, not for transport start of the active slot.

## 6. RED specification (host, `test_revision_manifest_lane`, emitted MIDI as oracle)

| case | scenario | oracle |
|---|---|---|
| 1. transport start | four-bar routed take, transport starts at phase 0 | traversal 0 plays every take note. RED today: traversal 0 silent |
| 2. Overdub stop | the existing Gate A case | traversal 3 plays every take note plus the overdub's committed note. RED today: silent |
| 3. restart | stop, then start at a non-zero phase | the first traversal after restart plays every note from the restart phase on |
| 4. steady state | with the measurement requester's knobs | stays continuous (GUARD, green today) |
| 5. identity | across each handoff | the predecessor's last served tick and the first manifest window's first tick are adjacent; no tick is served twice; every served window names its identity (manifest or frozen source), never a root |

Not in scope: changing the DEC-147 price or amendment 23, a merged fallback, or the publication cut.

## 7. Recommendation

Do the measurements in section 5 first (one device trace plus a host reproduction). The likely fix is narrow: bind
the overdub-stop return window at the stop edge, as the Record stop already does, and give the routed slot a launch
window at transport start. Confidence that the fix belongs to these two existing predecessors: high, since both
mechanisms exist and the device shows the overdub one being attempted. Confidence in the exact failure inside the
overdub return window: low until section 5.1 is measured.
