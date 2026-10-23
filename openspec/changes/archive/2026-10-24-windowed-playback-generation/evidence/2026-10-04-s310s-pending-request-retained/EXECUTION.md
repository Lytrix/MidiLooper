# Task 3.10 (increment 24) — the retained pending preparation request: execution packet

**Status: packet only. No production edit made, no run performed, every completion field below reads NOT RUN.**

Baseline: `4a7cef70` (increments 22-23 landed; working tree clean). Branch
`feature/windowed-playback-generation`. Work-order source: task 3.10's own owed list, the
`deadline-clock-identity` and `timing-envelope-qualification` packets (owed item 2), and
`DEC-108` §13(b)/(c).

## Identity and scope

**Authorized by an existing decision, not a new one.** `DEC-108` §13(c) — clock revalidation:
"a changed clock invalidates the *promise* to finish in time, not the content — **the request
is retained**, the valid audible source continues, the miss is reported, and an inline rebuild is
never used to compensate." `DEC-108` §13(b) — fairness: the selection cursor advances even when a
holder cannot progress. `DEC-108` §13(a) — "superseded **preparation requests** are coalesced
(never accepted content), useful preparation is not cancelled for newer edits". Spec
*One off-clock owner acquires, replaces and publishes windows*, scenario *An unprepared window keeps
the request pending rather than taking it inline* ("the request stays pending … the owner prepares it
later with no further user gesture"), and §"One pass late is the only stale authority" ("a failed
revalidation SHALL retain the request and continue the valid audible source").

**The gap this slice closes.** Both landed packets record the same item: `TrackInterferenceBudgetExceeded`
is a *named refusal* and "becomes a promise only when a caller actually keeps the pending request and
its original deadline. No such caller or pending-request store exists yet, so that half is **not
implemented and not claimed**." Until it exists, every refused request is **dropped**, and enabling
enforcement would silently discard work instead of deferring it — a soundness hole in the admission
that must close before activation is even discussable.

In scope:

1. A **per-slot pending preparation request** retained on that slot's `LoopPlaybackRuntime`
   (extern storage: no DTCM, no RAM1 `variables`), holding the demand rather than the events:
   the source identity the request was made against (revision, generation, snapshot identity,
   origin), the coverage interval, the absolute deadline (`deadlineTick`, `clockEpoch`, loop length)
   and the request's bounded cost (`PlaybackViewAdmittedWork`).
2. **Retention classes, pinned per refusal name** — which refusals mean "not now, the obligation
   stands" and which mean "nothing is owed".
3. **Coalescing**: one pending request per slot; the **oldest outstanding deadline** is preserved and
   the **newest identity** is kept, so a newer window may satisfy the older obligation.
4. **Invalidation without erasure**: a clock-epoch change or a geometry change invalidates the
   *positional deadline* while the obligation survives, and the invalidation is reported by name.
5. **Discharge**: admission (or the slot's own clear/load/generation change) clears the record;
   identity is generation-qualified, so returning to a slot never revives an earlier request.
6. **The re-attempt driver**: a track whose slot holds a pending request and has no build in flight
   re-issues it on **its own lane turn**, with no further user gesture — revalidating source currency
   and the clock, and re-deriving the deadline from the phase at that instant.

Non-goals (each stays owed, none is claimed here):

- **A production requester of committed windows.** No production code calls
  `requestPlaybackWindow`/`requestPreviewWindow` today (only the ownership suite does), so in
  production this store is still never written. The re-attempt driver is production code; the
  *producer* of a request is not. That is blocker 1 of the timing-envelope qualification and it is
  **not** solved here.
- **The permitted service-gap limit and the overrun policy** (blocker 3) — a policy decision.
- **Enabling `sInterferenceBudgetEnforced`**, any capacity reduction, any RAM1 re-base, any change to
  the declared contract constants.
- **The device leg** and any timing-envelope claim.
- A second scheduling owner: the re-attempt rides the existing `PlaybackViewMaintenance` turn
  (`src/main.cpp`), which already owns the cursor and the offer round-robin.

## Transition oracle (fixed before edits)

Each row names the observable and the mutation that must make its fixture fail. Restores must be
byte-identical and their pre-restore hashes recorded.

| # | Transition | Required observable | Mutation that must fail it |
|---|---|---|---|
| O1 | A "not now" refusal retains | After an enforced `TrackInterferenceBudgetExceeded`, the slot reports a pending request carrying the **original** absolute deadline and identity | remove the retention → the accessor reports nothing |
| O2 | A "nothing owed" refusal retains nothing | `NoRuntime`, `NotServable`, `SourceNotCurrent`, `WindowExceedsRetainedCapacity` leave **no** pending record | retain unconditionally → those cases report a record |
| O3 | Retention is not a claim of work | A retained request owes **no** grains, so `playbackViewOutstandingGrains(slot)` is unchanged and a sibling's admission is not charged for it | count the pending record as outstanding work → the sibling's admission refuses |
| O4 | Coalescing keeps the oldest deadline, newest identity | Two requests on one slot whose deadlines differ in order: one record, deadline = the **earlier**, identity = the **later** | overwrite the deadline with the newer → the earlier deadline is lost |
| O5 | An epoch change invalidates the deadline, not the obligation | After a reset/seek that changes the epoch, the record **survives** with an unqualified deadline and the invalidation is named | clear the record on epoch change → the obligation disappears |
| O6 | A geometry change behaves as O5 | After the loop length changes, the record survives, positional deadline unqualified, named | as O5 |
| O7 | Admission discharges | The record for that slot is gone once the same request is admitted | remove the discharge → the record survives a completed admission |
| O8 | The re-attempt needs no second gesture | After an interference refusal, clearing the interference and driving the lane turn admits the window (a build in flight / a grain served) **without** another request call | remove the re-attempt → the lane turn stays `Idle` |
| O9 | Generation-qualified identity | A slot cleared or reloaded (generation moved) does not revive an earlier pending request | compare by slot only → the stale request revives |
| O10 | No new DTCM state | The capture build's approved RAM1 line is unchanged; `sizeof(LoopPlaybackRuntime)` moves only by a compiler-observed measurement | (build-level: a DTCM member is the failing state) |

Two readings the packet **decides rather than discovers**, each stated so a reviewer can dispute the
reading without disputing the citation:

- **`BuildInFlight` / `SuccessorAlreadyPrepared` retain.** The runtime already owes work for that
  slot; the new demand coalesces into it under O4 rather than being dropped. The alternative
  ("the caller retries later") is exactly the mechanism this slice adds, so refusing by name and
  forgetting would preserve the defect.
- **`AdmissionRefused` does not retain.** It is the build's own primitive refusing *this* source
  shape; retaining it would re-drive a request that cannot succeed without new information. It is
  reported by name and the caller may ask again.

## Controllable production-path fixtures (established before the behaviour change)

The ownership suite already drives the real request → build → publication path with controlled
ticks and no sleeps (`test_runtime_working_set_ownership`); the fixtures extend that harness rather
than a copy. Note the increment-18 coupling defect: a fixture that drives the **lane turn** samples
the process-wide service-gap series, so lane-turn fixtures are ordered to keep the gap fixture
unaffected, or drive the runtime's own primitives where the turn is not the subject.

1. `test_refused_request_is_retained_with_its_original_deadline` (O1, O2, O7).
2. `test_pending_request_owes_no_grains_and_does_not_charge_a_sibling` (O3).
3. `test_coalesced_requests_keep_the_oldest_deadline_and_the_newest_identity` (O4).
4. `test_clock_epoch_and_geometry_invalidate_the_deadline_not_the_obligation` (O5, O6).
5. `test_a_pending_request_is_re_attempted_on_its_own_lane_turn` (O8).
6. `test_a_generation_change_does_not_revive_a_pending_request` (O9).

## Verification recipe

- `pio test -e native -f test_runtime_working_set_ownership` and `-f test_playback_prewarm`
  (focused, expected to gain the six fixtures).
- `pio test -e native` (full suite; the current baseline at `4a7cef70` is 2,727 cases / 2,720
  succeeded / 7 skipped / 0 failed).
- `pio run -e teensy41-capture-serial`; the approved RAM1 line must hold
  (`variables 96,192 / code + padding`, floor 840) unless a re-base is measured **and** justified.
- `git diff --check`; `openspec validate windowed-playback-generation --strict`.
- Each mutation restored byte-identically; the owner TU hash recorded before restore.
- **Device: NOT RUN and not claimed.** No upload, no HITL, no timing-envelope claim. Enforcement
  stays off, so no production behaviour changes.

## Completion record (2026-10-04 — native verified; **O8 deferred**, recorded below)

| Field | Value |
|---|---|
| Scope pinned | DONE (above), with two recorded additions (below) |
| Oracle pinned | DONE (O1-O10); O8 **deferred**, O1/O2/O4/O5/O6/O7/O9/O10 proved |
| Fixtures established before the change | DONE - five new fixtures + the retention harness in `test_runtime_working_set_ownership` |
| Focused suite | 89/89 (was 84/84) |
| Full native suite | `pio test -e native`: 2,732 cases / 2,725 succeeded / 7 skipped / **0 failed** |
| Capture build / RAM1 line | `pio run -e teensy41-capture-serial` **SUCCESS**; RAM1 `variables 96,192` **unchanged** (approved line, no re-base), code 390,408 -> 390,472, padding 2,808 -> 2,744, 1,904 B above the floor |
| Mutations red-proved and restored | **M120-M126**, all restored byte-identically (hashes below) |
| Size pins re-based (measured) | target `sizeof(LoopPlaybackRuntime)` 6,680 -> **6,792** (+112 B); host pin (now live) **8,624 -> 8,744** (+120 B) |
| Docs closed out (`tasks.md`, `PROJECT_STATE.md`, `CURRENT_WORK.md`) | DONE |
| Device / HITL | **NOT RUN** (out of scope); image `89e4d0c1f3080ff8c8ab9c05e93d601888685ef1452e93d2ce7231db155d8d9c` built, **not uploaded** |
| `git diff --check` / `openspec validate --strict` | clean / valid |

### What landed
`PlaybackViewPendingRequest` + `PlaybackViewPendingReason` (`include/TrackPlaybackRuntime.h`), the per-slot record on
`LoopPlaybackRuntime`, `Track::retainRefusedPlaybackViewRequest` / `Track::pendingViewRequest` /
`Track::pendingViewRequestInvalidation` (`Track.h`, `src/Track/TrackPlaybackWindowBuild.cpp`) and
`LoopPlaybackRuntime::retainViewPendingRequest` (`src/Track/TrackPlaybackRuntime.cpp`). Retention fires at the four
"not now" refusals (`BuildInFlight`, `SuccessorAlreadyPrepared`, `TrackInterferenceBudgetExceeded`,
`CannotCompleteBeforeHandoff`); admission discharges the demand. **No production behaviour changes**: no production
caller requests a window, the demand is invisible to the per-track budget, the interference switch stays off, and
nothing in the publication path reads the record.

### Recorded deviations, corrections and defects (mine, not smoothed over)
1. **O8 (the re-attempt) is NOT implemented and is not claimed.** The retained demand can be *re-issued* only by a
   caller that owns a gather buffer which outlives the build (`SessionMidiEventVec`, exactly as the O2 fixture and the
   `windowSourceScratch` note record); the lane turn owns no such buffer, and choosing one (per-slot persistent buffer
   vs. a bounded transient) is a RAM-shape decision. That is **increment 25**, and it is the same gap the
   timing-envelope qualification names as blocker 1. Everything else in this packet's oracle is proved.
2. **The packet's retention table did not enumerate two refusals**: `ContractUndeclared` and `AdmissionRefused` do
   **not** retain (nothing new can be learned by re-driving the same request without new information). Recorded as an
   addition, not as an original row.
3. **A reading added while implementing, and pinned by the O4 fixture:** a demand retained with **no qualified stamp**
   never overwrites a qualified one it coalesces with -- an unqualified re-attempt must not un-promise a positioned
   demand.
4. **Defect found by the O4 fixture, mine, fixed:** the first version of `retainViewPendingRequest` read the retained
   record through an alias **after** replacing that same record, so the coalesced promise was handed the *newest*
   attempt's origin stamp while keeping the *oldest* deadline -- a traversal that never existed. Every retained field
   is now read into a local before the record is replaced, and the reason is in the source comment.
5. **A wrong fixture precondition of mine, caught by running it:** `firstDeadline < admittedTick + 1` asserted nothing
   (it compared a deadline against a tick). Replaced by the real precondition -- the two attempts must promise
   *different* deadlines and the first must be the earlier one -- so the row cannot pass by accident.
6. **The host ABI size pin was unreachable dead code AND stale.** It sat in the `#else` of a branch whose `#if`
   already required `__IMXRT1062__`, so no configuration compiled it; the "the host pin is unchanged at 6,544 B"
   claims in earlier records therefore described an assertion no compiler evaluated. Measured on the clean tree at the
   previous commit, the host object is **8,624 B**, not 6,544. The pin is live (native) from this slice, re-based on
   measurement, and that correction is recorded where the pin is.
7. **Mutation-run cascade, recorded:** under **M120** the first failing fixture aborts before its own flag-restore
   line, so the interference flag stays enabled for later fixtures in that binary and the helper's
   "long enough for the declared lead" assertion fails downstream. That is a harness property of the mutation run,
   not a product finding; the *primary* failure M120 must produce (line 6868, and fixture 1) is the one cited.

### Mutations (guarded replace, unique anchor asserted, byte-identical restores)
Restore hashes: `src/Track/TrackPlaybackWindowBuild.cpp` **e98595129187ceee**,
`src/Track/TrackPlaybackRuntime.cpp` **35eec9683a0fdfd2**, `include/TrackPlaybackRuntime.h` **2e50b234a0cce56c**. Every
case patched, ran the focused suite, was restored from a snapshot and re-hashed to the pre-mutation value.

- **M120** (O1) retention removed at the budget refusal -> *"a refused request leaves a pending demand behind"* (6868)
  and 5 more demand fixtures.
- **M121** (O2) retention on the not-servable path -> *"and no demand is retained for it"* (6962).
- **M122** (O3) the budget counts a pending demand as outstanding work -> *"Expected 0 Was 164. the retained demand
  owes no grain, so the budget does not count it"* (6990), plus the legacy eight-track fixture at 6824.
- **M123** (O4) coalescing overwrites the retained deadline -> *"Expected 39952 Was 41952. coalescing keeps the oldest
  outstanding deadline, never the newest"* (7035).
- **M124** (O5/O6) the invalidation reading disabled -> *"Expected 5 Was 0. a changed epoch voids the positional
  deadline"* (7075).
- **M125** (O7) the admission no longer discharges -> *"an admitted demand is discharged whole: no retained deadline
  survives it"* (6948).
- **M126** (O9) the reader drops the generation qualification -> *"a returning slot must not revive a demand from an
  operation it no longer runs"* (7113).

### Still owed by this task (unchanged by this slice)
The re-attempt driver (**O8**, increment 25, reason above); a production requester of committed windows
(qualification blocker 1); the permitted service-gap/overrun policy (blocker 3); enforcement activation (awaits
`Delta-max`); the `H x K` envelope; the systematic sweep; the device leg. **Task 3.10 stays open** and its checkbox is
untouched.
