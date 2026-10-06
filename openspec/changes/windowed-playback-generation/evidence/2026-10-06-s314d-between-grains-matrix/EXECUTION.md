# Task 3.14 (increment d) — the **between-grains matrix**: repeated undo/redo, a slot/track switch and a stale completion, each landing while real preparation grains are paused

Execution packet for the next increment of slice **3.14** (*"Integrate undo/redo audible handoff"*), written
**before** the fixture edit. Authority: `tasks.md` item **3.14** (its verify clause: *"pause between real preparation
grains for repeated undo/redo, track/slot switching and stale completion. Assert exact MIDI output, ledger state,
retained-source lifetime and eventual release."*), `G2-EXECUTION-PACKET.md` §"Increment 3.14" (and §"Slice 3.7a" for the
serving path this now has), **DEC-108** §13(d)/(g), **DEC-114**, and the three landed 3.14 increments
(`s314`, `s314b`, `s314c`), whose *"still owed, unchanged"* line names exactly this matrix and records that it **needs
3.7's serving path** — which 3.7a's I1/I2/I5 have since delivered.

## Identity and scope

- **Slice:** 3.14 / increment **d** (the between-grains matrix). **DEC-114's direction is unchanged**; no fallback is
  removed (3.7b) and no window is re-wired.
- **Status:** specified. Expected to be **test-only** — the behaviour is already implemented (3.14a/b/c); a defect found
  here would be fixed at its owner and red-proved, not tolerated.
- **Owner and production chain:** `TrackUndo::undoForLoop` / `redoForLoop` (the gesture) → `setEditPassState`'s revision
  bump + `refreshPlaybackAfterEditPassStateChange`'s reconcile-then-refresh (the currency and the closures) →
  `Track::stepPlaybackViewMaintenance` (the deferred grain turns, one declared grain each) →
  `Track::publishCompletedPlaybackWindow` (the publication) → `Track::playbackWindowReadinessForSlot` (the named
  answer).
- **Primary file:** `test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp` (the 3.14 fixture
  `test_undo_edit_batch_settles_its_own_soundings_and_never_blankets_the_slot` is **extended**, as 3.14b/3.14c did —
  same suite, same helpers: `prepareWindowOwnerSlot`, `publishBarSpacedRootForSlot`, `pushUndoEntry`,
  `MidiSendCapture`, `TrackCaptureStopTestAccess`).
- **Preconditions already present:** 3.14a/b/c landed; the window lane's production serving path with a
  **controllable pause** (`hasPendingPlaybackViewMaintenance` / one-grain `stepPlaybackViewMaintenance`); the 3.11
  transition table (`resolvePlaybackViewTransition`); 3.6a's one release predicate
  (`tryReleaseRetainedPlaybackSource`); the native MIDI sink (`MidiSendCapture`).
- **External blockers:** none. No device leg (the lane has no row family on the shipping env) and no upload.
- **Non-goals (explicit):** no refresh-requester at the gesture edge (3.14c measured that its owner is 4.1's current
  root, then 3.7's consumer); **no displayed provenance** (per
  [`DISPLAY-CONSUMER-RECONCILIATION.md`](../../DISPLAY-CONSUMER-RECONCILIATION.md) that is the display's, owned by
  `runtime-working-set-ownership` S4e) and no preview-session undo/redo (3.13/2.12); no timing claim, no
  `ExecutionBounds.md` row change, no device claim, no fallback removal (3.7b).
- **Commit boundary:** one commit on the working branch `feature/windowed-playback-generation` — fixture, evidence and
  doc updates together; no PR without an explicit request.

## Oracle fixed before the fixture edit

The oracle is **what the wire and the owner's own vocabulary say after each step** — the gesture's exact output, the
obligations it settled, and the *named* answer the lane gives about the window the paused build was preparing. Every case
drives the real entries; nothing is published, adopted or settled by the fixture itself.

**Two mechanisms decide these cases, and they differ by gesture** (read from the code before the fixture, then
**corrected by the measurement** — see the evidence's §2 and §7):

* a **committed pass** advances `playbackRevision` and nothing else (`Track::setEditPassState`'s bump is the edit-pass
  case; a commit bumps the revision through the same currency) — so a paused build still completes and publishes, at the
  revision it **froze**;
* an **undo** additionally runs `refreshPlaybackAfterEditPassStateChange` → `Track::invalidatePlaybackMergedMidiEvents` →
  `bumpPlaybackGeneration()`, and the **playback generation is the operation identity**: the build recorded
  `viewBuildOperationGeneration` when it began (`src/Track/TrackPlaybackWindowBuild.cpp:3169`). **Corrected after the
  measurement:** what actually stops the build is the invalidation's own **slot release**, which discards the paused
  preparation (`hasPendingPlaybackViewMaintenance()` and `buildOwed` are false immediately after the gesture); the
  completion guard at `:4578` is the **second** line of defence, reached only by a completion already past the discard.
  The case therefore asserts the **discard**, the absence of any publication and the named owner — not a late refusal.

| Case | Initial state | Ordered actions (grains are the real deferred turns) | Expected observation | Counterexample that must fail |
|---|---|---|---|---|
| **W-3.14d-1** a **committed pass** lands between real grains: the build completes **stale, and the lag is named** | windowed slot (`kWindowOwnerLoopTicks`) with committed content, a **current** published root, a prewarmed runtime, **no window served yet** | (a) `requestPlaybackWindow` → `Admitted`; (b) serve **one** grain, then pause; (c) `commitRecordPassIntoTrackSlot` (a real production commit); (d) read the wire, the revision and the lane's role state; (e) resume the grains; (f) read the published identity, the readiness and the holder's name | (a) `Admitted`, build owed; (b) `GrainServed`, build still unfinished; (c) the commit emits **nothing** on the wire; (d) the loop's revision advanced and the build is still owed with its identity intact; (e) a window **is** published, carrying the revision the build **froze** (the pre-commit one) — measured, not assumed; nothing is emitted by the grains or the publication; (f) readiness is **`OwesCurrency`** and its holder names `window_rebuild`: the lag is *named*, never reported `Ready` | a completion that reports `Ready` for a revision the loop has left (the lag served as current), a commit that emits MIDI, or a build that vanishes at the commit |
| **W-3.14d-2** a real **undo** lands between real grains: the build is **discarded** and nothing stale exists | same setup **plus** an active edit batch that **added** note 60, a sounding on 60 and an unrelated sounding on 64 | (a) `requestPlaybackWindow` → `Admitted`; (b) one grain, then pause; (c) `TrackUndo::undoForLoop` on the batch (the batch is disabled); (d) read the wire, the ledger, the revision, the generation and the lane's role state; (e) offer the remaining turns; (f) read the published window and the readiness **and its holder** | (a) `Admitted`, build owed; (b) `GrainServed`, still unfinished; (c) **exactly one** NoteOff, for 60 — the batch's own identity, with 64 untouched and **no** blanket silence; (d) 60 discharged, the revision advanced, the generation moved (+1), and the paused build is **discarded**: `hasPendingPlaybackViewMaintenance()` and `buildOwed` are false; (e) no turn emits on the wire; (f) **no** window is published and readiness is **`OwesSource`** with holder **`published_source`** — the lagging root is *named*, never served | a gesture that leaves the discarded build's turn still owed (`pending` true afterwards), a stale window published afterwards, a closure that fires only after the grains resume, a second NoteOff for the untouched 64, or a readiness answer of `Ready` |
| **W-3.14d-3** repeated undo/redo with grains paused between gestures | same setup; a third sounding (62) that no gesture touches | (a) undo; (b) serve one grain; (c) redo; (d) serve one grain; (e) undo again; (f) resume to completion | each gesture: exactly its own closures (one NoteOff for a removal; **nothing** for the re-establishing redo), the untouched sounding 62 alive throughout with no early NoteOff and no retrigger, the revision advancing once per gesture, the ledger's obligations consistent with the content each time, and no window ever torn or published stale by a later grain | a redo that closes or retriggers an unchanged sounding, a ledger cleared by a refresh (the second half 3.14a found), or a late completion that still publishes |
| **W-3.14d-4** a slot/track switch between grains | slot 0 of one track with a paused build; a second track whose own slot carries its own soundings | (a) pause a build on slot 0 after one grain; (b) switch the selected slot and then the selected track (the production selection route); (c) read the wire and both slots' ledgers; (d) resume the grains; (e) read the published window | (a) build owed; (b) **nothing** is emitted by the switch and the worker keeps its **operation identity** (3.11's row: a switch continues the operation, it does not release the candidate and does not bump the generation); (c) both slots' soundings are untouched — no silencing anywhere and no cross-slot closure; (d)/(e) the build **completes and publishes to its own slot** at the revision it froze, and the newly selected track's slot has **no window** on that account | a switch that releases or retargets the worker, a closure emitted for the other slot's sounding, or a completed build published onto a slot that did not build it |
| **W-3.14d-5** retained-source lifetime across the gestures | the W-1 shape **plus** a retained source admitted for the slot's revision, with the window serving from it | (a) admit the retained source; (b) request and serve a window; (c) commit a further pass and serve a grain; (d) ask the release predicate; (e) read the retained revision the readers answer; (f) adopt a successor window for the new revision and ask once more | (a) `Admitted`; (b) a window is served and the retained revision is the one the readers answer (`servingRetainedRevisionForSlot`); (c) the gesture does **not** re-aim or drop the source; (d) release is **refused** while that window serves it; (e) the readers still answer the revision the source **holds**, not the loop's new one; (f) `tryReleaseRetainedPlaybackSource` answers true **only** once a newer window is served, and `hasRetainedPlaybackSourceForSlot` then answers false | a gesture that releases the source out from under a served window, a release that succeeds while the served window still reconstructs from it, or a source silently re-aimed at the new revision |

**Preserved invariants (asserted, not assumed):** no lifecycle transition tears the served window; the settlement runs
**before** the refresh and **nothing is silenced** on any of these paths; the gesture edge advances the currency and
settles the closures while enumerating nothing; publishing a window emits **no** MIDI; and after a late completion the
lane owes its own work **once**, not forever.

## Controls and coverage

- **Reused seams, no new test-only seam:** the production request/serve/publish entries, the production undo/redo
  entries, the real deferred grain turns, the native MIDI sink, and 3.6a's release predicate.
- **Negative controls inside the cases:** the untouched soundings (64 in W-2, 62 in W-3) are the control for "no blanket
  silence and no cross-identity closure"; the second track's own slot in W-4 is the control for cross-slot publication
  and cross-slot closure; the refused release in W-5 (d)/(e) is the control for the release predicate's own clause.
- **Untestable before link:** none — the whole matrix is native. The gesture edge's *requester* is deliberately not
  exercised (3.14c: its owner is 4.1's current root, then 3.7's consumer), and the request refusals reached here are
  **precondition reads**, never this increment's claim.
- **Boundedness:** ≤ 8 grains per case, one gesture per step, one wire capture per observation, no allocation added by
  the fixture beyond the suite's existing scratch.
- **Baseline:** focused suite green at `170/170` before this increment (I8's recorded count on this tree).

## Red-proofs (planned; each restored byte-identically and sha256-checked)

| ID | Mutation | Must fail |
|---|---|---|
| **RP-D1** | `playbackWindowReadinessForSlot` stops naming a lagging published window (`Ready` for any complete window) | W-3.14d-1 (f) — the lag is named, never reported `Ready` |
| **RP-D2c** | `Track::invalidatePlaybackMergedMidiEvents`'s slot release dropped, so a gesture no longer discards a paused preparation | W-3.14d-2 (d) — the discarded build owes no turn afterwards |
| **RP-D2a / RP-D2b** (planned form, **non-red-proof**) | `Track::publishCompletedPlaybackWindow`'s moved-identity guard **inverted**, and separately **dropped** | **not this fixture**: the discard happens first, so the guard is only reachable by a completion already past it; the inverted form fails W-1's *publication* and 3.11's late-completion fixture instead. Recorded in the evidence with its mechanism, not reworded |
| **RP-D3** | `resolvePlaybackViewTransition`'s `SlotSwitch` row releases the candidate (a switch tears the worker down) | W-3.14d-4 (d)/(e) — the build completes into **its own** slot after the switch |
| **RP-D4** | 3.6a's release predicate drops its "a served window keeps its source retained" clause | W-3.14d-5 (d) — release refused while the served window reconstructs from the source |
| **RP-D5** | the edit-batch site stops settling identities (silence removal alone — the same **class** as 3.14c's M47, re-run because this fixture is what a future reader will use) | W-3.14d-2 (c) / W-3.14d-3 — exactly one closure, for the batch's own identity |

A mutation that does not fail its case is recorded as a non-red-proof with its mechanism named, never reworded. Where a
mutation overlaps an earlier slice's, that is stated in the evidence rather than presented as new.

## Verification recipe

- Focused: `pio test -e native -f test_runtime_working_set_ownership` (baseline **173/173** on this tree).
- Broad: `pio test -e native` (baseline **2,852 cases / 7 skipped / 2,845 succeeded / 0 failed**).
- Build/RAM1: `pio run -e teensy41-capture-serial` through the RAM1 budget guard; the approved line is
  `variables 96,192` / `code 392,248 + padding 968` (floor 840). A test-only increment must leave it **byte-identical**;
  if a defect fix changes production code, the line is re-measured and any re-base is named.
- Hygiene: `openspec validate windowed-playback-generation --strict`, `git diff --check`,
  `git diff --stat -- src include` (**empty** for a test-only increment).
- Device: **NOT RUN** (nothing device-visible is claimed).
- Evidence path: `evidence/2026-10-06-s314d-between-grains-matrix/`.

## Completion record

| Field | Value |
|---|---|
| Oracle/contract unchanged | **YES** — the task's own verify clause, the 3.11 transition table and 3.6a's release predicate are cited, none weakened; **one planned expectation was corrected by measurement** (the undo **discards** the paused build rather than letting it complete late) and both the packet and the evidence carry the correction |
| Fixture implemented and discriminating assertion demonstrated | **YES** — `test_between_grains_gestures_keep_the_wire_exact_and_the_window_named`, four cases, registered in `test_runtime_working_set_ownership` |
| Native verification | **PASSED** — focused **174/174** (baseline 173/173); full native **2,853 / 7 skipped / 2,846 succeeded / 0 failed** (baseline 2,852/7/2,845) |
| Build/RAM1 verification | **PASSED** — capture build SUCCESS with the RAM1 line **byte-identical** (`variables 96,192`; `code 392,248 + padding 968`; `locals 34,880`; floor 840) |
| Device verification | **NOT RUN** (declared out of scope) |
| Required reviews and durable docs | tasks.md 3.14 entry, `G2-EXECUTION-PACKET.md` §Increment 3.14, `RECONCILIATION.md` §3, `CURRENT_WORK.md` and `PROJECT_STATE.md` updated with this increment |
| Task IDs eligible for checkoff; stage gates still open | **no checkoff** — 3.14's verify clause's *matrix* half is now discharged, but the task stays open on the refresh-requester (4.1 → 3.7), **displayed provenance** (RWO S4e) and preview-session undo/redo (3.13/2.12); 3.7b's gates are unchanged |
