# Task 6.4/6.5 — the rolling demand path's gate row (`DIAG,win_gate`)

Execution packet, written **before** the firmware edit. Authority: task 6.4 ("*the profile … used for the F-7/F-8 device
legs and the AC-8 receipts under a running transport*" — profile/oracle preparation may precede rolling implementation)
and task 6.5's hardware half (whose four claims are **NOT RUN**), plus **F-16** of this change's own device record
(`evidence/2026-10-05-switch-leg-demand/MEASUREMENT.md`: *"the demand paths' refusals are **unobservable on device** — the
outcome is a typed enum returned to its caller (`RollingOutcome::Excluded`, …) with no row naming it, so a device reader
cannot tell 'nothing was owed' from 'the source was refused'. That is exactly the distinction five runs were spent on, and
a row would have settled it in one."*).

Three device sessions have since made the *other* silent families speak — `DIAG,launch_gate` (the launch path) and
`DIAG,snap_gate` (the `Snapshot` owner's selector). Each one converted a five-run inference into one row. This slice adds
the **third and last** of the demand-side families: `Track::serviceRollingPlaybackWindow`, whose ~20 exits all return a
typed `RollingOutcome` with no row — including the exit the device question now rests on, the **unstamped deadline
clock** (`TrackPlaybackWindowBuild.cpp:2397`, `!clock.epoch || !shouldAvoidFullVisualRebuild(...) || loopLengthTicks % span
!= 0`), which is the only explanation left for "the lane dispatched under transport (`win_gap` `running=1`) and produced
no lane work".

## Identity and scope

- **Slice:** task 6.4/6.5's **oracle preparation** — one qualification-only, state-change-bounded diagnostic row naming the
  rolling demand path's exit and the inputs that decided it. Instrumentation only: **no guard, order, admission, capacity
  or fallback changes**, and no task checkoff (6.4 and 6.5 stay open; the device leg that consumes this row is a separate
  authorized step).
- **Status:** specified (this packet) → fixture implemented → native verified → **device NOT RUN** (needs an authorization
  this slice does not take).
- **Owner and production entry → deferred worker → commit/retirement:** `src/main.cpp`'s
  `DeferredIdleOwner::PlaybackViewMaintenance` turn calls `trackManager.getTrack(i).serviceRollingPlaybackWindow(windowClock)`
  for every offer track, off-clock, once per lane turn, with `clockManager.getPlaybackDeadlineClock()`. That is the **only**
  production caller. The row is emitted from inside that call; nothing else in the lane is touched.
- **Primary files:** `src/Track/TrackPlaybackWindowBuild.cpp` (the exits, the cadence state, the exit tokens — this TU
  already carries the `.rodata.str*` progmem carve-out), `src/Utils/DebugSessionCapture.cpp` (the emitter + the Tier-A
  registration), `include/Utils/DebugSessionCapture.h` (the row's declaration and the `SC_WIN_GATE` seam),
  `include/Track.h` (the reporter's declaration), `test/test_runtime_working_set_ownership/test_runtime_working_set_ownership.cpp`
  (the recorder + the fixture), `test/test_capture_ring`, `test/test_capture_line_tier`.
- **Preconditions already present:** the lane's gate (`PLAYBACK_WINDOW_ROLLING_ENABLED`, opened only by
  `teensy41-capture-serial-window-qualification` or native — `include/RuntimePlaybackView.h`), the qualification-row gate
  (`SC_QUALIFICATION_ROWS_ENABLED`), the `#ifndef`-guarded emitter seam pattern, the state-change dedupe pattern and the
  two delivery rules (`serialWriteRoom` direct, ring fallback) all exist and are cited rather than reinvented.
- **External blockers:** the device run that would read the row needs (a) an authorized upload of the qualification image
  and (b) a bench shape whose active slot is demoted (> 16 bars) — the leg's own 24-bar take already produces one.
- **Mutation, postconditions and explicit non-goals:** the observable is *the row*: its exit token, the target's identity,
  the flag set that decided it, and the once-per-state-change cadence. Non-goals: no change to `RollingOutcome` values,
  no new lane behaviour, no `resolveRollingPlaybackWindow` instrumentation (that is the **consumption** path on the
  playback hot path — off-clock only, per the instrumentation rule), no oracle adjustment to fit an observation.
- **Commit boundary:** independently buildable and testable; no temporary compatibility shim. Firmware + host-tooling
  stage → commits on the working branch `feature/windowed-playback-generation`, never `dev`.

## Oracle fixed before behavior edits

`Track::reportPlaybackRollingGateRow(const char* exit, bool reprojectedTick, uint8_t requestResult)` assembles the flags
from the **active slot's own state** (as `reportPlaybackLaunchGateRow` does for its target), so a call site names only the
exit and the two facts only it knows. Wire form:

```
DIAG,win_gate,t=<track>,s=<active slot>,x=<exit>,len=<loopLengthTicks>,ep=,av=,al=,pl=,ed=,od=,pv=,rw=,sv=,rt=,rl=,ow=,dm=,bld=,gr=,rp=,rq=<PlaybackViewRequestResult ordinal>
```

Flag bits: 0 `clock.epoch != 0`, 1 `shouldAvoidFullVisualRebuild`, 2 `loopLengthTicks % span == 0`, 3 `isPlaying`,
4 `isNoteEditActive`, 5 `hasPendingDeferredOverdubStop`, 6 `previewSourceActive`, 7
`hasBoundRecordReturnWindowForPendingJob`, 8 the prewarmed view set's current view is complete **and** published,
9 `publishedSnapshotIsCurrent`, 10 `coverageRoleAvailable`, 11 `ownsCoverageSource`, 12 `pendingViewRequest_.active`,
13 `viewBuild.active()`, 14 outstanding grains on this track (coarse), 15 the tick fell outside the projection
(`effective != clock.tick`, supplied by the call site). `rq` is the request result ordinal for the exit that asked
(`0` where no request was attempted).

| Case | Initial state | Ordered actions | Expected observation |
|---|---|---|---|
| **W1** the epoch term, named | the demoted 24-bar target active and playing (the launch harness's committed state), root current, span-aligned | call `serviceRollingPlaybackWindow({tick, epoch = 0})` | exactly **one** row, `x=win_window_ineligible`, `ep=0`, `av=1`, `al=1`, `len=18432` — the refusal is attributable to the **epoch**, not to the shaping. Repeat the same call → **no** new row (cadence) |
| **W2** the shaping terms of the same exit, distinguished | as W1, stamped clock | 4-bar loop length → `av=0`; then 24 bars + 1 tick → `al=0`; then 24 bars | three rows, each `x=win_window_ineligible`, with `av`/`al` moving exactly as the input did — one exit token cannot hide which term refused |
| **W3** the demand opens | as W1, stamped clock | call with `{tick, epoch = 1}` | `Requested`, and the row says `x=win_requested` with `ep=1`, `av=1`, `al=1`, `pl=1` |
| **W4** the lane's own progress is named, not flooded | as W3 | drive the production lane cadence (`rollingTestTick`) to the episode's end | the rows name `win_waiting`/`win_ready`/`win_serving` as the phase moves, and the **total count stays small** (a state-change bound, not one row per turn) |
| **W5** a refusal *before* this row's family existed | as W1 | `forceSetState(TRACK_STOPPED)` then call | `x=win_track_not_playing` with `pl=0` |

- **Counterexamples that must fail:** removing the reporter from the epoch exit leaves W1 with no row (the defect this
  slice exists for); emitting unconditionally (no dedupe) makes W1's second call produce a second row and W4's count
  explode; naming one exit token for two different deciding terms leaves W2 unable to discriminate.
- **Preserved invariants:** every `RollingOutcome` value, guard order and returned pointer is unchanged; the lane's
  behaviour on a device without the qualification gate is untouched (the row and its state compile out —
  `SC_QUALIFICATION_ROWS_ENABLED`); the row observes state *after* the guards decided and admits nothing.

## Controls and coverage

- **Seams reused:** the `SC_WIN_GATE` `#ifndef` substitution seam (the `SC_LAUNCH_GATE` / `SC_SNAPSHOT_GATE` pattern),
  `assert_s4b_7_a_demoted_target_through_the_turn`, `rollingTestTick`, `TrackCaptureStopTestAccess::runtimeIfAllocated`,
  `nativePlaybackDeadlineClock`, `PlaybackPathRows`.
- **Cadence stays in production:** the dedupe table lives in the reporter (`TrackPlaybackWindowBuild.cpp`), so a
  substituted recorder observes exactly the rows the device would emit — a seam substitutes the transport, not the policy.
- **Production paths exercised:** the real `serviceRollingPlaybackWindow` through its real exits, and the real
  `main.cpp` turn shape (a stamped/unstamped `PlaybackDeadlineClock` per call).
- **Untestable-before-link:** `src/main.cpp` is not native-linkable (the suite's established limit); the row's *call
  site* there is unchanged (the pre-existing `serviceRollingPlaybackWindow(windowClock)`).
- **Bounded drain:** the drive's own tick counts; no sleeps, no unbounded loop.
- **Baseline reuse evidence:** the launch gate's own device record (`evidence/2026-10-05-launch-path-diagnostic`) is the
  precedent for the row's shape, delivery rules and Tier-A need, cited rather than re-derived.

## Verification recipe

- Focused: `pio test -e native -f test_runtime_working_set_ownership`, `-f test_capture_ring`, `-f test_capture_line_tier`.
- Broad: `pio test -e native`.
- Build: `pio run -e teensy41-capture-serial` **and** `-e teensy41-capture-serial-window-qualification` (RAM1 line must
  be re-read; a 4 KB snap names the align edge).
- Contract: `python3 scripts/check_marker_contract.py 'DIAG,win_gate,'` and `--audit`; `git diff --check`;
  `openspec validate windowed-playback-generation --strict`.
- **Device: NOT RUN at the time of writing** — the owner authorized the upload and the registered leg after this
  packet's native verification, and the run's outcome (including a defect it found in this row) is the **Completion
  record** below plus [`DEVICE-RUN.md`](DEVICE-RUN.md).
- Evidence path: `openspec/changes/windowed-playback-generation/evidence/2026-10-05-rolling-gate-row/`.

## Completion record

## HITL test recipe (filled — the marker half of AGENTS.md step 8)

| Field | Fill in |
|---|---|
| Acceptance criterion (one sentence) | The row the device reads must name *which* term of the rolling demand refused, so "nothing was owed" and "the window was refused" stop being the same absence (F-16). |
| Reused stimulus / flow (`stimulus list`, `scripts/hitl/flows/`) | none new: the marker rides the **already-registered** `STIM-WINDOW-CONTENT-01@v1` (`windowed_content_parity`) and its existing driver (`run_windowed_content_parity`, the reused long-loop leg). No canonical stimulus was edited. |
| New stimulus? If yes, variant name (never edit a canonical one) | no. |
| Gestures used (only helpers from `scripts/hitl/actions/`; constants from `control_constants.py`) | unchanged from the registered profile (no new gesture; this slice changes firmware instrumentation only). |
| Expected markers | `DIAG,win_gate,` — Tier-A, **one row per state change** (never per turn), emitted from `src/Utils/DebugSessionCapture.cpp:winGateRow`, called only by `Track::reportPlaybackRollingGateRow` (`src/Track/TrackPlaybackWindowBuild.cpp`). |
| Marker tier test | `test_capture_ring` pins both boundaries (`win_gate` in, `win_gatex` out). `test_capture_line_tier` needs no case: like `win_gap`/`launch_gate`/`snap_gate`, the tag lives in the `.cpp` list only. |
| `check_marker_contract.py` output (EMITTED / TIER-A / FIT) | `OK   DIAG,win_gate,: emitted=src/Utils/DebugSessionCapture.cpp:1482 tier-a-prefix='DIAG,win_gate,' (14/31 bytes)`; `--audit`: `89 Tier-A prefixes, limit 31 bytes, 0 over`. |
| RAM1 impact | instrumentation is **qualification-only** (`SC_QUALIFICATION_ROWS_ENABLED`): `teensy41-capture-serial` padding **1,640 → 1,640** (row compiles out), `-window-qualification` `code 392,232 / padding 984` **byte-identical** to its recorded line. No re-base. |
| Native test that fails without the change | `test_runtime_working_set_ownership`: `test_rolling_gate_rows_name_the_demand_paths_silent_exits_once_per_state_change` (MR-W1/W2/W3 each fail exactly this case). |
| Boot-census / smoke check | **OWED with the device run**: after an authorized flash, `python3 scripts/hitl_boot_census.py` / a short census, expecting the boot banner and the first `#CAP,<micros>,DIAG,win_gate,` row once the transport runs on a demoted active slot. |
| Full scenario entry point | the registered windowed leg: `.venv/bin/python scripts/host_midi_hitl.py run --preset windowed_content_parity --build-env teensy41-capture-serial-window-qualification …` (bounded by `HITL_MANAGED_RUN_TIMEOUT_S`). **NOT RUN in this slice** — needs an authorized upload. |
| Verdict rule | a green read is `win_gate` rows naming the exit **and** `ep`/`av`/`al` on `x=win_window_ineligible`; a demoted active slot under transport with `x=win_requested` (then `win_waiting`/`win_ready`/`win_serving`) is the lane working; **missing** means run `scripts/hitl_absence_check.py` first — `ABSENT-EVIDENCE` only when the ring reports no Tier-A loss, `ABSENT-NOT-EVIDENCE` otherwise. |
| Evidence to record | log tail of the run's capture + the row lines; the same read as the launch gate's device episodes (`evidence/2026-10-05-launch-path-diagnostic`). |

## Completion record

- [x] **Oracle/contract unchanged:** `RollingOutcome`, its exits, guard order and every returned value are
  byte-identical — the only production lines that moved are the report calls themselves; no admission, capacity,
  ordering or fallback behaviour changed.
- [x] **Fixture implemented and discriminating:** `test_rolling_gate_rows_name_the_demand_paths_silent_exits_once_per_state_change`
  (cases W1–W5) in `test_runtime_working_set_ownership`, driving the production entry.
- [x] **Red proofs (each restored byte-identically, sha256 `c47bf2c5c5be4693…` before the cadence fix and
  `f93c1da22fa574e5…` after it; `git diff -- src` empty afterwards):** **MR-W1** (the entry exit goes silent) → *"Expected
  1 Was 0. the exit that refused names itself exactly once"*; **MR-W2** (dedupe removed) → *"Expected 1 Was 2. one row per
  state change, never one per call"*; **MR-W3** (the shaping terms stop being told apart) → *"`av=0`: a loop the
  whole-take policy admits never takes the windowed demand"*; **MR-W4** (every observation bit back in the change key,
  the device's own alternation) → *"Expected 2 Was 4. one row per decision: the role/progress bits must not re-emit the
  same state"*. Each mutation failed **exactly one** case (the new one).
- [x] **Native verification:** focused `test_runtime_working_set_ownership` **144/144** (was 143),
  `test_capture_ring` **16/16** (with the new boundary pin); full `pio test -e native` **2,801 cases / 7 skipped /
  2,794 succeeded / 0 failed**, no ERRORED suite.
- [x] **Build/RAM1:** `teensy41-capture-serial` **SUCCESS** (`variables 96,192 / code 391,576 + padding 1,640` —
  byte-identical) and `teensy41-capture-serial-window-qualification` **SUCCESS**
  (`variables 96,192 / code 392,232 + padding 984` — byte-identical). No re-base.
- [x] **Contract:** `check_marker_contract.py 'DIAG,win_gate,'` OK (14/31 bytes) and `--audit` 0 over;
  `git diff --check` clean; `openspec validate windowed-playback-generation --strict` valid.
- [x] **Device verification: RUN** (authorized 2026-10-05, three episodes: `captures/s64-win-gate-0{3,4,5}`, the
  registered profile through the content-verification pipeline). It **read the answer** and it **found a defect in this
  row**: the demand dies at the entry because the leg's committed loop is **not span-aligned** (`len=19200`, `al=0`)
  while demoted, with `ep=1` in every row (the clock-stamp hypothesis refuted) and `x=win_not_servable, rt=0` naming the
  second gate where the loop *is* aligned; and the change key alternated A↔B through two observation bits (the traps
  list's "dedupe that alternates forever") — 809 rows in one leg with up to 20 identical lines per state, corrected to
  **57** on the same leg with no alternation. The profile's own oracle was replayed over the run
  (`captures/s64-win-gate-05-verify`). Full record: [`DEVICE-RUN.md`](DEVICE-RUN.md).
- [ ] **Still unmeasured:** F-7, F-8, AC-8 and task 6.5's four hardware claims — the lane was never given a *demand* in
  these runs, so nothing about its service, timing or memory was measured. A leg whose committed length is a whole
  number of two-bar spans is the next shape (a *leg-shape* decision), and the root-currency question stays the owner
  decision already on record.
- [x] **Two defects found and fixed beside it (recorded, not glossed):** (a) `isTierATextLine` carried a duplicated
  `DIAG,launch_gate,` clause *after* its `return` — unreachable, left by the `snap_gate` slice; removed, classification
  unchanged, and both capture suites pin the family's boundaries. (b) The row's own **change key** was wrong, and only the
  device could show it: see the device bullet above (809 → 57 rows).
- [x] **Durable docs:** `tasks.md` (6.4/6.5 notes), `CURRENT_WORK.md` § Now implementing, `PROJECT_STATE.md`,
  this directory's `EVIDENCE.md` + `win-gate-verification.json`; committed on the working branch
  `feature/windowed-playback-generation`.
- [x] **Task checkoff:** **none.** 6.4 and 6.5 stay open — this slice supplies the instrument their device legs need,
  not the legs.
