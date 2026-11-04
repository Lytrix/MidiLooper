# Audibility before publication, and publication liveness (owner ruling 2026-11-03)

Evidence first; no fix. Four device runs of the same `lane_cadence` sweep on track 8 / slot 4.

| run | image | session log |
|---|---|---|
| A1 | probe `f8a30f07` (manifest reader routed to track 8) | `captures/session_20261103_211405.log` |
| A2 | default `310580ef` (shipping rolling lane, unrouted) | `captures/session_20261103_213204.log` |
| B1-B3 | earlier probe runs | `session_20261103_171618`, `_190327`, `_193923` |

Witnesses:
- `DIAG,tick_route`: per 5 s, which source served each playback tick, over all 8 tracks; about 600 ticks per track.
- `DIAG,track_send`: messages sent.
- `DIAG,win_gate` / `route_reject` / `win_frozen_readiness`: the lane's window state for track 7, the 0-based HITL
  track.
- `DIAG,snap_gate`: the Snapshot owner's eligibility and construction units.
- `DIAG,alloc_census`: the internal arena tail.

## Gate A: audible playback before publication

**The root never published in any of these runs**: `root_rev=0` at run end in all five. Playback was nevertheless
served for almost the whole run, so audibility is not gated on publication in this sweep.

| window | A1 probe (routed) | A2 default (unrouted) |
|---|---|---|
| Record stop | `none=10` for one 5 s window | `none=0` |
| overdub | track 8 served from `merged` (merged 4,800) | same |
| Overdub stop → +20 s | **`none` 357 / 597 / 600 / 601 / 381; sends 169 → 26 → 0 → 0 → 166** | `none=0`; view back at 560 |
| after the sweep's restart | `none` 250 / 601 / 600 / 44 (~15 s) | `none=0` |

The A1 gap after the Overdub stop belongs to the manifest lane, track 7, and has two parts:
1. **77.2 → 84.3 s:** `win_not_servable`, then `win_busy`, about 7 s.
   - The lane rebuilds source qualification after the stop (`build_drop`: `qualification_started`, whole-loop,
     `handoff_unreachable`, `released_without_site`).
2. **84.7 → 97.9 s:** `win_requested`, then `win_ready` at 85.4 s, but `win_serving` only at 97.9 s; ready but
   not serving for 12.5 s.
   - Under the C5.3b rule, a missed successor is a named miss with no merged fallback, so the routed track is silent
     the whole time. No `merged_build` row exists for track 7 in the gap, while track 3 keeps building merged windows.

**Reading.** In this sweep the measured audible defect is specific to the probe's manifest-routed track, after a
stop or a restart. Shipping playback shows no gap. The owner's "inaudible until publication about 100 s later" is not
reproduced here as publication-gated: the routed track resumes at 97.9 s with no root published. Still to establish:
- whether the owner's observation was on the probe image;
- the gesture sequence, if it was not this sweep.

## Gate B: finite publication does not converge

The publication owner is **stalled, not slow**.
- On track 7, construction units freeze mid-fragment at the overdub stop, with `elig=1` on every turn.
- The freeze point differs per run: `cu=196` (A1), 146 / 70 / 14 (B1-B3).
- No fragment completes for the rest of the run (more than 100 s). The default image's snapshot units also stop at
  about 80 s.

**Cause, measured on both images.** The internal arena tail steps down during the overdub and never recovers:

| image | tail before | during the overdub | after |
|---|---|---|---|
| A1 probe | 98,304 B | 65,536 B | 36,864 B |
| A2 default | 110,592 B | 77,824 B | 32,768 B |

- `admitSnapshotPublicationTurn` → `MemoryMonitor::admitOperationMemory(SnapshotReserve, 8 KiB)` requires
  `tail >= 32 KiB floor + 8 KiB`.
- Its own commentary records that the tail "never recovers because `_sbrk` is a pure bump" and is "a false denial"
  (tail 36 KB while the largest free chunk was 79.4 KB). That is why user gestures moved to a read-only free-list
  walk.
- `SnapshotReserve` was deliberately left on the tail, because it gates every turn. Every publication turn is
  therefore refused once the tail is below 40 KiB.

**Host RED:** `test_publication_red_converges_when_the_tail_is_low_but_memory_is_free`
(`test/test_continuous_capture/wrap_publication_reds.inc`, run with `PUBLICATION_LIVENESS_RED=1`).
- It uses the measured tail of 36,864 B and the measured largest free block, 80 KiB.
- Wrap, then the overdub stop, then the production stop job and Snapshot owner. Publication never converges: FAILS.
- Control: the same fixture with only the tail raised to 80 KiB converges (PASS). The RED therefore isolates the
  tail-only admission.
- Not decided here (owner): how `SnapshotReserve` should judge memory without the per-turn probe storm the earlier
  withdrawal measured (`2776cae3`). Options include the read-only walk at a bounded cadence or on denial edges, or a
  cached largest-free value.
- Also open: what consumes the tail during the overdub (`cap` 1 → 3, `undo` 1 → 2, `ib`/`ub` 0 → 8).

## Gate C: display bars 20-32

Not yet traced. Track 7's display rows in the gap show `disp_turn ... st=ret, why=hold_gap` over a published window
that the playhead is entering. That is the lead for the viewport-demand trace.

## Gate A, follow-up (owner: the observation was on the probe image's track 8)

**Host RED:** `test_c5_3_gate_a_take_stays_audible_through_an_overdub_stop` in `test_revision_manifest_lane`,
registered with `ROUTED_AUDIBILITY_RED=1`. A four-bar take on the routed slot, played on the production owners with
the reader owner serviced every tick: two traversals, then an overdub (one note), its deferred stop, and four more.

| traversal | manifest-served ticks | take notes played |
|---|---|---|
| 0 | 0 | none |
| 1 | 1,536 / 3,072 | bars 2-3 |
| 2 (overdub, merged route) | 0 | bars 0-3 + overdub note |
| 3 (after the stop) | 0 | **none** |
| 4 | 1,536 | bars 2-3 |
| 5 | 0 | **none** |
| 6 | 1,536 | bars 2-3 |

**Reading:**
- The routed manifest lane is audible only inside a window that is already serving. The next window is not prepared
  ahead, and a gap is silent. This happens in steady state, not only after a stop.
- That is amendment 23 as written: lane-owned gaps are named misses, never merged. The existing case
  `test_c5_3_current_boot_content_is_served_from_the_manifest_only` asserts "outside them the routed lane is silent
  by contract".
- `tasks.md` keeps C5.3b successor preparation open, pending the owner's successor price.
- The overdub stop adds a longer gap on top: the revision move makes the serving window unservable. On device, the
  lane's whole-loop re-qualification followed (about 7 s busy, then a window ready 12.5 s before it served).
- The defect is therefore not publication-gated. The owner's contract ("a newly recorded loop plays immediately")
  conflicts with amendment 23's interim silence on the routed track. Turning the RED green needs either C5.3b
  successor preparation (plus a post-stop serving rule), or an amended amendment 23 that gives the routed track an
  interim audible fallback. Owner decision.

## Gate A reconciliation (owner ruling: reconcile with the earlier successor handoffs)

- **The measured handoffs are correct, and they are not production.**
  - The 20/20, 21/21 and 5/5 handoffs come from the C5.3b-m **measurement requester** (`RollingSuccessorProbe`),
    which the probe image enables at boot with the DEC-147 lead price waived (`installAtBoot`:
    `bypassLeadPrice = true`).
  - With the price in force, production admits no successor at N's end (`test_rolling_successor_lead`:
    `test_c5_3b_the_declared_price_refuses_every_successor_at_n_end`).
  - The manifest-lane host suite never enables the requester. The first Gate A RED therefore exercised production
    with no successor preparation, which is why it showed a gap at every boundary.
- **Same path, same knobs.** The host Gate A case with the probe image's knobs plays steady state continuously: every
  tick served and every take note, in traversals 5-6. That matches the device's continuous `view` route.
- **Classification of every silent interval** (host with probe knobs, and device A1):

| interval | successor state |
|---|---|
| playback start (traversal 0 -> bar 2 of traversal 1) | the chain's **first** window: none covers the current phase; ready, but servable only from a later two-bar boundary |
| after the Overdub stop (traversal 3 -> bar 2 of traversal 4; device 77.2 -> 97.9 s) | the revision move ends the serving chain (`win_not_servable`). The chain restarts with a first window: on device about 7 s of busy lane plus whole-loop qualification, then ready at 85.4 s but reachable only at 97.9 s |
| after the sweep's restart (device 144 -> 159 s) | the same chain cold start |
| steady state with the requester on | none: successors are requested, prepared, adopted and handed off with audible coverage |

- **What the counters miss.** No interval is a successor that was counted as handed off without audible coverage.
  The counters are honest about successors. They simply never count a chain's opening window, and every silence
  observed is one.
- **The defect to fix (not implemented).** A chain cold start (playback start, a revision move, a restart) has no
  window covering the current phase until the first window's start is reached.

## Gate B (DEC-249)

- **Fixed:** `SnapshotReserve` resolves a low tail with the read-only walk; see DEC-249.
- **Device:** tail 32,768 B, largest free 54,824 B, walk 13 µs, publication resumed and published.
- **Open:** a roughly 50 s publication-idle window after the stop on the default image, with memory admitted.

## Record-stop gap on the succprobe image (2026-11-04, `captures/session_20261104_002601.log`)

**Run.** Image `fc0a5e80` (succprobe), track 8 slot 4, dense 32-bar take, Record stop, no overdub. The routed slot is
silent from **61.8 to 94.9 s**: `tick_route` shows `none=600` per 5 s row. Track 7 (0-based) is the routed track. The
gap has two parts with different owners.

**Part 1: 61.84 -> 86.63 s (~25 s), `win_busy` / `ReadyRoleOccupied`.**
- Before the gap:
  - At 60.14 s the Record stop's commit moves the source (`disp_src crev 0 -> 1`).
  - At 61.12 s the frozen Record reader's successor is still built and published into `ready`
    (`build_drop,published_as_ready`).
- At 61.84 s the producer retires the serving frame for `ServingViewSourceStale` (`win_stale_serving_retired`). It
  cannot position a replacement. The one roles row reads `busy,roles,9,2,1,52,0,255,255,68`:

  | field | value |
  |---|---|
  | refusal | 9 = `ReadyRoleOccupied` |
  | current | view 2 (complete, published) |
  | ready | view 1 (complete, unpublished) |
  | build | view 0 (empty) |
  | retained | none (255) |
  | owners | 68 = pending request (4) + `hasBoundRecordReturnWindowForPendingJob` (64) |

- The orphan-successor reclaim ("orphaned successor" in the requester) explicitly excludes both owners. The
  stale-retirement path clears only an *unpositioned* pending demand. So nothing releases the `ready` window.
- At 86.63 s the playhead reaches that window's own start phase again (8448, one traversal = 25,344 ticks ≈ 26.4 s at
  300 BPM). It is adopted (`route,turn,...,8448`, `adopt_wait,e2_boundary`), and the Record reentry releases
  (`Reentry released slot=3`).
- **Reading (medium confidence).** The Record-return job's successor window outlives the source move that retired its
  frame. It keeps the `ready` role and the job binding until its own boundary, a full traversal later. The retirement
  treats the serving view's stale source as evidence of obsolescence, but not the ready view built from the same
  pre-commit source.
- **Not proven.** The ready view's source identity was not logged. That it is the pre-commit source is inferred from
  its publication time (61.12 s, after the 60.14 s commit, with the reader still frozen) and from playback staying
  silent after it is adopted.

**Part 2: 86.97 -> 94.94 s (~8 s), ready but not serving.**
- After the release, a fresh full-loop window is positioned and built quickly:
  - 86.65: `qualification_started`
  - 86.71: `handoff_unreachable`, then `win_requested`
  - 86.82: `build_admitted`
  - 86.97: `win_ready`
- It is first served at 94.94 s, at phase 16416, about 8,000 ticks past the nearest span boundary.
- This is the completion-aware placement item already open (host RED
  `test_c5_3_completion_placement_serves_the_nearest_eligible_boundary`, `COMPLETION_PLACEMENT_RED`).

**Not caused by slices 1 and 2 (by construction, not by an A/B run).**
- No successor row exists before 95.15 s, so the frame was never in two-span states 4/5, and slice 2's exceptions
  cannot apply.
- `retained` is 255 throughout, so slice 1's orphan release is not involved.
- Gate A's earlier A1 run showed only `none=10` at the Record stop, but with a different gesture sequence (an overdub
  followed). An A/B of this exact stimulus on the image before `05057ed3` would settle whether it is a regression.

**Candidate direction (owner decision; not implemented).** Make the `ServingViewSourceStale` retirement also discharge
a `ready` window bound to the same Record-return job whose source is not the current one: an identity-guarded release
through the lifecycle's superseded-ready path, never by timeout. Part 2 belongs to completion-aware placement.

### Record-stop gap: Ready-source identity verdict (owner ruling on `05042537`, 2026-11-04)

**Instrument.** `Track::reportStaleRetirementIdentities` emits report-only `stale_id` rows at the stale-serving
retirement edge, one per party, before anything is retired.

**Device run.**
- Image `teensy41-capture-serial-revstore-succprobe`, HEX
  `63d950bf9a9e494375b868233ab6f4cb1a8a006f78ae123e070d1989dbfeb5c7`.
- Boot `crash=0`, `BOOT,load,ok`.
- Exact stimulus as before; capture `captures/session_20261104_004411.log`.

**Identities at the edge (66.29 s).** Values are taken from the rows; nothing is inferred from timing.

| party | origin | rev | manifest | gen | capture | interval | state |
|---|---|---|---|---|---|---|---|
| serving `current` (view 1) | Capture | 0 | 0 | 0 | 1538 | 9984+768 | complete, published |
| occupied `ready` (view 0) | **Committed** | **1** | **1** | **0** | 0 | **9216**+768 | complete, unpublished |
| `build` (view 2) / `retained` | empty / none | | | | | | |
| pending | CommittedSnapshot | 1 | 1 | 0 | -- | 9288+1536, deadline 59976 | active, positioned |
| frozen request | none | | | | | | |
| Record job (qualified source) | Capture | 1 | 0 | 0 | 1538 | interval 1 | qualified, retirement active, owns extent, owner current |
| effective committed source | loop rev 1, servable manifest 1 (state Ready), generation 0 | | | | | | presentation bound, committed current |

**Verdict: B. `ready` is source-correct, not superseded.**
- Its origin, revision, manifest identity and generation equal the effective committed source.
- It is the bound Record job's **DEC-136 root-return successor**. Its interval is the one that contained the demand
  phase, and its adoption boundary is that interval's next entry. `rootReturnSuccessorIsOwedForSlot` says so
  explicitly: timing is "deliberately not judged here".
- The retired serving view is also valid by the job's own predicate. It matches the qualified frozen identity
  (Capture, gen 0, capture 1538), which is `fromThisFrozenJob` in the E2 route.
- The resolver's `ServingViewSourceStale` is the **origin** term of the committed rolling check
  (`current.source.origin != Committed`), not evidence of a stale source. This bears on DEC-159: revision 0 against 1
  is not invalidity here.

**Sequence (device).**
1. 64.67 s: the frame adopts frozen bar 9216. The root-return demand for 9216 is raised.
2. 65.47 s: the frozen bar 9984 is adopted from `ready`. That frees `build`, and the root-return build is admitted
   (65.49) and published into `ready` (65.53) for 9216, which is now a traversal away.
3. With `ready` held, the frozen reader never prepares bar 10752. Its `complete_ready` rows, one per bar until 64.77 s,
   stop.
4. 66.27 s: the playhead leaves the 9984 bar. The committed resolver rejects the Capture-bound frame by origin, the
   producer retires it, and the positioned committed demand is refused `ReadyRoleOccupied`
   (`busy,roles,9,1,0,13,2,255,255,68`).
5. 91.11 s: 9216 recurs, and the root-return window is adopted.

**So the failure is role contention inside one owner (the Record-return job).** Its DEC-136 root-return successor
parks in `ready` for a traversal, which starves the same job's frozen reader of the role its next bar needs. DEC-136
item 2 says the frozen reader keeps serving until the root-backed successor is ready; with three views it cannot.

**Regression status: pre-existing.**
- Same stimulus on the image before slice 1 (`bf482df9`, HEX `6634501f…27f19f`, boot `crash=0`,
  `captures/session_20261104_005353.log`).
- Identical roles row at the retirement (60.04 s), release about one traversal later (84.88 s), routed slot silent
  about 62-97 s.

**Host.**
- `test_c5_3_record_stop_a_late_ready_never_holds_the_role_for_a_traversal` (`RECORD_STOP_LIVENESS_RED`): the Record
  gesture into PLAYING on the routed slot. A late ready of the **current source** (asserted) holds the role for
  **3065 ticks** while the playhead's demand is refused. FAILS as intended; non-vacuous (`lateSeen > 0`).
- Guard `test_c5_3_record_stop_a_timely_ready_is_adopted_and_nothing_plays_twice` (default gate): passes.
  - Every timely ready is adopted at its own start (6 observed, 0 lost).
  - Every Capture-origin Record-return window completed after the stop is served before its view is reused
    (non-vacuous).
  - No NoteOn plays twice in any traversal.
- The host does not reach the stale-serving retirement (its frozen reader runs out differently). It reproduces the
  same occupied-late-ready mechanism. The retirement is downstream of the contention, not its cause.

**Smallest ownership-correct repair (proposal; not implemented).** The repair belongs to the Record-return job, case B.
No Ready retirement, no timeout, no fallback, no DEC-147 change.
1. Position the root-return successor at the frozen reader's next **unserved** boundary, not the interval containing
   the demand phase. Its adoption then *is* the next bar's handoff, so it takes `ready` exactly when the frozen reader
   would have needed that role for its own next bar, and the frozen frame hands off to the committed window there.
2. If it completes after that boundary, the job applies the rule its frozen reader already uses for its own late
   readies (`record_ready_late`): the late view is retired through `retireSupersededReady`, and the one owed demand
   re-targets the next boundary. That is exactly one replacement, from the same owner.
3. Separately for DEC-159: the committed resolver should not label a frame bound to the job's own qualified frozen view
   `ServingViewSourceStale`. It is the E2 route's frame, not a stale committed one.

The 8 s completion-placement delay stays separate.

### DEC-136 next-boundary repair: blocked by DEC-147's lead floor (owner ruling 2026-11-04, approved narrow repair)

**REDs** (`test_revision_manifest_lane`, gated `RECORD_RETURN_RED`, failing on unchanged production code):

| case | result today |
|---|---|
| `test_c5_3_record_return_on_time_root_replaces_frozen_exactly_once` | "the root handoff waited for a later traversal" |
| `test_c5_3_record_return_delayed_root_never_blocks_frozen_continuation` | "traversal 1: the take's bar-2 note was not played" |

- The delayed-root case withholds every lane turn while a root qualification counts, for the first three bars after the
  stop, using the harness's test-only `gWithholdLaneTurn`. Non-vacuous: `withheld > 0`.
- Both cases also gate exactly-once root handoff, no Capture view after the root, and every NoteOn closed by exactly
  one NoteOff.

**What the trial implementation showed (host; reverted, not committed):**
1. Retargeting worked. Root requests named the boundary after the frozen `ready`: requested at 3276 for 1536 while the
   frozen 768 ready held the role, then qualified and awaiting admission at 3278.
2. At the frozen handoff (3841, the first instant the single successor role is free) admission refused it:
   `start=1536 deadline=4608 lead=767 price=4 leadTooShort=1 privateRoot=1`.
   - `CoverageServiceContract::canCompleteWithinLead` requires `available >= leadTicks + turns x advance`, with
     `leadTicks = TICKS_PER_BAR` (768).
   - Admitted at the handoff into bar B, the next unconsumed boundary B+1 is at most 768 ticks away. So **every**
     next-boundary root window is refused, whatever its grains.
   - The DEC-136 waiver (`ordinaryAdmissionCannotServeDemand`) applies only when no lead in the whole loop could serve
     it, so it does not apply.
3. The current code works only because the interval-containing-the-playhead target puts the deadline a traversal
   away. That same choice is what parks the root in `ready`.
4. A second, independent defect, needed for outcome 2: the Record-return branch discards any active build whose source
   is not the committed root (`SourceSuperseded`). That discards the frozen reader's own continuation build every turn.
   Frozen continuation also needs that discard to exempt the job's own frozen build.

**Conflict.** The approved target ("next unconsumed handoff boundary") cannot satisfy outcome 1 with:
- DEC-147 admission unchanged;
- one-bar frozen windows;
- the single `ready` role (admission waits for it).

Without outcome 1 the root never takes over, and the Record-return job never releases.

**Options for the owner** (preferred first):
- **(a) Park the root beside the frozen ready.** Admit the root-return successor into the free third view *while* the
  frozen Capture `ready` holds the next boundary, targeting the boundary after it (lead ≥ one span). Keep it complete
  and unpublished in `build`, and publish it at the frozen handoff without a rebuild. This is the three-view parking
  shape slice 2 proved, applied as its own narrow Record-return exception, not the two-span requester. DEC-147
  unchanged.
  - Late (incomplete at the frozen handoff): its build is retired through the existing transitions, and the frozen
    reader prepares the next bar (outcome 2).
- **(b) Waive the DEC-147 lead for the root-return successor at the next boundary.** Simpler, but a DEC-147 change.
- **(c) Keep the traversal-later target,** but hold the root candidate outside `ready` until its boundary. That needs
  a role for a parked committed window for a whole traversal; it is a lifecycle change.

The `ServingViewSourceStale` correction (authorized frozen predecessor) is independent and was not started. It touches
the clock-path resolver, and the ruling sequences it with this repair.

### DEC-136 option (a): admission analysis, ownership REDs and the proposed speculative rule (owner ruling on `f871b67d`)

**The exact root candidate.** Measured on host with a temporary trace (reverted). The candidate is the committed
root-return window targeted at the boundary after the frozen `ready`.

| term | value |
|---|---|
| ownership authority | `rootReturnSuccessorIsOwedForSlot` (DEC-136 item 1): presentation `retirementActive`, qualified frozen source, current root identity, bar-shaped coverage, frozen return window served or owed. Owner: the Record-return job (`CaptureStopPresentation`). |
| grant count G | host 4. The device's dense take is 7 (`max_grains`), so price = 768 + 7 x 8 x 64 + 128 slack = **4,480** ticks, the owner's figure. Host: 2,816 + 128. |
| beside the frozen `ready` | qualification runs privately (`coverageRoleRefusal(true)` / `beginCoverageQualification(..., true)`, the `keepCaptureReady` exception) and reaches `AwaitAdmission`. No admission is evaluated while `ready` holds the frozen view. |
| admission at the frozen handoff | `lead=767 required=2816 leadTooShort=1 privateRootOwed=1 waived=0` |
| `beginQualifiedCoverageBuild` | refuses while `ready` is complete ("cannot discharge until the single successor role is free") |

**No existing DEC-136 exception admits it.**
- Item 2 (owed without a deadline) only bypasses the gate's epoch and deadline terms. The admission lead is still
  judged.
- Item 3 (`privateRootWaived`) applies only when `ordinaryAdmissionCannotServeDemand`, that is, no lead within the
  whole loop could serve the cost. The device loop is 25,344 ticks, far above 4,480.
- The `keepCaptureReady` exception permits qualification, not admission.

So any lead available at a frozen handoff (≤ 768 to the next boundary, ≤ 1,536 to the one after) is below the
4,480-tick price.

**Ownership REDs** (`RECORD_RETURN_RED`; all FAIL on the unchanged production code):

| RED | case | fails with |
|---|---|---|
| 1 | `..._red1_root_prepares_in_build_beside_frozen_current_and_ready` | never prepares beside frozen current+ready |
| 2 | `..._red2_root_parks_then_publishes_once_ready_is_free` | never parks |
| 3+4 | `..._red3_red4_delayed_root_yields_and_frozen_builds_survive` | 0 frozen bars prepared after the root (needs ≥ 2, 0 killed, 0 gap ticks) |
| 5 | `..._red5_handoff_exactly_once_and_no_gap` | 6,150 gap ticks |
| 6 | `..._red6_retained_third_view_refuses_root_and_frozen_plays_on` | the root prepared over the retained third view (18 ticks) |

Notes on the cases:
- RED 3+4 and 5 also gate one handoff, served-view source generation, and paired NoteOn/NoteOff.
- On RED 4 (`SourceSuperseded`): a kill is only *reachable* once frozen continuation exists. On today's code no frozen
  build starts after the root becomes current, so the RED fails on non-vacuity. The kill itself was observed in the
  reverted trial (drop reason 6 on every other turn).

**Proposed rule: DEC-136 item 4, speculative root preparation (Record-return only; owner decision).**

The root-return candidate may be admitted **speculatively** into the third physical view, without the DEC-147 lead
test, only while all of the following hold. They are evaluated in the deferred Record-return owner and never on the
Clock.
- **S1 (authority):** `rootReturnSuccessorIsOwedForSlot` holds for the request.
- **S2 (frozen playback guaranteed):**
  - `current` is the job's frozen view, published and containing the playhead. The view matches the qualified frozen
    identity: Capture origin, source generation, capture content generation, loop length.
  - `ready` is the job's frozen view, complete and unpublished, starting at the next boundary B+1.
- **S3 (target):** the candidate covers the bar-shaped interval after the frozen ready, B+2.
- **S4 (free third view):** `build` is distinct from current, ready and retained, and unpublished, and no valid retained
  view holds it. The builder is free except for this candidate.

Speculative means:
- No admitted promise. The demand keeps deadline 0 and epoch 0 (the existing `PrivateRootReturnPreparation` reason),
  so it never enters DEC-147's promise ledger.
- Other tracks' admitted promises are still protected: `playbackViewAdmissionKeepsAdmittedDeadlines` stays a term.
- Normal successor pricing is untouched; the rule cannot be reached by any non-Record-return request.

Lifecycle, using existing transitions only:
- **Park.** A candidate completing while S2's frozen `ready` holds the role stays complete and unpublished in `build`.
- **Publish.** On the first owner turn after the frozen handoff at B+1 (`ready` free), a parked candidate whose start
  equals the next unconsumed boundary (B+2) is published with `publishCompletedBuildAsReadySuccessor`. The existing
  root-return adoption takes it at B+2, and the job releases (exactly once).
- **Yield (a miss is never audible).** On that same turn, a candidate that is incomplete, or parked for any other
  boundary, is discarded through `discardCoverageBuild` / `retireCoverageSource` / `resetMetadata`. The frozen reader
  then prepares B+2 in that view exactly as it does today.
  - Because S2 requires the frozen reader to already hold B+1 before the root may take the builder, and the builder
    returns at the frozen handoff, the frozen reader always has its next bar, whatever the root's timing.
- **Frozen builds.** The Record-return branch's `SourceSuperseded` discard exempts the job's own frozen build (the same
  identity as S2).
- **No new state.** "Parked" is read from the lifecycle (a complete, unpublished Committed `build`). No queue, buffer,
  timeout or Clock work.

The `ServingViewSourceStale` correction (explicit Record-return authorization) follows once these REDs are green.

### DEC-136 item 4: frozen-continuation envelope and slice 136.4a (owner ruling, conditional approval)

**Frozen-continuation envelope.**

Grants per frozen (Capture) bar, measured on host with `FROZEN_ENVELOPE_MEASURE`
(`test_c5_3_record_return_frozen_continuation_envelope`). This is the same gesture as the device run, with one lane
turn per tick.

| take | grants per frozen bar |
|---|---|
| 4 bars, 1 note/bar | 1 |
| 4 bars, 16ths | 3 (2 fill + 1 order) |
| 32 bars, 16ths (the device's dense shape) | 3 (2 fill + 1 order) |

The frozen builder reads `entryQuantum = 32` rows per fill grain.

Device: in all three captures (`session_20261104_002601`, `_004411`, `_005353`; 13-15 frozen bars each) every frozen
bar completed with **624-704 ticks of its 768-tick bar left**, i.e. 64-144 ticks after its start.

**Is starting frozen B+2 after the B -> B+1 handoff safe?**
- **Under the declared service envelope: no.** G_f x (H+1) x advance = 3 x 8 x 64 = **1,536 ticks**, more than the
  768 available.
  - The declared envelope cannot certify the *baseline* frozen cadence either: the frozen reader has always started
    its next bar after each handoff.
  - Without a 64-tick turn guarantee there is no tick-domain proof at all.
- **What can be proven is relative:** speculative preparation adds **zero grants** to the frozen path. Two conditions
  make that true:
  1. **No frozen-path grant before the handoff.** The root may hold the builder only while the frozen reader's next
     bar (B+1) already sits complete in `ready` (S2). The frozen reader then has no work it could be denied.
  2. **A free yield at the handoff turn.** The root releases the builder on the handoff turn at no cost:
     `retireCoverageSource` -> `releaseManifestCoverage()` on the routed slot, with no retirement grains.
     A snapshot-backed root goes through `Retiring` (release grains), so it could delay the frozen build. Speculation is
     therefore limited to the **manifest-routed slot**, which is the only slot where the gap exists: the unrouted
     default has the merged fallback.
- **Latest safe yield:** the frozen B -> B+1 handoff turn, provided the yield is free there. With that, frozen B+2
  starts at exactly the point it does today.
- An earlier yield buys nothing: the existing frozen progression can only prepare B+2 once `ready` frees at the
  handoff.
- Any absolute guarantee for the frozen cadence (declared or measured) is a separate, pre-existing question. It does
  not change with this slice.

**`gWithholdLaneTurn`, corrected.** The delayed-root predicate (`delayRootQualificationUntil`) now withholds a turn
only while it could advance the root: the root owns the coverage source while the frozen next bar holds `ready`. Turns
stay open in every other case:
- the owner turn at the frozen handoff, where the root must yield;
- every frozen-continuation build (a frozen build never owns the coverage source).

The earlier predicate withheld every turn while a qualification counted, so it could not establish frozen-reader
liveness.

**Slice 136.4a** (REDs 1, 2 and 6 green; production code in `TrackPlaybackWindowBuild.cpp` and
`TrackPlaybackRuntime.cpp`):
1. `Track::recordReturnSpeculativeRootTarget` (cold) is S1-S4 with full identity, routed slot only.
   - It checks: presentation `retirementActive` and `ownsExtent`; the qualified frozen identity; `current` and `ready`
     both the job's own frozen views (`recordReturnOwnFrozenView`: Capture origin, no snapshot, source generation, the
     capture content generation of both the receipt and the job, a bar-aligned interval); the playhead in `current`,
     or exactly at `ready`'s start on the boundary tick; `ready` at the next boundary; a free, unpublished `build`;
     and no complete retained view.
   - The target is the bar after the frozen ready.
2. `recordReturnSpeculativeRootAdmissible`: the request's coverage equals that target.
3. The Record-return branch's Capture-ready sub-block names the root at the speculative target when S1-S4 hold.
   Otherwise it keeps the existing obligation. A parked root discharges it: no re-request.
4. `playbackBuildGateRefusal`: the speculative candidate is the one qualified root exempt from `CaptureReadyWait`.
5. `stepCommittedPlaybackRetry`: it is admitted through the existing DEC-136 path (`PrivateRootReturnPreparation`,
   deadline and epoch 0, no DEC-147 lead test, sibling promises still checked), with
   `beginQualifiedCoverageBuild(..., besideFrozenReady = true)`.
   - That parameter lets the build begin beside an unpublished Capture `ready` in a distinct, unretained view, and
     never over `ready`.
6. Parking: the branch's leftover-shell reset spares a complete, unpublished current-source root for the speculative
   target (or, once `ready` is free, for the next unconsumed boundary).
7. Publication: once `ready` is free, that parked root is published with `publishCompletedBuildAsReadySuccessor`, and
   only for the bar after the one the playhead is served in. The existing root-return adoption takes it at that
   boundary.

Normal DEC-147 pricing is untouched. There is no new view, worker, queue, buffer, timeout or Clock work, and no new
state.

**Budget.**
- A first form put the identity check in a lambda, which landed in ITCM (+128 B; the RAM1 gate refused it).
- As a flash-placed static function, ITCM is unchanged: 392,312 B code, padding 904, 64 B over the floor. The default
  image is also unchanged (304 B).

**Results.**
- Host: REDs 1, 2 and 6 pass and are promoted to the default gate. In RED 2 the root parks in v2 at 3282, is published
  at the 3841 handoff, and is adopted at 4609 (boundary B+2) from the same view and storage generation, exactly once.
- RED 6 is non-vacuous (frozen authority held beside the retained view). Its refusal is structural: with frozen
  current, frozen ready and a retained view there is no `build` role at all. The S4 retained clause is defensive.
- Native full: 3,996 cases, 0 failed. Default lane gate: 21/21.
- REDs 3-5 stay gated for 136.4b.
- The earlier `RECORD_STOP_LIVENESS_RED` (a late ready holding the role) now passes.

**Open for 136.4b, found here.**
- After the root handoff the routed slot is still silent: RED 5 shows 3,081 gap ticks, and there are no notes after
  4609.
- The root-return adoption on the clock edge rebinds `frame.boundView`, but keeps the frozen frame's geometry
  (`startPhase` of the frozen bar, `deadlineTick = 0`, i.e. a whole-loop span).
- Once the Record job releases, the committed resolver rejects the adopted one-bar root window as `ServingViewGeometry`,
  which is silent on the routed slot.
- This is pre-existing: the baseline traversal-later adoption has the same rejection, and it explains baseline
  traversals 2-3 being silent.
- This is a clock-path (ITCM) site, and the 64 B headroom bounds the fix.

**136.4a independent review (one focused pass, `docs/Agents/reviewer.md`), findings and disposition:**

| finding | severity | fix |
|---|---|---|
| F1: a refused speculative attempt left the frozen request with deadline 0 and `PrivateRootReturnPreparation`, so a later ordinary admission would wrap the `uint64_t` lead and skip DEC-147 | high | fixed: a refused speculative attempt restores the request exactly (`refuseSpeculation`); the long-standing waived path is unchanged |
| F2: the gate and admission derived the phase from the live clock, not the turn's sample | medium | fixed: phase from the turn's `clock`; active slot only |
| F3: park-preservation and publication also ran on the unrouted slot | medium/low | fixed: both gated on `REVISION_MANIFEST_READER` and `manifestLaneRoutesSlot`; publication now requires `parkedSpeculativeRoot` |
| F4: the overlay candidate was not excluded | low | fixed |
| F5: an unqualified pending speculative request is replaced after the handoff | low, fail-closed | tracked for 136.4b |

- F1 has no dedicated injection test yet: it needs a sibling admitted promise during the speculative turn. That is
  noted for 136.4b.
- After the fixes: REDs 1, 2 and 6 pass, the default lane gate is 21/21, and ITCM is unchanged (392,312 B, padding 904).

### DEC-136.4b geometry: span-shaped root takeover (owner ruling "DEC-136.4b geometry ruling — approve option 1", 2026-11-04)

**Conflict found before any edit.**
- The committed resolver serves a frame only when the bound view's length equals the frame's span, and the span is
  derived (`deadlineTick == 0` ? the whole loop : `rollingFrameSpan`, i.e. `rollingWindowLengthAt(start)`).
- `RollingFrame` has no span field. A one-bar root window is therefore coherent only on a loop's last bar.
- Approved option 1: the speculative root candidate takes the exact coverage the committed rolling grid expects at its
  takeover boundary. The 136.4a one-bar requirement is superseded for speculative root-return candidates only.

**The amendment.**

| term | change |
|---|---|
| target shape | `recordReturnSpeculativeRootTarget` S3 is now `[B, B + rollingGridWindowLengthAt(B))` on the routed grid (equal partition, as the routed requester positions). `rollingGridWindowLengthAt` is `rollingWindowLengthAt`, but 0 unless the window ends on a partition boundary or the loop's end. So: an on-grid start gives the normal two bars; the terminal bar gives one bar; an equal-partition bar start runs to its own window's end; an off-grid default-partition start (a two-bar window crossing a boundary) has no target, and no speculation happens at that boundary. |
| owed shape | DEC-136 item 1's one-bar clause also accepts a span-shaped request, but only when `recordReturnSpeculativeRootAdmissible` holds for that exact request. Every other item-1 term (source, revision, generation, manifest identity, bar-aligned start) is unchanged. |
| parked target and publication | the same shape function. Parking, the leftover-shell guard and the publication rule are otherwise unchanged. |
| takeover re-key | at the root-return adoption on the clock edge, the frame takes the adopted window's `startPhase`, `deadlineTick` = the boundary's absolute tick (0 only for a whole-loop window, the deadline-free frame), and the routed partition bit. These are fixed-metadata stores, O(1). `epoch`, `generation`, `loopLength` and `projectionStart` were already checked equal by that branch's guard. |
| boundary survival | the main-loop requester (`serviceRollingPlaybackWindow`) cancelled the frozen frame on the exact boundary tick, before the clock edge could take over, because the served window no longer contains the boundary phase. The takeover adopts only from a Serving frame. On that one tick the frame is now kept when a complete, unpublished ready starts exactly there. If the clock edge does not adopt it, the next tick is past the boundary and the existing cancellation runs. |

**Unchanged.** No `RollingFrame` field, no change to `ServingViewGeometry` or any resolver term, DEC-147 pricing,
DEC-159, DEC-247/248, DEC-249, the three-view lifecycle and publication. No new builder, buffer or queue. ITCM is
byte-identical on both images:

| image | ITCM code | padding | headroom over 840 |
|---|---|---|---|
| `teensy41-capture-serial` | 392,072 | 1,144 | 304 B |
| `teensy41-capture-serial-revstore-succprobe` | 392,312 | 904 | 64 B |

**RED-first.** The tests below ran against `ea05fb82`'s `TrackPlaybackWindowBuild.cpp` (the 136.4a resolver and
adoption), then against the amendment:

| ruling item | test | on 136.4a | amended |
|---|---|---|---|
| 1, 7 | RED 1: the root builds beside frozen current and ready, as the grid's window (`rollingGridWindowLengthAt`, more than one bar) | FAIL (768 ticks, one bar) | PASS |
| 1, 6, 7 | RED 2: parked span-shaped; source (Committed, manifest identity) and coverage unchanged through publication and adoption | FAIL (one bar) | PASS |
| 2 | RED 5: at the takeover, frame `startPhase` = the window start, `deadlineTick` = the takeover tick, epoch, generation and loop length match, and `rollingFrameSpan(frame)` = the view's length | FAIL (not a serving frame) | PASS |
| 3 | RED 5: every take note in the adopted window is emitted on a tick where that very window is the serving committed view of a Serving frame, one per bar; exactly one root handoff; no gap; every take note paired exactly once over traversals 1-6 | FAIL | PASS |
| 3 | on-time root replaces frozen exactly once (bar-2 note plays) | FAIL | PASS |
| 4 | shape refusals: off-grid default start gives 0, terminal bar gives one bar, equal-partition start runs to its window's end; live, a wrong span, a wrong boundary and a wrong source are refused, and the exact target is admissible and owed | PASS (guard) | PASS |
| 6 | RED 5: the root serves its whole window from the adopted view, and the Record-return job retires while committed playback serves | FAIL (earlier assertion) | PASS |
| 4 (resolver) | `ServingViewGeometry` is untouched; the existing resolver geometry tests cover wrong-span serving views | unchanged | unchanged |

- **Witness correction.** The gap witness runs after the lane turn and before the clock edge, so every handoff
  boundary tick counted as a gap, ordinary committed-to-committed handoffs included (11 ticks, all at window starts). A
  ready view starting exactly at the playhead now counts as served on that tick. A missed adoption still shows on the
  next tick.
- **Gates.** On-time and RED 5 move to the default lane gate.
- **Continuity after the takeover.** On the probe image (`enableProbeImageSuccessors`), the committed successor after
  the root window is adopted on time, so RED 5's continuity over traversals 1-6 passes there.
- **Production dependency, named.** The production image has no C5.3b successor requester. There, the window after the
  root comes from DEC-147-priced positioning. That is the C5.3b scheduling item, not part of this patch. The local
  takeover gate (RED 5's takeover assertions) is distinct from that continuity.
- **Still red, the frozen-priority slice.** The delayed-root test and RED 3/4 fail as before: a larger root that misses
  its handoff must yield without costing frozen continuation (ruling item 5).

**Geometry slice independent review (one focused pass, `docs/Agents/reviewer.md`), findings and disposition:**

| finding | severity | fix | RED on the unfixed code |
|---|---|---|---|
| F1: off-grid fallback. With the frozen authority held, an off-grid boundary returned no target, so the Record-return owner fell back to the ordinary one-bar demand at the playhead's own, already-started bar. | medium | `recordReturnSpeculativeRootTarget` answers "authority held, no window here" with an empty target. The owner then waits (`Idle`) for a later on-grid boundary. Admission requires a non-empty target. | `..._off_grid_boundary_waits_instead_of_one_bar_fallback`: 1 fallback demand before the fix, 0 after. The off-grid state is detected from the views alone, and is reached by holding the root publication until bar 3 (fixture `holdPublicationUntil`). |
| F2: owed status lost at the handoff. A span-shaped root stopped being owed once the frozen ready was adopted, so a qualification still in flight lost DEC-136's owed classification and item-3 waiver. The one-bar root kept them by its shape. | low-medium | `recordReturnSpeculativeRootOwedAfterHandoff`: the exact grid window from the end of the job's own frozen view now serving stays owed, routed slot only, until the bound job releases. | `..._span_shaped_root_stays_owed_after_the_frozen_handoff`: fails before the fix, passes after. |
| Boundary survival does not check the ready's origin | low, bounded | none. The kept frame lives one tick at most: at end+1 the existing cancellation runs, and the resolver's Defect-B path still retires a stale Capture frame. | — |
| Test gaps: no equal-partition loop end to end; the refusal test samples only the first target; the wrong source exercises only the revision term | noted | none in this slice | — |

**Continuity finding for the frozen-priority slice (not caused here).** In the F1 fixture (root current during bar 3,
then the off-grid wait), the frozen reader stalls after its handoff to bar 0. The slot has 6,912 gap ticks, and the
first root handoff is at tick 9,217. The stall is the same with the F1 wait disabled and on the pre-review geometry
code, so it is the existing frozen-continuation arbitration (a committed demand holding `build`/`ready` against the
frozen reader's next bar). That is ruling item 5's subject, and it gets its own RED in that slice.

**Gates after the review fixes.** Default lane gate: 27 cases, all passing. Both images are byte-identical in ITCM
(392,072 / 392,312 code; padding 1,144 / 904).

### DEC-136.4b frozen-priority arbitration (owner ruling, after the geometry slice `bfd9d94f`)

**What the delayed-root RED showed (host, routed slot, root qualification withheld while a frozen ready is held).**
1. At the frozen handoff the incomplete speculative root's work was dropped. The Record-return owner then raised the
   *ordinary* one-bar root demand for the bar already playing. It built into `ready`, so the frozen reader never
   prepared its next bar, and playback was silent until that root was adopted a traversal later.
2. Once the root is current, the Record-return branch takes every turn. The frozen-continuation branch ran only while
   the root was *not* current, so nothing prepared the frozen reader's next bar.
3. The holder selector's root-current term `currentRebindable` read a frozen bar built after root publication as
   root-served: such a bar carries the root's revision and generation. The slot then offered no grain, so neither the
   frozen reader's next bar nor the speculative root (nor the parked root's publication) got a turn.
4. With the frozen reader starved for a bar, the committed resolver judged the frame bound to the job's own frozen
   view as `ServingViewSourceStale`. The producer's Defect-B path then retired it and could clear the root's demand.

**The rule (routed slot only; three views, one builder, no new state).**

| term | change |
|---|---|
| frozen continuation owed | `recordReturnFrozenContinuationOwed`: the job (bound, retiring, owning its extent, qualified frozen identity) has its own frozen view serving the playhead, `ready` is free, no root for that view's end is parked complete in `build`, and **no retained role holds a view**. With a retained third view only two views rotate, so the root can never be prepared beside the frozen reader, and priority would keep it from ever taking over. That case keeps the pre-slice ordering (found by `test_5_1_routed_dense_take_presentation_yields_to_the_manifest_lane`, whose incomplete retained role is not released while the job owns the slot). |
| yield | while it is owed, root work (on this slot the only coverage-source owner, plus a pending committed demand) is discarded. The routed release is free. The demand is retired by name (`frozen_priority_yield`, a new report-only `CoverageBuildDropReason`), not left to be coalesced. The Record-return branch steps aside. The frozen-continuation branch prepares the next frozen bar from the frozen receipt, even with the root current. |
| SourceSuperseded | the Record-return branch never discards an active frozen build that carries the job's full frozen identity. |
| holder offer | `offerRecordReturnPriorityHolder`: an owed frozen continuation (lead to the served frozen view's end), else the speculative root while its target holds (lead to its own boundary; nothing for an off-grid wait or a root already parked). Also, on the routed slot `currentRebindable` no longer accepts a Capture-origin view. |
| wasted demand | with the published root in `ready` for the served frozen view's end, the Record-return tail raises no ordinary one-bar demand beside it. |
| `ServingViewSourceStale` | a deadline-free frame bound to a view the **active** job authorizes (`recordReturnJobAuthorizesFrozenView`: bound and retiring, full frozen identity), once that view no longer covers the playhead, is expired to Idle (`ServingExpired`), as a served-out frame is. The frozen route adopts its next window from Idle, and the root's demand is untouched. Any other Capture or committed frame keeps every existing term. |
| F5 | a pending speculative demand is retired by name at each yield, so it is never coalesced into a later target's record (`retainViewPendingRequest` keeps the older position). Admission re-checks the exact current target, so no waiver authority outlives the target. |
| F1 | native seam `gNativeSiblingPromiseRefusesForTest` inside `playbackViewAdmissionKeepsAdmittedDeadlines`, the real sibling-promise term. Native builds only. |

**Tests.**

| test | before (pre-slice `bfd9d94f`, or the line removed) | after |
|---|---|---|
| delayed root never blocks frozen continuation (was `RECORD_RETURN_RED`) | FAIL | PASS, default gate |
| RED 3/4: delayed root yields; at least 2 frozen builds after the root, none killed, no gap, one handoff, notes paired | FAIL | PASS, default gate |
| F5: no pending speculative demand outlives its target or carries another boundary's deadline; no frozen build killed; no gap | FAIL on the gap (6,143 ticks); the stale and deadline terms already held (guard) | PASS |
| F1: under a sibling's refusal the root is never admitted, and every refused attempt leaves the request at its DEC-147 position and own reason (no zero-deadline private form); later exactly one takeover, no gap | FAIL with the restore removed (157 damaged attempts) | PASS |
| authorized frozen frame never labelled stale or blocked (frozen reader starved for a bar via `gWithholdLaneTurn` and the new harness hook `gWithholdRollingService`, the device's main loop running behind the clock edge) | FAIL with the resolver term removed (1 blocked) | PASS |

Default lane gate: 32/32 (after the review below). Native full: 4,007 cases, 0 failed.

**Budgets.**
- First build: the selector code grew `fillPlaybackHolders`, and its `matches` lambda (116 B) was outlined into ITCM.
  The probe image then had padding 776, below the floor (an ITCM symbol diff against `bfd9d94f` named exactly that
  one symbol).
- Fix: the offer moved into the cold member `offerRecordReturnPriorityHolder`.
- Both images are byte-identical in ITCM again: default 392,072 + 1,144 (304 B headroom); succprobe 392,312 + 904
  (64 B).

**Post-takeover continuity.** On the probe image the committed successor after the root window is adopted on time
in every test above. On the production image the window after the root comes from DEC-147-priced positioning, because
the C5.3b successor requester is absent. That dependency is named, not addressed here.

**Arbitration slice independent review (one focused pass), findings and disposition:**

| finding | severity | disposition |
|---|---|---|
| R1: the SourceSuperseded `Idle` guard could strand a frozen build whose boundary passed, holding the single builder until the S4f.19 stall escape | high | fixed: `Idle` only while the build is still the next window of the frozen view serving the playhead; otherwise it is discarded as before. RED `..._a_frozen_build_past_its_boundary_never_holds_the_builder`: the builder was held for 2,272 ticks with the old guard; after the fix, less than 8. |
| R2: the resolver expiry was not limited to the routed slot | medium | fixed: `recordReturnJobAuthorizesFrozenView` answers only on the routed active slot. Elsewhere the Defect-B retirement stands. |
| R5: the yield's discard was not scoped to root work by construction | low | fixed: the yield requires the active build, if any, to be coverage-owned (root work). |
| R6: the F5 test asserted neither a yield nor the takeover | test | fixed: it now requires at least one observed yield and exactly one root handoff. |
| R3: root liveness depends on cost. A speculative root that cannot complete within the frozen bar it is prepared in (one bar, minus the frozen build) is yielded at each frozen handoff and re-raised one bar later, so a root costlier than that never takes over. Its dropped work carries no DEC-147 promise (speculative admission records none), so no miss is counted. | open, owner decision | Not patched: a bound would need a timeout or fallback, which the ruling excludes. On the device a frozen bar completes with 624-704 ticks spare and G_f = 3 grants. The two-bar root's actual grant time is what device qualification measures. Under the retained-role exclusion above, the pre-slice ordering still guarantees an eventual takeover. |
| R4: once the frozen reader is late (a starved bar), the frozen branch is skipped and the next windows come from the root path. | noted | This is pre-slice behavior. The resolver comment was corrected: the next window is adopted from Idle at its own boundary, the frozen route's or the root's. |
| `recordReturnSpeculativeRootOwedAfterHandoff` protects no production work while the yield applies | noted | It still classifies the span-shaped root in the retained-role case and keeps item 1 consistent with the speculative shape. |

**First device qualification of `b0d63d25`, and the fix it drove (2026-11-04).**

*Run 1.*
- Image: succprobe at `b0d63d25`, plus the uncommitted TEMP boot breadcrumbs. HEX sha256 `396f08f9…7f8c`, retained in
  `captures/succprobe-396f08f9/`.
- Upload: the first `teensy-gui` attempt failed ("unable to read your compiled sketch"); the retry succeeded.
- Boot: `boot_reset,crash=0`, `BOOT,load,ok`.
- Gesture: `STIM-LANE-CADENCE-TEMP-01`, track 8 slot 4, the same command as before.
- Capture: `captures/session_20261104_102248.log`.

*Result.*
- The routed slot's post-stop silence fell from about 33 s (61.8-94.9 s on the earlier runs) to about 9 s
  (`none` 232/600/253 across 57-72 s).
- The root becomes current at 59.28 s, on the boundary where frozen bar 8448 is adopted. The yield then fires
  (`frozen_priority_yield`).
- No frozen bar 9216 is built. `win_gap` shows the slot eligible on each turn with every turn denied. The frozen frame
  expires at its end (60.08 s, `ServingExpired`) and the root path takes over.

*Attribution (TEMP device diagnostic).*
- Image: HEX `c0dbbcb2…86e2`, transition-cadenced decision codes, not committed. Capture
  `captures/session_20261104_103529.log`.
- It shows a single `fp_yield_pend`. The yield fired on a **pending committed demand alone**, which holds no builder.
  Identical repeats are deduplicated by the drop trace.
- A pending demand is re-raised between lane turns on the device. Separately, the holder selector served a pending
  committed demand through its retry gate before the frozen offer was reached, so the owed frozen bar got no grain.

*Fix.*
- Only work that holds the builder yields: a qualification, a build, or its frozen request.
- The owed frozen bar's holder offer (`offerOwedFrozenContinuationHolder`) now precedes the selector's pending-demand
  rule.
- F5, explicit: when the speculative target is requested, a committed demand for another interval is retired first,
  never coalesced into it.
- Budget: the `matches` lambda in `fillPlaybackHolders` is now `TRACK_COLD_MEM`, so an outlined copy stays in flash.
  The first build of the fix had outlined it into ITCM (+128 B on both images).

*Host RED.*
- `..._a_re_raised_pending_demand_never_starves_the_frozen_reader`: a one-bar committed demand is re-raised before
  every lane turn while the frozen bar is owed, which is the device's producer shape.
- Before the fix: 10,752 gap ticks. After: no gap, one takeover, notes paired.

*Gates.* Lane 33/33; native full 4,008 cases, 0 failed; ITCM byte-identical (392,072 + 1,144; 392,312 + 904).
