# The rolling demand path's gate row: the last silent exit family of the demand side now names itself

Evidence for `EXECUTION.md` in this directory. Firmware instrumentation, one new qualification-only row family
(`DIAG,win_gate`), one native fixture with three red proofs — and **no device run** (the flash is owed and would need an
authorization this slice does not take).

## 1. Why this slice, and why now

Task 6.4's remaining work is a device leg, and every device leg this change has run has ended the same way: the lane was
**dispatched** (`DIAG,win_gap` with `running=1`) and produced no lane work, while the capture could not say **why**. Three
sessions have since made one silent family each speak:

| family | row | what it settled |
|---|---|---|
| the launch path | `DIAG,launch_gate` | *five runs of inference*: the launch asked for its source and the source publication never became current |
| the `Snapshot` owner's selector | `DIAG,snap_gate` | the active-loop preference starved a pending non-active slot (then fixed, with a red-proved native fixture) |
| **the rolling demand path** | **`DIAG,win_gate`** (this slice) | **still open**: `Track::serviceRollingPlaybackWindow`'s ~20 exits all return a typed `RollingOutcome` to their caller with **no row** |

That last one is **F-16** of this change's own device record (`evidence/2026-10-05-switch-leg-demand/MEASUREMENT.md`):
*"the demand paths' refusals are unobservable on device — the outcome is a typed enum returned to its caller
(`RollingOutcome::Excluded`, …) with no row naming it, so a device reader cannot tell 'nothing was owed' from 'the source
was refused'. That is exactly the distinction five runs were spent on, and a row would have settled it in one."* The
launch half of F-16 is discharged; this is the rolling half, and it is the exit the four hardware claims of task 6.5 are
blocked behind: the demand's **entry** decision (`!clock.epoch || !shouldAvoidFullVisualRebuild(...) || loopLengthTicks %
span != 0`, `TrackPlaybackWindowBuild.cpp:2397`) is where an unstamped deadline clock — the only remaining explanation
for "dispatched, and owed nothing" — would be named.

## 2. The instrument (qualification-only, on state change, active-slot identity)

```
DIAG,win_gate,t=<track>,s=<active slot>,x=<exit>,len=<loopLengthTicks>,ep=,av=,al=,pl=,ed=,od=,pv=,rw=,sv=,rt=,rl=,
              ow=,dm=,bld=,gr=,rp=,rq=<PlaybackViewRequestResult ordinal>
```

- assembled by `Track::reportPlaybackRollingGateRow` from the **active slot's own state**, called by **every** exit of
  `Track::serviceRollingPlaybackWindow` — the production demand turn `src/main.cpp` drives once per lane rotation for
  every offer track (`DeferredIdleOwner::PlaybackViewMaintenance`), i.e. **off-clock**;
- **one row per state change**, never per turn: the change key is (exit token, active slot, the length the deciding terms
  read, the request's ordinal, the deciding flag bits), compared against the last emitted row *for that track*, so a
  track that sits on one exit for hundreds of rotations costs one row while every move is still named. Bit 14
  (outstanding grains) is **printed as context but excluded from the key** — it is a progress counter, and keying on it
  would turn one state into a row per grain (the flood failure mode the earlier rows recorded);
- `x` names this call's exit: `win_no_runtime`, `win_track_not_playing`, `win_reprojection_blocked`,
  `win_window_ineligible`, `win_frame_invalidated`, `win_serving`, `win_blocked`, `win_blocked_by_request`, `win_ready`,
  `win_waiting`, `win_not_servable`, `win_busy`, `win_reprojected_tick`, `win_boundary_rejected`, `win_request_refused`,
  `win_request_dropped`, `win_requested`;
- the three terms of the **entry** exit are told apart on the row itself (`ep` the clock's own stamp, `av` the demotion
  bound, `al` span alignment), because one token for three guards hides exactly the discrimination the device question
  needs; `rq` carries the request's own ordinal (`DeferredSourceQualification` = 12 on the admitting exit, `0` where no
  request was attempted);
- **qualification-only by build**: the row, its table and its flags exist only where `SC_QUALIFICATION_ROWS_ENABLED` is
  open (a capture build that also opens the rolling lane's gate, and native). `teensy41-capture-serial` compiles the call
  to `((void)0)`;
- **it observes only**: no guard, order, admission, capacity, fallback or return value changed.

## 3. The row now speaks — measured on the production entry

The temporary probe's own reading of the fixture's whole drive (removed before the commit; the row list is the
measurement, the assertion in the suite is what pins it):

```
WIN_GATE rows after request=4 total=16:
 [win_window_ineligible/0x070e/rq0] [win_window_ineligible/0x070d/rq0] [win_window_ineligible/0x070b/rq0]
 [win_requested/0x170f/rq12] [win_waiting/0x170f/rq0] [win_waiting/0x3b0f/rq0] [win_waiting/0x2b0f/rq0]
 [win_ready/0x0b0f/rq0] [win_ready/0x030f/rq0] [win_serving/0x070f/rq0] [win_requested/0x170f/rq12]
 [win_waiting/0x3b0f/rq0] [win_waiting/0x2b0f/rq0] [win_ready/0x0b0f/rq0] [win_ready/0x030f/rq0]
 [win_track_not_playing/0x0307/rq0]
```

Reading it: the first three rows are the **entry** exit with `ep=0` (`0x070e` — bits 1,2,3 set, bit 0 clear), then with
`av=0` (`0x070d`), then with `al=0` (`0x070b`): three different reasons, one token, told apart by their own fields. The
admitting call carries `rq=12`. The lane's episode then moves `waiting → ready → serving` **12 rows across 120 lane
turns**, and the second `win_requested` is the next interval on the next lap. `0x0307` is the stopped transport
(`pl=0`).

## 4. Verification

- **Focused:** `test_runtime_working_set_ownership` **144/144** (was 143) — the new case
  `test_rolling_gate_rows_name_the_demand_paths_silent_exits_once_per_state_change`;
  `test_capture_ring` **16/16** with the new Tier-A boundary pin (`DIAG,win_gate,` in, `DIAG,win_gatex,` out).
- **Red proofs, each failing exactly one case and each restored byte-identically** (`src/Track/TrackPlaybackWindowBuild.cpp`
  sha256 `27d6ffde909d72566b7ab5bb94a1a2603072510fe99b974a7a928264f5f8f230` before and after every mutation;
  `git diff -- src` empty afterwards):
  **MR-W1** the entry exit goes silent again → *"Expected 1 Was 0. the exit that refused names itself exactly once"*;
  **MR-W2** the dedupe removed → *"Expected 1 Was 2. one row per state change, never one per call"*;
  **MR-W3** the shaping terms stop being told apart → *"`av=0`: a loop the whole-take policy admits never takes the
  windowed demand"*.
- **Full native:** **2,801 cases / 7 skipped / 2,794 succeeded / 0 failed**, no ERRORED suite (was 2,798 / 7 / 2,791).
- **Builds through the RAM1 guard, both byte-identical (no re-base):** `teensy41-capture-serial` **SUCCESS**
  (`variables 96,192 / code 391,576 + padding 1,640`) and `teensy41-capture-serial-window-qualification` **SUCCESS**
  (`variables 96,192 / code 392,232 + padding 984`) — the qualification-only row costs this image nothing that moved.
- **Marker contract:** `check_marker_contract.py 'DIAG,win_gate,'` → `OK … tier-a-prefix='DIAG,win_gate,' (14/31 bytes)`;
  `--audit` → **0 over**. `git diff --check` clean; `openspec validate windowed-playback-generation --strict` valid.
- **Storage/lifetime:** the cadence table is `DMAMEM` (RAM2) on the target — off-clock state read by the lane turn
  alone; nothing added to DTCM, no new member on `Track` or `LoopPlaybackRuntime`, and no change to the S3.0 size pins.

## 5. A defect found beside it (recorded, not glossed)

`DebugSessionCapture::isTierATextLine` carried a **duplicated `DIAG,launch_gate,` clause after its `return`** —
unreachable code left by the `snap_gate` slice (the first `return` already classified both families, so the duplication
was harmless but wrong). It is removed with the `win_gate` entry added in the same list, and the boundary pins in both
capture suites cover the family as it now stands.

## 6. The device read (authorized after this slice's native verification)

The owner authorized the upload and the registered leg, and the row was read on hardware. Three device runs
(`captures/s64-win-gate-0{3,4,5}`) measured the demand's decision **and found this row's cadence wrong** — the traps
list's *"dedupe that alternates forever"*: 809 rows in one leg with up to 20 identical lines per state, because bits 10
(`coverageRoleAvailable`) and 13 (`viewBuild.active()`) alternated A↔B while the exit never changed. The change key was
corrected against that measurement (bits 10/11/13/14 are **observation**, not decisions); the same leg then produced
**57 rows** with no alternation and only genuine differences left (`len`/`al` as the recorded loop grows). Run 04 is
recorded too, because it is the *wrong first guess* (bits 11+13 cleared, `rl` still alternating) — the second fix
followed the measurement, not the assumption.

And the answer five runs of inference could not reach is now a row: `ep=1` in every row (the deadline clock **is**
stamped, so the clock-stamp hypothesis is refuted), and the demand dies at its **entry** because the leg's committed
loop is **not a whole number of two-bar spans** (`len=19200` = 25 bars, `al=0`) while it *is* demoted (`av=1`); where the
loop is aligned, the second gate is named instead (`x=win_not_servable`, `rt=0`). Full record, including the profile's
own oracle replayed over the run and the two host-tooling failures that shaped the invocation:
[`DEVICE-RUN.md`](DEVICE-RUN.md).

## 7. What this does *not* claim

- **No F-7/F-8/AC-8 or hardware-envelope claim.** The lane was never given a *demand* in those runs — that is what the
  rows show — so nothing about its service, timing or memory was measured. Task 6.5's four hardware claims stay **NOT
  RUN**; **6.4 and 6.5 stay open**, and nothing is checked off.
- **No claim about an exit the runs did not reach.** The fixture pins that the row *names* the terms; the device then
  named them itself. Those are observations, not a claim that every exit has been seen on hardware.
- **No instrumentation of `resolveRollingPlaybackWindow`.** That is the **consumption** path, called on the playback hot
  path per tick; only the off-clock demand turn is instrumented, per the instrumentation rule.
- **No fix of what the row showed.** The unaligned committed length is a *leg-shape* matter, and the root currency is the
  owner decision already on record (RWO's restore edge vs a DEC-108 source rule). The row's job was to end the
  inference; it did.
