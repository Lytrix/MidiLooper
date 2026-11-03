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

## 8. Measurements (owner: "Take measurements", 2026-11-03)

### 8.1 Device: the Overdub-stop window (`captures/session_20261103_211405.log`, track 7)

`win_frozen_readiness` fields: readiness, holder, revision, phase-or-lead, generation, token, start, length, loop,
adopt bits. At about 961 ticks/s:

| time | row | reading |
|---|---|---|
| 76.3 s | `route,turn`, rev 2 | the pre-stop chain |
| 77.7 -> 84.3 s | `busy,roles`, refusal 9 | the stop job's frozen presentation holds the coverage roles for 6.5 s; nothing is requested |
| 84.3 s | `route,turn`, rev 3, phase 10,752 | the lane acts on the new revision |
| 84.3 -> 85.4 s | `qualification_started` -> `handoff_unreachable` -> `released_without_site` -> `qualification_started` -> `build_admitted` -> `completed_unpublished` | a window built in about 1.1 s |
| 97.9 s | `route,turn`, phase 23,856 | that window's start: serving begins |

The window was built about 13,100 ticks ahead of the playhead (its priced lookahead) and waited for it. The 12.5 s
"ready, not serving" span is the price, not a slow build.

### 8.2 Host: the same transition (`test_revision_manifest_lane` Gate A case, owner state every 128 ticks)

| tick (phase) | owner state |
|---|---|
| 9,176 | `stopOverdubbing()` |
| 9,216 (0) | stop job pending; return window **bound and servable** (`hasBoundRecordReturnWindowForPendingJob`, `hasServableDeferredRecordView`) |
| 9,344 (128) | stop job **retired**; no return window; rolling frame positioned at `startPhase = 1536`, deadline tick 13,824 (the next traversal) |
| 9,344 -> 13,824 | nothing serves: 4,480 ticks |
| 13,952 | serving; continuous afterwards |

- 4,480 is exactly the declared DEC-147 lookahead: 7 grains x 8 x 64 + 768 + 128.
- Transport start shows the same: from tick 0, the first window is at 4,608, which is bar 2 of traversal 1. The routed
  slot binds no launch window.

### 8.3 What the measurements decide

1. **The overdub-stop predecessor does exist and does serve, but only while the stop job is pending.** On host the
   job retires within 128 ticks, the return window goes with it, and the cold frame is then positioned a full priced
   lookahead ahead. On device the job held the roles for 6.5 s without the window becoming servable for the new
   revision.
   - The defect is **predecessor lifetime**, not an absent mechanism. Its coverage ends with its owner (the job),
     not at the first manifest handoff.
2. **The silence equals the cold frame's DEC-147 lookahead in every case.** The first window is ready in about a
   tenth of that (device: built in 1.1 s; successors ready at most about 550 ticks after their request). The price
   is a declared worst case, and the gap is the price.
3. **Transport start has no predecessor at all.** The launch-window path (`seedRollingFrameFromBoundLaunch`) finds no
   bound launch for the routed active slot.

### 8.4 Refined rule

The section 4 rule is unchanged ("a routed chain never cold-starts at an audible boundary"), and the measurements
fix its shape:

- **Predecessor lifetime.** A predecessor is retired by its **handoff**, not by its owner. A stop job's return window
  stays bound until the routed frame's first manifest window serves, even after the job has retired. Its source is
  immutable: the committed passes the stop just finalized. Until then the successor demand is the routed frame's
  first window, priced as today.
  - Open point: the return window covers bar 1 plus onward windows built from the job's range. After the job, the
    onward windows need an owner. The candidate is the frame itself serving the frozen source's windows as its first
    links, which is the S4f.19 onward path with the job's range replaced by the committed source.
- **Transport start.** The routed active slot binds a launch window while STOPPED, where no deadline is in force, so
  the price is trivially met. The Task 3.7e seed then adopts it at the first tick.
- **Unchanged:** the DEC-147 price, amendment 23 (no fallback), DEC-247, DEC-248 and publication.

**Confidence.** High on the cause: the lifetime and the price are measured on host and device. Medium on the exact
owner for post-job onward windows. That is the one design choice for the owner.

## 9. Owner ruling: the rolling frame owns the onward windows (2026-11-03), and what it can adopt

REDs (`test_revision_manifest_lane`, with `ROUTED_AUDIBILITY_RED=1`; both run with the probe image's successor knobs,
so only cold starts can be silent):
- `test_c5_3_cold_start_transport_start_plays_the_first_traversal`: traversal 0 silent.
- `test_c5_3_gate_a_take_stays_audible_through_an_overdub_stop`: traversal 3 silent.

What exists to adopt at the Overdub stop (host, the slot's `lifecycle.current` every 16 ticks from the stop):
- Before and after the stop, the current view is a **committed window at the pre-stop revision** (rev 1; the loop
  moves to rev 2 at the stop), covering bars 2-3 (`1536+1536`), carrying a root identity.
- It does **not** cover the playhead (phase 0), and it is stale for the new revision.
- During the overdub the routed slot was served by the **merged overdub route**, not by a manifest chain. No warm
  committed window covers the playhead at the stop.
- The return view that was servable at the stop belongs to the stop job's presentation and retires with the job one
  grain later.

Consequences:
- **Transport start: implementable within the rulings.** While STOPPED no deadline applies, so a launch window bound
  for the routed active slot meets its price trivially, and the Task 3.7e seed adopts it at the first tick. This is
  the same mechanism a committed switch target uses.
- **Overdub stop: frame ownership alone cannot close it.** There is no built window to adopt. Building one at the stop
  has zero lead under DEC-147, and continuing the merged overdub route past the stop would be the excluded merged
  fallback. Closing it needs one of these (owner decision):
  - (a) **A warm chain through the overdub:** the routed frame keeps serving committed (manifest) windows during the
    overdub, so the stop is a revision move inside a running chain and its successor is requested at the new
    revision. The largest option: overdub serving on the manifest lane.
  - (b) **Prepare at the stop edge from the session's own sealed source:** the overdub stop job's presentation binds
    a return window over committed passes plus the sealed capture before the stop's revision moves, as S4f.19 does for
    a Record stop's bar 1. The frame then adopts it and owns its onward windows. This keeps the session's immutable
    source as the predecessor, which is the ruling's intent.
  - (c) Amend DEC-147 for a chain's first window. Excluded by the ruling.

## 10. Option (b) feasibility (owner: verdict before production changes)

### 10.1 The "job-owned servable return window" is not one

Host, every 8 ticks from the overdub stop (`test_revision_manifest_lane` Gate A case), all view roles:

| tick | job | `hasServableDeferredRecordView` | first-return index | current | ready |
|---|---|---|---|---|---|
| 9,176-9,224 | pending | **1** | 255 (none) | rev 1, `1536+1536`, complete, published | none |
| 9,232 | retired | 0 | 255 | rev 1, `1536+1536` | **rev 2, `1536+1536`, complete** |

- `hasServableDeferredRecordView` is true when a first-Record return window exists **or** the current/ready view is
  merely complete. It checks neither revision nor playhead coverage.
- At the overdub stop it counts the stale pre-stop window. The first-return window is a **Record-only** path
  (`firstRecordReturnWindowIndex` stays 255).
- **There is no job-owned return window over the session's source to transfer.** The apparent contradiction is this
  predicate, not a hidden window.

### 10.2 Transfer without a synchronous rebuild

- Nothing exists to transfer at the stop edge.
- The post-stop window for the new revision, however, is **built within 56 ticks** of the stop by the existing
  bounded workers: tick 9,232, `ready`, rev 2, bars 2-3. The work is not the problem.
- That window is held for the *next* traversal's bar 2 (deadline 13,824), because at request time the DEC-147 price
  (4,480 ticks) would not admit this traversal's bar 2, then 1,520 ticks away.

### 10.3 Onward continuity under production DEC-147 pricing

- Under production pricing no successor at N's end is admitted (`test_rolling_successor_lead`, both lanes). The host
  Gate A case without the probe knobs plays bars 2-3 of every other traversal in steady state.
- A two-bar return window (1,536 ticks) cannot bridge the cold-start horizon: lookahead 4,480 ticks on host, about
  13,100 on device. Under production pricing its successor is refused anyway.
- So **no return window, however bound, gives continuity under production pricing**: the chain itself does not
  continue there.

### 10.4 Earlier preparation through existing bounded workers

- The workers already prepare the right content in time (10.2). What holds it back is the price's **position**
  judgement: it is made at request time, from a declared worst case, and never revisited when the build completes
  early.
- Preparing the post-stop chain before the stop would mean serving committed-plus-live content on the manifest lane
  during the overdub. That is option (a), the excluded manifest overdub reader.

### 10.5 Verdict

**Option (b) is not feasible as specified. Within the current constraints no narrow variant of it is either.**
1. The return source it would transfer does not exist for an overdub stop (10.1).
2. Under production DEC-147 pricing the routed chain does not continue past any window, so even a perfect return
   window ends in a cold frame (10.3).
3. The binding constraint is the price's request-time position judgement, not the work, the source or the ownership.
   The correct window is complete 56 ticks after the stop and then waits 4,480 ticks (10.2).

What would close it without a fallback, a new reader or relaxed work pricing is for the owner to decide:
- **The C5.3b successor price (already pending).** Production needs an admissible successor regardless of cold
  start; without one, steady state is not continuous either.
- **Judging the position at completion as well as at request:** a ready window may take the earliest boundary its
  *completed* build precedes, by at least the one-bar `leadTicks` the contract already requires. The work price is
  unchanged. Only a position the price ruled out at request time may be reclaimed once the work is done.
  - This touches DEC-147's position rule, so it is an owner amendment, not a mechanical fix.

REDs stay separate and unchanged: `test_c5_3_cold_start_transport_start_plays_the_first_traversal` (stopped-state
launch) and `test_c5_3_gate_a_take_stays_audible_through_an_overdub_stop` (post-overdub-stop return). The acceptance
for either fix is complete emitted-MIDI parity across several traversals, including the first successor handoff,
under production pricing.

## 11. Revised C5.3b ruling: the pricing reconciliation and the interaction map (no production change)

Option (b) is withdrawn by the owner.

### 11.1 Why `G(E)` and the grant budget do not admit production successors

| improvement | where it lives | what it governs |
|---|---|---|
| `G(E) = ceil((2E+1)/64) + 3*ceil(E/48) + 2` | `CoverageServiceContract::deriveManifestWindowGrantsFrom` | the price's **input** (grains) |
| amendment 31 grant budget: due grants spaced over the actual remaining lead, never withheld | the lane's grant scheduling | **delivery** of an admitted build |
| DEC-147 admission: `canCompleteWithinLead(lead, outstanding)` | `requestPlaybackViewMaintenance` admission | **whether** a demand is admitted and **where** a cold frame is placed |

- The admission inequality still charges every grain `(H + 1) x advance` = 8 x 64 = 512 ticks: seven denied lane
  turns, each the full permitted advance. It adds the one-bar `leadTicks` and the 128-tick slack.
- Amendment 31 records itself as "not yet the DEC-147 admission rule". The guarantee it proved on device (a due
  grant cannot be withheld; 21/21 at a 1,536 lead with 7 denials) is therefore not what admission charges.
- **Host 4,480 vs device about 13,100 ticks.** Both use the same formula with different `G`. The host's sparse take
  sits at the 7-grain declared floor: 7 x 512 + 896 = 4,480. The device's dense take prices its densest span at about
  24 grains: about 24 x 512 + 896, roughly 13,180.
- **Production has no successor requester.** Only `RollingSuccessorProbe` opens N+1's demand while N serves.
  Production opens the next span when N expires, which is a cold frame. That is C5.3b's open "successor preparation"
  task.
- **Net:** under production pricing a 1,536-tick successor (7 grains needs 4,480) is always refused, and none is
  requested anyway.
  - With amendment 31's delivery bound as the admission model, the feasibility condition is about
    `G x lane-opportunity gap + guard`: 7 x 46 + 64 is about 386 ticks on the measured device gap.
  - That is the gap between the two models. Admitting on it is an owner amendment of DEC-147.

### 11.2 Pricing, completion, musical phase and serving ownership

- **Window content is traversal-free.** A `RuntimePlaybackView` carries its source identity (revision, generation,
  manifest or snapshot identity), its loop-local `coverage` (start phase, length, loop length) and a physical
  `storageGeneration`.
  - The traversal binding lives only in the rolling frame (`deadlineTick`, `epoch`, `projectionStart`) and in the
    request (`admittedTick`, `deadlineTick`, `clockEpoch`).
  - A completed window may therefore serve an earlier occurrence of the same phase interval only while its revision,
    generation and epoch are still current. That is the periodicity condition the owner named.
- **The frame owns the placement.** A Ready frame is promoted at its `deadlineTick`. Placement-at-completion moves
  `deadlineTick` to an earlier occurrence; it never touches the view. Conditions:
  1. revision, generation, epoch and manifest identity are unchanged;
  2. the earlier occurrence's start is still ahead of the playhead (never part-way into an interval);
  3. completion precedes that start by at least the contract's `leadTicks` (one bar);
  4. the earlier interval is the same phase interval; non-wrapping two-bar spans make this exact;
  5. no other promise owns that interval;
  6. the publication and generation barriers the resolver already checks still hold.
- **Not covered by placement:** the interval from a stop to the first qualified boundary (bars 0-1 after the overdub
  stop), and transport start. Those need a source, not a reposition.
  - The resolver already prepares "an unpositioned cold initial view … while stopped" for a cold-shaped frame. The
    stopped-launch RED is why the routed slot does not use it.

### 11.3 REDs (host, `test_revision_manifest_lane`, emitted MIDI as oracle, each behind its own flag)

| case | flag | today |
|---|---|---|
| steady state under production pricing (traversals 2-5) | `STEADY_STATE_RED` | RED: traversal 2 incomplete |
| completion-aware placement: traversal 3's bars 2-3 after a stop at phase 0; no NoteOn twice | `COMPLETION_PLACEMENT_RED` | RED |
| guard: a consumed boundary is never claimed (stop at phase 1,600) | default gate | green |
| guard: the handoff margin is required (stop 600 ticks before the boundary) | default gate | green |
| transport-start launch (traversal 0) | `ROUTED_AUDIBILITY_RED` | RED |
| post-overdub-stop coverage (traversal 3, all bars) | `ROUTED_AUDIBILITY_RED` | RED; placement of bars 2-3 does not satisfy it |

- The two guards are trivially green today, because nothing serves a routed gap. They bind the fix.
- The wrong-revision case is pinned by the existing `test_c5_3_a_source_move_while_serving_cannot_publish_a_stale_window`.
  The wrong-traversal and incomplete-coverage cases are conditions 1, 2 and 4 above.
