# Rolling-on 2-bar base recipe, re-run on the current tree: the same stall, and the timings that *are* reachable (2026-10-18)

**Requested:** run the HITL base pass (record + two overdubs + post-stop undo/redo, `STIM-BASE-2-2-01@v1`) with
**rolling playback ON** and verify it works within the timing goals.

**Answer: it does not work — the recipe stalls exactly where the 2026-10-17 A/B left it.** Two independent runs on two
fresh builds of the current tree both end `INCONCLUSIVE_CAPTURE` with `OVERDUB_STOPPING = 0`: the second overdub and the
undo/redo steps never run, so the recipe's own timing goals are **unreachable**, not missed. The timing that could be
measured (record leg + PLAYING) is **inside** every enforced check, and its lateness instrument reads **tighter** than
the rolling-OFF control's. Nothing was fixed; no production line changed.

## Identity

| | Run 02 | Run 03 (verdict-grade) | Rolling-OFF control (2026-10-17) |
|---|---|---|---|
| Build env | `teensy41-capture-serial-window-qualification` | same | `teensy41-capture-serial` |
| Firmware HEX | `7d03ac930b819247d4a2e3b261dd62bb3bf1d9c6137157568c361646adf44ae4` | `5a5d8c8da4ca7f8815fbeb57612a7eb86ff154b2f71942260cf75cf2e57a7131` | `b67684e4…` |
| Build root | `$PWD/.pio-build-local-fw` (**not** the configured root) | the configured `build_dir` (clean rebuild) | configured root |
| `metadata.json` `firmware_hex_sha256` | `6c495ade…` — **stale, not the flashed image** | `5a5d8c8d…` = the flashed image | matches |
| RAM1 | `variables 96,192 / code 392,280 + padding 936` ≥ floor 840 | same (96 B above floor) | unchanged line |
| Source | `26125ceb` + the branch's dirty tree | same | same |
| Capture | `captures/i5-rolling-base-2bar-02` | `captures/i5-rolling-base-2bar-03` | `captures/i5-rollingbase-control-off-01` |

Both rolling runs used `--boot-reset`, track 8 / slot 4 (the authorized scratch slot), channel 8, device clock.

**Build identity is not source identity.** `src/RtcTime.cpp` folds `__DATE__`/`__TIME__` into the image, so every build
produces a *different* hex from identical sources — three clean builds of this tree gave `7d03ac93…`, `9abaddb6…`,
`5a5d8c8d…`. A hex hash therefore pins **one build**, never a source tree, and no hex comparison can settle "did the
tree change". Identity for this run is `git 26125ceb` + dirty tree + the flashed hash above.

**Evidence-integrity trap found and fixed mid-session.** `scripts/hitl/run_metadata.py::firmware_hex_path` resolves the
hex from `PLATFORMIO_BUILD_DIR` if set, else the `build_dir` configured in `platformio.ini`
(`~/Library/Caches/pio-build-250513-215524-teensy41`). Run 02 was built into `.pio-build-local-fw` while the shell did
not export `PLATFORMIO_BUILD_DIR`, so the run was pinned to whatever that configured root last held — the **withdrawn
candidate-3** image `6c495ade…` (`a2bd536a…` for the default env), a false pin, not a null one. Run 03 was rebuilt clean
in the configured root so the pinned hash **is** the flashed hash. Rule for later runs: build where the harness reads, or
export the same `PLATFORMIO_BUILD_DIR` the flash came from.

## What the two rolling runs show (identical signature)

- verdict **`INCONCLUSIVE_CAPTURE`** both runs (`capture_complete: false`, `setup_transitions_confirmed: false`); abort
  reason **`second overdub transition not confirmed`**.
- `interval_counts` = `{RECORDING 1, PLAYING 1, OVERDUBBING 1, OVERDUB_STOPPING 0}`;
  `overdub_stopping_segments = []`; the verifier's own diagnostic is **`overdub_without_stop`**.
- **The stop was accepted and latched, not denied.** The log carries exactly one
  `#CAP,…,DIAG,latch,accept,7,3,2,0,playing` (track 7, slot 3, interval 2) — `Track::stopOverdubbing` falls into the
  capacity branch and calls `latchAcceptedStop` there, which under **DEC-078 / task S4f.1** is the accepted obligation,
  not a refusal. The four `[TRACK] Overdub stop refused: stop_job_pending slot=3 interval=2 phase=16 …` rows
  (run 03: 38.3 s, 47.0 s, 55.1 s, 63.2 s; run 02: from 48.6 s) are the **retained DEC-067 counterfactual diagnostic**
  the retention oracle reads, plus its slot/interval attribution — reading them as the disposition is wrong.
- **The accepted obligation is never driven.** `Track::serviceAcceptedInteractionTurn()` returns immediately while
  `hasPendingDeferredOverdubStop()`, so the latch is never retried while the wrapped take's `CaptureStopJob` is pending:
  the log has **no** `DIAG,latch,consume` and **no** `accepted_stop_finalizer_wait` row (that counter is emitted only
  from `parkAcceptedStopForCanonicalOutput`, i.e. after a drive attempt). That is why a *latched* stop and a *denied*
  stop are indistinguishable in this evidence — the observability gap **task S4f.8** owns ("make the waits observable as
  scheduling, not refusal").
- Run-end state is the same parked job every time:
  `#STOP_STATUS … state=OVERDUBBING,pending=1,phase=16,dest=1,queue=1/4` (run 03 last sample at 103.2 s),
  `Record stop retarget: phase=16 dest=1` ×6, and the run-end allocator observation `#HEAP_STATS,refused=track_work`.
- The **record leg completed on both runs** (`RECS,stop,3,357,0,1893,1536,0` run 02 with `raw 1893`; run 03
  `raw 1785` → final `1536` in both) — the 2-bar bucket, take wrapped.
- The lane never served this loop: `win_window_ineligible` ×5, `win_not_servable` ×2, `win_track_not_playing`
  ×18–21, and both `win_readiness` rows read `not_servable`.
- `snap_gate` again shows the **construction running to completion while the overdub was live** and the session-exit
  root then publishing and becoming current (`st` 416→473, `cu` 62→158 with `hp=0`, then `p=0,sep=0,hp=1,rv=1`) —
  while `#STOP_STATUS` still reports `pending=1,phase=16`. That confirms on a second image what the 2026-10-17 session
  concluded from run 01: the remaining gate is **downstream of root currency**, in the DEC-136/DEC-140 hold's counting
  condition, and the "which adoption edge refuses a ready successor on a 2-bar loop" question stays open.
- Undo/redo: `undo_log_count = 0`, `redo_log_count = 0` (goal ≥1) — unreachable, never attempted.
- Run 03 additionally reports `persistence_stage_missing:result` and
  `persistence_active_snapshot_unavailable_during_save`; both are **consequences of the abort** (the deferred save was
  still in flight when the session ended), not independent defects.
- Shutdown was canonical on both runs (`2` stop+drain cycles, persistence idle, no latch); ring loss run 02
  `shed 41, overflow 0, crit_rej 0`; capture cleanup `dedup 0 / minlen 0 / wrap_synth 0`.

## Timing: what is enforced, what was measured, what is unreachable

No **signed** microsecond limit exists in this repo to compare against — `scripts/hitl/measure/envelope_timings.py`
declares no limit (its own docstring, DEC-108 §13b), and the base verifier's enforced list is `timing_warnings`, which is
**empty** on both rolling runs. The enforced, recipe-level timing goals and their readings:

| Enforced check | Goal | Rolling ON (run 03) | Rolling ON (run 02) | Control (rolling OFF) |
|---|---|---|---|---|
| Overdub stop reaches its destination (boundary exists) | 1 per overdub, 2 total | **0 / 2** ✗ | **0 / 2** ✗ | 2 / 2, **19 µs** each |
| Undo/redo after overdub stop | ≥ 1 each | 0 / 0 ✗ | 0 / 0 ✗ | 1 / 1 ✓ |
| Record first-note capture latency | ≤ 12 clocks | **6** ✓ | **8** ✓ | ✓ |
| Overdub / second-overdub first-note latency | ≤ 12 clocks | *not measured* (`None`) | *not measured* | ✓ |
| Record loop length | take's own bars | 1536 / 1536 ✓ | 1536 / 1536 ✓ | 1536 ✓ |
| Record-stop stage trace | all stages present | slowest `finalize 585 µs` ✓ | `finalize 576 µs` ✓ | ✓ |
| Capture-ring overflow-free / call-site gate | both true | ✓ | ✓ | ✓ |
| `timing_warnings` (the enforced list) | empty | ✓ (empty) | ✓ (empty) | ✓ (empty) |

Advisory instruments (device rows, no enforced threshold; quantiles are bucket bounds):

| Row | Rolling ON (run 03, last window) | Control (rolling OFF) |
|---|---|---|
| `late_on` p50 / p99 | **0–255 µs / 0–255 µs** (n=42) | 20,736–20,991 µs / 20,736–20,991 µs (n=61) |
| `late_off` p50 / p99 | **0–255 µs / 0–255 µs** (n=41) | 20,736–20,991 µs / 20,736–20,991 µs (n=58) |
| `late_route` NoteOn / NoteOff max | **0 µs**, over 0/41 and 0/14 | 20,969 / 20,929 µs, over 451/495 and 421/457 |
| `midi_gap` p99 | 768–1023 µs (last window; earlier windows 1023–11,775 µs) | 2560–2815 µs |
| `midi_input` p99 | 0–255 µs | 0–255 µs |
| `deadline_samples` | **none** (empty) | 8 samples, `deadline_safety_proven: false` |

**Read carefully:** the rolling runs aborted at the first overdub, so their lateness rows cover the **record leg and the
single PLAYING interval only** — not the full recipe, and there are no correlated deadline samples at all. The
~20.8 ms on every note in the control is one MIDI clock pulse at 120 BPM (24 PPQN ⇒ 8 internal ticks × 2.60 ms); it is
recorded, not attributed, and the verifier treats it as advisory. So on the timing axis rolling-on is **tighter**, and
the failure is **functional**: the pass never gets far enough to be judged.

## What this does and does not change

- **No firmware change, no DEC, no limit, no checkoff**; 6.4 / 3.15f stay open at 29/60 and the DEC-140/141
  `rolling_record` PASS is untouched (different preset, 8-bar take).
- **No SD destructive action**: only the authorized scratch slot (track 8 / slot 4) was written, through the stimulus's
  own pre-record clear.
- **Device restored** to the exact pinned default `08f43e450ee327546b844af95a8b9ac57be46fcb8e84ad982ddda10e357fc964`
  uploaded no-build from the retained file (hash verified before and after), with a read-only postflight
  (`captures/i5-rollingbase-restored-postflight-02`): boot banner, reconcile 0, `free_chunks=398`,
  admission/interactive/undo/stopped/prewarm-idle all 1, readiness attested, **S2b.3 met** (398 vs 16 required),
  library observation complete over 64 slots (`payload_inventory=64`, `initial_playback=8`, `restore_completions=0`,
  `committed_at_checkpoint=22`, `representation_observed=true`; the observation's own `consistent_view=false` is
  recorded, not explained here).
- **The work is now tracked by mechanism, in two places** (owner-reviewed split, 2026-10-18):
  this change's **3.15g** owns the liveness defect — a wrapped short-loop Record finalization must build a
  **positioned** root demand (`epoch != 0`, `deadline != 0`) and the `CaptureStopJob` must then retire, with one
  regression case carrying job retires → latch `consume("admitted")` once → `OVERDUB_STOPPING` boundary → destination;
  its native RED must fail **at the construction point**, not at the retirement check, so no later change weakens the
  gate that correctly observes a job that cannot progress. `runtime-working-set-ownership` **S4f.8** owns the
  observability defect found here (a waiting accepted stop must never be classified as a refusal). **Not** in scope of
  either: removing `serviceAcceptedInteractionTurn()`'s `hasPendingDeferredOverdubStop()` yield, which is correct — one
  heavyweight deferred-finalization owner per track+slot stays (DEC-078).
- **The 2026-10-17 ordering still stands as the first step:** make `test_rolling_record_retirement` publish the **wrap
  root** for a 2-bar take and assert the session-exit starts and publishes while the overdub is live; then the narrow
  change at the construction point, gates, device rerun. This session adds no reason to reorder it — only a second,
  properly pinned device witness, and the two task lines that name the mechanism.
## The fix attempt: the RED is not reachable yet; the chain is now a green native control (2026-10-18, after the packet above)

Owner asked to implement the fix. The first mandated step is a native RED at the construction/root-demand point for a
**wrapped** take. It was built and it **does not reproduce the stall**:

`test_wrapped_two_bar_record_stop_retires_while_the_transport_runs` (`test_rolling_record_retirement`, rolling enabled,
production owners only) records a take that **wraps** its bucket — two-bar geometry, so the lane declines with
`win_window_ineligible` (`shouldAvoidFullVisualRebuild(1536)` is false) — reaches `RecordPublished`, starts a **live
overdub while the job is still pending**, publishes the session-exit root **during** that overdub, then presses the
overdub's stop. The whole chain completes: job retires, accepted stop executes once, queue empties, record pass keeps its
committed rows. **It is green**, so the wrapped 2-bar geometry *plus* a live accepted stop is not the board's state.

Two intermediate measurements, both recorded because they cost device runs to discover otherwise:

- the wrapped take **without** an overdub also retires (the geometry assertions passed first);
- the two-bar geometry is only reachable when accepted input keeps arriving into bars 2–3 — a clock-advance-only take
  still stops as one bar (the `stopRecording` extent re-derivation the 2026-10-17 session hit), which is why the earlier
  attempt concluded the shape was unreachable.

**No production line was changed** (a patch without the RED repeats candidates 1 and 3, both withdrawn). What this
fixture still does not carry is exactly what the 2026-10-17 packet named: a **complete, unpublished root-backed
successor in the ready role** (device `ready=2`) or a serving frozen Capture view at the release edge — here
`ready=255` with no frozen current view. Next, in order: (i) instrument the E2 root-return block to name the refusing
term on the board (one bounded row per slot+term, `LaunchGateLastState` cadence, `SC_PLAYBACK_BUILD_ROW` vehicle,
FLASHMEM site, no behaviour change) and read it from a 6.4 profile run; (ii) build the fixture that carries *that* state
and take the RED from it.

Gate: full native suite with the new control — **3,305 cases / 7 skipped / 3,298 succeeded / 0 failed**.

## The E2 instrument: implemented, verified, and blocked by RAM1 (2026-10-18, same session)

The owner-approved route 1 was built in full — one read-only `#CAP,<us>,DIAG,e2_ret,…` row carrying the eight
root-return adoption terms as a flag mask plus the adoption's own refusal codes (1–6), `SC_FMT`/`PSTR` on both delivery
paths, `DMAMEM` transition-cadence state, Tier-A registration, the exact tier boundary pinned in `test_capture_ring`,
and a green marker pre-flight (`check_marker_contract.py`, `'DIAG,e2_ret,'` 12/31 bytes).

**It does not fit the approved RAM1 line.** Qualification `variables` went 96,192 → **100,288 (+4,096)** while `code`
moved only 392,280 → 392,552 — the alignment snap `check_ram1_budget.py` names — and it survived three targeted trims:
the cadence state moved to `DMAMEM`, the file-scope format replaced by `SC_FMT` at both call sites, and the emitter
stubbed to a no-op. So no single symbol is the cause; the small additions cross a `.rodata` edge. Padding fell 936 →
**664**, under the 840 floor. The real constraint is headroom: **96 B** on the qualification image, **240 B** on the
default.

Reverted in full (no `e2_ret`/`rootReturnBoundary*` symbol remains), line restored byte-exactly (qualification
`96,192 / 392,280 + 936`; default `96,192 / 392,136 + 1,080`), both builds SUCCESS, full native **3,305 / 7 / 3,298 /
0 failed**.

**Cheapest path back to the evidence (owner decision on the first two):** (a) carry the same fields on the existing
`win_frozen_readiness` receipt — already Tier-A, already emitted at this edge, no new family and no new format
machinery; (b) add a `.rodata.str*` carve-out for the emitting TU (the traps' own remedy); (c) re-base the budget
deliberately (never by an agent). Then one 6.4 profile run on the 2-bar recipe names the refusing term.

## Option 1 taken: the adoption diagnostics ride the existing receipt, and the board answered (2026-10-18)

Per the owner's decision, the reverted standalone family was replaced by an **append-only extension of the existing
Tier‑A `win_frozen_readiness` receipt**: three trailing fields — `mask`, `result`, `why` — added after `loopLength`,
never reordering or re-meaning an existing one (the lane parser skips rows with fewer than 15 fields, so an older
reader is unaffected). `mask` is the read-only eight-term adoption mask (bit0 the served view is this job's frozen
window, bit1 the root is **current**, bit2 the ready role is valid, bit3 it is complete, bit4 it is **root-backed**,
bit5 its start phase is the playhead's, bit6 its loop length is the loop's, bit7 the rolling frame envelope holds);
`result` is 0 not attempted / 1 adopted / 2 refused; `why` is the adoption's own refusal code (1–6). It is emitted
from the E2 block twice per decision — the state **before** any attempt (`adopt_wait`) and the answer after one
(`adopted` / `adopt_refused`) — transition-cadenced in `DMAMEM` state, computed in a cold FLASH helper so the clock-path
TU costs no ITCM.

**Gates:** qualification `variables 96,192 / code 392,360 + padding **856** ≥ 840` (16 B headroom, no budget change);
default `96,192 / 392,136 + 1,080` unchanged; full native **3,305 / 7 skipped / 3,298 succeeded / 0 failed** (the tier
pin for this family added to `test_capture_ring`). `check_marker_contract.py` reports FAIL for this family's prefix and
that is a **static-scan limitation, not a runtime gap**: the prefix is registered in `CaptureLineTier.h` through the
`frozenReadinessTagPrefix()` accessor (line 198), which the tool's literal scan cannot follow; delivery under transport
is proven by the rows below.

**Device runs.** `captures/i5-rolling-base-2bar-04` (HEX `393f185b…`) and `-05` (HEX `bf40dac6…`, with the pre-attempt
row). Both stall identically: `INCONCLUSIVE_CAPTURE`, `#STOP_STATUS … state=OVERDUBBING,pending=1,phase=16,queue=1/4`,
run-end `#HEAP_STATS,refused=track_work`.

Run 05's receipt rows (track 7 / slot 3), decoded by mask:

| row | mask | bits standing |
|---|---|---|
| `adopt_wait` | 68 (`0x44`) | ready role valid, ready loop matches — root **not** current, ready incomplete, frame not serving |
| `complete_ready` (completion transition) | 0 | — |
| `adopt_wait` | 76 (`0x4C`) | ready valid + complete + loop matches |
| `adopt_wait` | 108 (`0x6C`) | ready valid + complete + **start phase = playhead** + loop matches |
| `adopted` (result 1) | 0 | the frozen **Capture** successor was adopted at a boundary |
| `adopt_wait` | 129 (`0x81`) | served view is this job's frozen window, frame envelope holds |
| `adopt_wait` | 205 (`0xCD`) | served frozen window + ready valid + complete + loop matches + frame serving |

**The first false predicate, named from the board:** **`loop.publishedSnapshotIsCurrent()` is false at every E2
evaluation** — bit1 never sets, and therefore bit4 (a root-backed successor) never sets either. The only successor the
edge ever adopted was **Capture-origin** (the frozen handoff). Then the rows **stop**: the edge's own entry condition
requires the job's *qualified frozen source*, which that adoption consumes, while `snap_gate` later reports
`hp=1, rv=1` (root published and current) with the job still `pending=1, phase=16`.

**Hypothesis, stated as such (mechanism, not yet proven):** the E2 root-return adoption edge and the retirement hold
share one prerequisite — the job's qualified frozen source — and adoption consumes it, so the root-backed successor the
hold waits for can never be adopted by the edge that would supply it. If that holds, the fix belongs to the
hold/edge's *successor qualification* (adopt a Committed successor of the current root even after the frozen handoff),
not to the wait's counting. Next code step: make the receipt observable from the retirement hold itself (the same
family, holder `retire_hold`), so one run settles it — the mask is already the right vocabulary.

## The hold answered it: the ready successor exists, is complete, and is **not root-backed** (2026-10-18)

Run `captures/i5-rolling-base-2bar-06` (HEX `48bcf92a…`, same recipe and slot) adds the **retirement hold's own
observation** on the same receipt family (`holder=retire_hold`, one-argument cold call; a string-pointer parameter cost
64 B of ITCM and did not fit — a one-bit site flag does, at `code 392,360 + padding 856`).

Decoded masks (bit0 served view is this job's frozen window, bit1 root current, bit2 ready role valid, bit3 complete,
bit4 **root-backed**, bit5 start phase = playhead, bit6 loop length matches, bit7 frame envelope):

| site | mask | standing bits |
|---|---|---|
| `e2_boundary` | 76 (`0x4C`) | ready valid + complete + loop match (root not current yet) |
| `e2_boundary` | 108 (`0x6C`) | + start phase = playhead |
| `e2_boundary` (adopted, result 1) | — | the frozen **Capture** successor adopted |
| `e2_boundary` | 129 / 205 / 207 | served frozen window, ready complete, loop match, frame serving — **bit4 clear** |
| `retire_hold` | **239** (`0xEF`) | everything except **bit4**: root current, served frozen window, ready valid + **complete** + **loop match** + **frame serving** — and the ready successor is **not** root-backed |

**The fault is now named at the identity level, not the counting level.** In the hold, a **complete** successor sits in
the ready role, the root is **current**, the track is audible and the frame serves — but that successor is
**Capture-origin** (built from the job's frozen token), never a **Committed successor of the published root**. Every
adoption edge in the code that could release the job requires the root-backed identity (bit4), so it never adopts; the
hold, correctly, sees an owed successor and the audible transport and keeps waiting. On this geometry nothing can ever
build the root-backed one: the ordinary window lane refuses the loop (`win_window_ineligible`,
`shouldAvoidFullVisualRebuild(1536)` false) and the private root-return route (DEC-136) prepared from the frozen Capture
token instead.

**Fix direction (for the next increment, and it is *not* the wait):** the private root-return preparation must target
the **published root** once it is current — Committed origin, published-snapshot identity — for a loop the lane cannot
serve, so bit4 can set and the E2 boundary adopts. Weakening the hold or the `serviceAcceptedInteractionTurn` yield
would hide this, not fix it. The native RED is now constructible from an *observed* state (complete Capture successor in
ready + root current + bound job + audible transport), which is the intermediate shape the 2026-10-17 session could not
build.

**Instrument note:** the first run of the two-site version emitted 79 rows for one stall (34 from the hold) because the
two edges sample different instants and their start-phase bit alternated. The cadence signature now excludes bit5, so a
genuine change costs one row and the sites cannot ping-pong.

## The native RED, built from the observed 0xEF state (2026-10-18)

`test_observed_hold_state_produces_a_root_backed_successor` (in `test_rolling_record_retirement`, rolling enabled) feeds
the **board's own state** rather than replaying a whole run: a wrapped two-bar take reaches `RecordPublished`, the root
is published and current, the job's frozen **Capture** successor is completed into the ready role, and then the
production owners (window lane offer/select/step, the clock-edge reader, the deferred idle owners) are driven with the
transport running. It asserts the identity every release edge requires — a **Committed** served window of the published
root — and it **fails**:

```
steps=8192 cur=1 ready=255 readyComplete=0 readyOrigin=255 root=1 bound=1 holdGrains=0 pending=1
        | pendActive=1 pendKind=1 pendReason=7 pendEpoch=0 pendDeadline=0 pendLead=0 inval=0
```

Read: the Capture successor was adopted into `current` (the reader is the frozen window), the root is current, the job
is bound — and the **retained demand is a Committed one (`pendKind=1`) stamped `SourceNotCurrent` (`reason=7`) with no
position** (`epoch=0`, `deadline=0`), so it can never progress and nothing ever builds the root-backed successor.
`ready=255` is the consequence, not the cause.

**The two-way narrowing this gives (each a concrete next probe, one line of code each, no guessing):**

1. the demand keeps being **re-stamped `SourceNotCurrent`** — i.e. `playbackWindowSourceIsCurrentForSlot` (or the
   revision term beside it) answers false while the *served* window is the frozen Capture view of the pre-publication
   revision and the demand is for the current published root. That predicate is broadly used, so it must be read before
   it is touched, not widened blindly.
2. **`rootReturnSuccessorIsOwedForSlot` still requires `qualifiedFrozenRecordCaptureSourceForSlot`** — which needs the raw
   range and owner in hand and answers false once the frozen window has been adopted. Relaxing exactly that term to the
   job's own identity (`frozenCaptureContentGeneration != 0 && job->admission.pending`) was tried and is **inert** while
   (1) stands: the demand is still re-stamped and still unpositioned. Reverted, because an inert change is not a fix.

**Fixture now reproduces the board's state attribute-for-attribute (2026-10-18, later).** Three details had to be right
before the case was faithful, and each is worth recording because it is what makes this a reproducer rather than a
near-miss: the frozen return window must be built with the **loop** length (not one bar) or it is refused by
`frozenRecordReturnWindowIsServedOrOwed`; the frozen successor must be **adopted** (`adoptPreparedPlaybackWindow`), or
`current` is still the live one-bar window; and only then does the served reader match the job. The driven state now reads

```
curOrigin=2(Capture) curToken=37==jobToken curLoop=1536 curLen=768 curPublished=1
root=1 bound=1 pending=1 | pendKind=1 pendReason=7 pendEpoch=0 pendDeadline=0
covStart=768 covLen=768 covLoop=1536 revMatch=1 snapMatch=1
prep=0 buildActive=0 needsSvc=0 ownsSrc=0 gather=0 paged=0 frozenActive=0
```

i.e. exactly the board: the job's frozen window is the served reader, the root is current, the job is bound, the retained
demand is a Committed one-bar window of the current root — and it is still stamped `SourceNotCurrent` with no position,
with **nothing in flight**. Two candidate production edits were tried in this state — relaxing the
`qualifiedFrozenRecordCaptureSourceForSlot` term to the job's own identity, and admitting a null snapshot identity
because the qualification branch is what writes it — and **both are inert here**, so both were reverted; an inert
change is not a fix. The remaining unknown is narrow and named: **whether `committedPlaybackRetryCanProgress` opens at
all in this state** (it is private; the next step is an accessor or a harness hook to assert it directly rather than
infer it). Until that is answered, the demand's `SourceNotCurrent` stamp and its absent position are two facts that
have not yet been tied to the gate that consumes them.

## Correction: the "RED" was the harness's epoch-less clock, and the state is GREEN once qualified (2026-10-18)

The probe the previous section named was run, and it overturned the RED. `TrackCaptureStopTestAccess` now exposes
`committedPlaybackRetryCanProgress` and `rootReturnSuccessorIsOwedForSlot` directly, and the fixture asked them instead
of inferring: both answered **false**, with `epoch=0 tick=0` — while `clockManager.getCurrentTick()` read 133312.

**Cause: the native harness stubs the deadline clock.** `test/test_support/track_harness.inc:127` defines
`ClockManager::assignCurrentTickSilently(tick) { currentTick = tick; }` and returns a `nativePlaybackDeadlineClock{}` that
is never stamped, so `epoch` is **always 0** in every suite that does not define `HARNESS_LINKS_REAL_CLOCK_MANAGER`. Every
epoch-gated path — `rootReturnSuccessorIsOwedForSlot`'s first term, `committedPlaybackRetryCanProgress`'s position gate,
the DEC-136 private route, the hold — is therefore **structurally unreachable** in this family. That is also why two
sessions of candidate patches measured inert, and why no amount of fixture care could reproduce the board.

**Once the clock is qualified — the harness's own P1 pattern (`nativeTransportRunning` is test-controlled the same way;
here `nativePlaybackDeadlineClock = {tick, 3u}` each step) — the same fixture PASSES.** The production path builds the
root-backed successor, adopts it, and serves it. So:

- the earlier "RED" and its gate values were a **harness artifact**, not the board's condition;
- the board's defect is **not** that no root-backed successor can be produced for this state, and not the successor
  identity — it is that **on the board the retained Committed demand is stamped `SourceNotCurrent` with no position
  (`epoch=0`, `deadline=0`)** while a real epoch exists (the same row the 2026-10-16 packet measured). The next probe is
  therefore the re-stamp site, with the *live* epoch recorded: which of `TrackPlaybackWindowBuild.cpp`'s
  `SourceNotCurrent` assignments runs for this slot, and why the demand is unpositioned when `clock.epoch != 0`.

**Kept:** the case stays registered as a **green control** for the observed state (it is the proof that the state is
serviceable when its clock is qualified), beside the wrapped-two-bar control. Full native **3,306 / 7 skipped / 3,299
succeeded / 0 failed**; both builds unchanged and inside the floor.

**Left in the tree deliberately:** the case stays registered as the intentional reproducer (full native **3,307 cases /
1 failed**) — it is the RED the fix must turn green, and the wrapped-two-bar control beside it stays green as the
no-weakening guard. Both builds unchanged (`96,192 / 392,360 + 856` qualification, `392,136 + 1,080` default).

## The hold rows now carry the demand's own stamp — and one value is a *terminal* answer (2026-10-18)

A `retire_hold` row has no adoption to report, so its `why` field now carries the **demand's position stamp** (bit0 active,
bits 1–4 the reason, bit5 `clockEpoch == 0`, bit6 `deadlineTick == 0`). Device run `captures/i5-rolling-base-2bar-07`
(HEX `71e9369b…`) — again the same stall (`refused=track_work`) — returned four hold rows:

| `w` | decode |
|---|---|
| 15 (`0x0F`) | active, reason **7 = `SourceNotCurrent`**, clockEpoch **non-zero**, deadline **non-zero** |
| 21 (`0x15`) | active, reason **10 = `ClosuresExceedDeclaredCapacity`**, clockEpoch and deadline non-zero |
| 96 (`0x60`) | no active demand, both position terms zero |

Three facts follow, and they correct the previous section's reading once more:

1. **the board's demand is positioned** (`clockEpoch` and `deadlineTick` are set) — the "unpositioned (`epoch=0`,
   `deadline=0`)" row belongs to the earlier arm, not to this state;
2. it is nevertheless stamped **`SourceNotCurrent`** with its position intact, so a re-stamp site runs while the
   position is still valid;
3. and in the same window it is once stamped **`ClosuresExceedDeclaredCapacity`, which that reason's own contract calls
   *terminal for the lane's retry*** — "a refusal with a name is an answer, not a demand to re-gather every turn". A
   demand in that state is *never* retried, which is a complete explanation for a job that never retires while the
   accepted stop waits behind it.

**This is a lead, not a verdict** — four rows, and the decoding is this session's own encoding. The next step is to
confirm or reject it directly: assert the slot's demand reason across the stall (the same fixture hook that settled the
clock question), or read the existing `pending_position_invalidated` / `win_gate` rows against it. If
`ClosuresExceedDeclaredCapacity` is the terminal answer for a wrapped two-bar take, the fix belongs to the **closure
capacity/derivation** for that content — a different owner from the successor identity and from the wait.

## The terminal-refusal lead is **rejected** natively at the board's own density (2026-10-18)

The lead in the previous section was tested as an invariant rather than accepted: the observed-state fixture now records the
demand's reason across every driven step and fails if the bound job's demand is ever stamped
`ClosuresExceedDeclaredCapacity` (reason 10 — the reason whose own contract calls it terminal for the lane's retry). The
fixture was then brought up to the **board's own content**: a note pair every 24 ticks with a 12-tick gate (~32 per bar)
over a take that stops at ~1920 raw ticks, so the bucket stays two bars — the board's `raw 1893 > final 1536` shape.

Result: **green.** `terminalSteps = 0`, the root-backed successor is built and adopted, the job retires, and the
accepted stop executes exactly once. So, with (a) the deadline clock qualified and (b) the board's density, the whole
chain works natively and the demand is **not** terminally refused by its own content.

**What that rejects and what it leaves.** The board's reason-10 stamp is therefore not produced by this state's own
closure profile (`owesClosures && closureHighWater > ActiveNoteLedger::kMaxOpenNotes` is false here at board density), so
either the stamp comes from a *different* interval the paged lane attempted, or `pagedOutcome`/`retainedGatherRefusal` is
**sticky** from an earlier attempt and repaints the demand's reason each turn. Both are readable without new firmware:
the hold row already carries the demand's stamp, and the existing `win_gate` / `retainedGatherRefusal` surface names the
lane's own refusal. The next probe is therefore to pair the demand stamp with the lane's refusal name in one row (the
`why` encoding has room) rather than to reason about capacity further.

**Fixture value kept:** the case is now the densest faithful control in the family — board density, wrapped two-bar take,
qualified clock, full chain asserted — and it is what will falsify any future "fix" that weakens a gate to make the board
pass.

## The demand's terminal reason is **stale**: it survives the outcome that produced it (2026-10-18)

The lane state is now reported beside the demand stamp, from the same call site (`hold_lane`, its own cadence; `why` packs
`pagedOutcome` in bits 0–3 and `retainedGatherRefusal` in bits 4–7). Runs 09 and 10 are both the same stall; run 10
(`captures/i5-rolling-base-2bar-10`, HEX `0aae9c97…`) carries 16 receipts, decoded:

| time (µs) | row | mask | note |
|---|---|---|---|
| 38.30 M | `adopt_wait` e2 | 76 | ready valid + complete + loop match |
| 38.36 M | `complete_ready` | — | the frozen window completed |
| 39.37 M | **`adopted`** | — | the frozen Capture successor adopted |
| 39.52–40.03 M | `adopt_wait` e2 | 129 / 205 / 129 / 205 | ready complete, start = playhead — **bit1 (root current) and bit4 (root-backed) clear** |
| 40.79 M | `hold_lane` | 110 | **`pagedOutcome = None`, `retainedGatherRefusal = None`** |
| 40.79 M | `adopt_wait` retire_hold | 110 | root current ✔, ready complete ✔, **bit0 served-frozen and bit4 root-backed clear** |
| 40.79 M | `adopt_wait` retire_hold | 110 | demand stamp `why = 15`: active, reason **7 `SourceNotCurrent`**, positioned |
| 40.80 M | `adopt_wait` retire_hold | 110 | `why = 96`: **no active demand** |
| 43.36 M | `adopt_wait` retire_hold | 110 | `why = 21`: active, reason **10 `ClosuresExceedDeclaredCapacity`**, positioned |
| 43.38 M | `adopt_wait` retire_hold | 110 | `why = 15` again (reason 7) |

**The defect this names:** the demand carries the **terminal** reason 10 at instants where the lane reports
`pagedOutcome = None` twenty microseconds earlier — so the terminal stamp **outlives the outcome that produced it**. A
demand in that state is (by the reason's own contract) never retried, so the job never retires and the accepted stop
waits behind it. The fix direction is therefore narrow and in the derivation, not a bound: **a terminal reason must not
survive the clearing of the outcome that produced it** — it must be re-derived (or cleared) when `pagedOutcome` /
`retainedGatherRefusal` no longer say so, or the retry gate must read the *live* outcome rather than the stored reason.

This also explains why the native fixture cannot reproduce it: natively `pagedOutcome` and the reason stay consistent, so
the stale-stamp window does not exist there. The RED for this fix has to assert the *invariant* — "the demand's terminal
reason implies its producing outcome is still set" — which can be falsified natively by setting the reason and clearing
the outcome.

**Run 09** is recorded separately because it is a **different arm**: no frozen receipts at all, `snap_gate hp=0`, the same
`pending=1, phase=16` stall — i.e. the stall also occurs before any frozen/root path engages. Two arms, one symptom.

## The stale-stamp fix: real defect fixed, **not** the board's blocker (2026-10-18)

RED first: `test_a_stale_terminal_demand_reason_does_not_block_the_root_backed_successor` builds the board's state (root
current, frozen reader served, a Committed demand of the current root) and then stamps the demand with the **terminal**
reason while `pagedOutcome = None`; it failed with `rootBacked=0 reason=11 pagedOutcome=0 pending=1` — the stale stamp
blocked the retry, exactly as on the board.

**Fix (derivation, not a bound):** in `committedPlaybackRetryCanProgress`, the two reasons that *are the paged lane's own
answer* (`ClosuresExceedDeclaredCapacity`, `PinnedEditRowsUnresolved`) are terminal only while `pagedOutcome` still says
so. `WindowExceedsRetainedCapacity` stays unconditional — it is a property of the interval's size, not an outcome that
can clear. RED turns green, the observed-state control stays green, full native **3,307 / 7 skipped / 3,300 succeeded /
0 failed**, both builds inside the floor.

**But the board is unchanged.** Run `captures/i5-rolling-base-2bar-11` (HEX `ec97be8e…`, with the fix) is
**behaviourally identical** to run 10: same receipt sequence, the same demand stamps (`15`, `96`, `21`, `15`), the same
`#STOP_STATUS … pending=1, phase=16, queue=1/4`, the same `refused=track_work`. So the terminal-reason gate was **not**
what blocks this demand — the fix is necessary (a genuine stale-stamp defect, now natively pinned) and **not sufficient**.

**What that leaves, narrowed to the gate's remaining terms** — in `committedPlaybackRetryCanProgress` order, the terms
that can still refuse this demand are `coverageRoleAvailable()`, `playbackViewOutstandingGrains(slotIndex) != 0`,
`playbackWindowSourceIsCurrentForSlot(slotIndex)` and `request->source.playbackRevision == loop.playbackRevision`, plus
the fact that the hold's masks show **bit7 (frame serving) clear** in every `retire_hold` row. The next probe reports
those four terms on the same row (one more packed byte in the existing `hold_lane` vehicle); no new family, no new call
site.

## Blocker identified on the board: **no free view role** and a revision mismatch (2026-10-18)

The four remaining gate terms and the frame phase now ride the `hold_lane` row (`start` = packed terms, `length` = rolling
phase; both free on a diagnostic-only label). Run `captures/i5-rolling-base-2bar-12` (HEX from the fixed tree, same
recipe) reads:

```
gate terms 00000100   role=0  outstanding=0  srcCurrent=1  revEq=0  rollPhase=0  epochEq=0
```

- **`role = 0`** — `coverageRoleAvailable()` is **false**: the view set has **no free role** (current, ready, build and
  retained all occupied). A build cannot even start, which is why bit4 (root-backed) never sets and the job never retires.
- **`revEq = 0`** — the demand's `source.playbackRevision` differs from `loop.playbackRevision`, so the gate's final
  revision equality refuses it as well.
- the other two terms hold, and the rolling frame is **Idle** (phase 0) with its epoch **≠** the clock's.

So the stall is not the demand's reason (the stale-stamp fix above is necessary but not sufficient), not the successor
identity rule, and not the wait: it is **role occupancy swallowed by a complete successor that will never be adopted** —
the ready role holds a complete, unpublished Capture-origin successor while the root is current, and nothing retires
it. `RuntimePlaybackViewLifecycle::retireSupersededReady` exists for exactly this and is called only on a narrow path.

**That is the next fix target, and it must be narrow:** retire the ready successor as **superseded** when the root is
current and the bound job is owed a Committed successor of it, so the role frees for the build that can satisfy the
obligation — while leaving `current` (the served frozen window) untouched. E2, the retirement hold, accepted-stop gating
and successor identity rules stay as they are. The native RED is constructible from this row: drive the observed state
until the ready role holds a non-adoptable complete successor and require that a root-backed build eventually starts.

## Decisive: the handoff **is** entered; the retained demand's **revision is stale** (2026-10-18)

The `hold_lane` row now also carries the current-root handoff block's own entry terms (bits 8–15 of `start`). Run
`captures/i5-rolling-base-2bar-13` reads one row:

```
gates = 50948
  ON : srcCurrent(2) ownsRaw(8) servableView(9) audible(10) rootCurrent(14) bound(15)
  OFF: roleFree(0) outstanding(1) revEq(3) phase(4-6) epochEq(7) buildNeedsSvc(11) servedCopy(12) servedIsRoot(13)
  mask = 110, why = 0
```

Reading it against the block's guard (`recordJob && Record && slot && ownsRawRange && owner && publishedSnapshotIsCurrent()
&& hasServableDeferredRecordView() && isAudiblePlaybackActive() && hasBoundRecordReturnWindowForPendingJob()
&& !coverageBuildNeedsService()`):

- **every guard term holds** (`ownsRaw`, `servableView`, `audible`, `rootCurrent`, `bound` ON, `buildNeedsSvc` OFF) — so the
  current-root handoff block **is entered** on the board; the role is not blocked by a missing precondition;
- inside it, the "complete same-revision copy" bridge does **not** apply (`servedCopy` OFF) and the served view is **not**
  the current root's own window (`servedIsRoot` OFF) — the served reader is the frozen Capture view, as the masks said;
- and the one term that ends the story is **`revEq = 0`**: the retained demand's `source.playbackRevision` is **not** the
  loop's current revision, so `committedPlaybackRetryCanProgress`'s final equality refuses the retry outright.

**So the named defect is a stale retained identity, not a blocked path and not a wait.** The handoff raises (or keeps)
a Committed demand whose `source.playbackRevision`/`snapshotIdentity` were captured **before** the root publication, and
nothing refreshes them, so the retry gate treats a demand for the current root as a demand of a vanished revision — the
same shape as the stale terminal reason fixed above, but for the *identity* fields instead of the *reason*.

**Next fix (narrow, and the last unproven hop):** when the retention re-raises a Committed demand while the current root
is published, its `source` must be **refreshed from the live root** (revision, generation, published-snapshot identity)
before it is retained — the demand is *for* the current root by construction, so a stale stamp is never the truth. The RED
is constructible natively: retain a Committed demand with the pre-publication revision, publish the root, and require the
retry gate to progress and the root-backed successor to be served.

## The stale-identity fix lands and is **confirmed on the board**; the role is the next blocker (2026-10-18)

RED first: `test_a_stale_retained_revision_does_not_refuse_a_demand_for_the_current_root` retains the Committed demand with
the pre-publication revision and failed as `rootBacked=0 demandRev=0 loopRev=1 pending=1` — the board's `revEq = 0`
exactly.

**Fix:** `Track::refreshRetainedDemandIdentityForSlot` re-stamps a retained Committed demand's identity fields (revision,
generation, published-snapshot identity, loop length) from the live root, because such a demand is *for* that root by
construction; its reason, position and coverage are untouched. It is called from the **per-turn lane service**
(`serviceRollingPlaybackWindow`) rather than from the owner turn — a refresh placed behind holder selection never runs in
the state that needs it, because a demand with a stale identity is exactly one the selector reports as unable to
progress. RED → GREEN, suite 10/10, full native **3,309 / 7 skipped / 3,302 succeeded / 0 failed**, both builds inside
the floor.

**Confirmed on the board:** run `captures/i5-rolling-base-2bar-14` (HEX `850a8592…`) reads `gates = 50956` with **`revEq`
ON** (it was `50948`, `revEq` OFF, in run 13). So the retained demand now carries the current root's revision.

**And the stall's next blocker is exposed by the same row:** `roleFree` still OFF and `servedCopy`/`servedIsRoot` OFF, mask
110. With the demand now identity-current, it still cannot be served because **no view role is free** while the ready role
holds a complete successor that is neither the current root's window nor a copy the reader is serving, and the
current-root handoff never reaches its own `retireSupersededReady` site — the retained-demand branch returns into
`stepCommittedPlaybackRetry` first, where `coverageRoleAvailable()` refuses.

**Next fix (narrow, and now precisely placed):** in that retry path, when the root is current, the bound job is owed a
Committed successor, and the ready role holds a complete successor that is **not** the current root's own window, retire
it as superseded so the role frees — instead of refusing on role availability. `RuntimePlaybackViewLifecycle::
retireSupersededReady` is the existing operation; E2, the hold, accepted-stop gating and identity rules stay untouched.
The native RED is constructible from this row (complete non-root successor parked in ready + role unavailable).

## The role hypothesis is tested and rejected as *the* blocker — the refusal is one level lower (2026-10-18)

`test_a_redundant_frozen_successor_does_not_hold_the_role_forever` builds the board's shape with an **identity-current**
demand: the served reader is the job's frozen window, a redundant complete Capture-origin successor sits in the ready
role, and the demand is Committed for the current root. It fails, and the failure detail is the useful part:

```
rootBacked=0  roleFree=1  cur=2  ready=255  build=0  retained=1
demandActive=1  demandReason=7 (SourceNotCurrent)  prep=0  buildActive=0  needsSvc=0  ownsSrc=0  pagedActive=0
```

Two facts follow, and they redirect the work again:

1. **the role does free** (`roleFree=1`, `ready=255`) — so "the ready occupant holds the role forever" is not what this
   code does, and the retirement change I had scoped is **not** the fix;
2. with the identity now current and a role free, the demand is **still** stamped `SourceNotCurrent`, and the only writer
   of that reason in this state is `LoopPlaybackRuntime::stepCoverageQualification`'s **Denied** branch — i.e. the
   **source qualification** of the retained source is refused, and the source is retired.

So the chain is: demand → qualification start → qualification **denied** → reason `SourceNotCurrent` + source retired →
demand retained → repeat. That is the next and last named term, and it is a read/qualification question (why the source
descriptor read is denied in this state), not a role, identity, reason or wait question.

**No production change shipped for this step**: the scoped role retirement is rejected by the RED above, and shipping it
would be the fifth inert patch of the session. The case is registered as a **deliberate RED** pinning the sequence, with
full native at **3,311 cases / 1 failed (this RED) / 7 skipped / 3,302 succeeded**.

## The qualification denial, enumerated — six named checks, capacity ruled out (2026-10-18)

`LoopPlaybackRuntime::stepCoverageQualification` maps a denied qualification to `SourceNotCurrent` and retires the source.
Reading the denial to its source, `RuntimePlaybackViewFill::step` (the `SnapshotWindow` scope, which is the root-backed
route) returns `Denied` on exactly six conditions:

1. `destination_->published` — the destination role is already published;
2. `!(destination_->source == expected_)` — the candidate is not the source the qualification was begun against;
3. `destination_->storage.eventCount != copied_` — the driver's cursor and the destination's count disagree;
4. `snapshotSource_->idle()` — the snapshot traversal was never begun;
5. `snapshotSource_->retiring()` — the traversal is being released;
6. `snapshotSource_->traversalGeneration() != baselineGeneration_` or `readUnits() != cursor_` — the traversal moved under
   the driver.

**Ruled out in this state:** the fixed event capacity is not the constraint — `kEventsPerView`/`kWindowAdmissionLimit` are
**2048**, and this take's committed rows are ~160 (`32 notes per bar over 2 bars`, one row per event), so neither the
whole-loop nor the one-bar window can overflow `append`.

**The probe was then run, and it corrected the attribution again.** A one-byte denial code was added to the fill (set at
each `Denied` return, forwarded by the build) and read from the reproducer: it reported **`denial = 0`** — the
`SnapshotWindow` fill **never refuses**. The same dump shows `coveragePreparation = Idle` (0 in
`{Idle, Counting, AwaitAdmission, Building, Retiring}`), which means `stepCoverageQualification` is **not called at all**:
the state never reaches `Counting`. The `SourceNotCurrent` stamp on the demand is therefore a *leftover* from an earlier
attempt, and the real fact is that **nothing restarts the preparation** for this slot — the owner turn's
`stepCommittedPlaybackRetry` is not driving it (holder selection does not offer a progressable holder for it).

The one-byte code was **reverted**: the fill is a member of `LoopPlaybackRuntime`, whose size is **pinned** at 7,880 B, so
the byte broke the static assertion (a deliberate pin, not a budget to nudge). Its answer is recorded here, which is what
the byte was for.

**So the last open question is holder selection, not qualification:** why does the lane never offer this slot as a holder
the owner turn may serve, in a state whose demand is positioned, identity-current, role-free and unblocked by every other
term measured? That is the next probe, and it is one predicate (`fillPlaybackHolders` / `selectPlaybackHolder`) with the
same read-only reporting treatment.

**A case built to manufacture this state (`test_a_redundant_frozen_successor_does_not_hold_the_role_forever`) was removed
rather than kept as a RED**: its demand was hand-assembled and unpositioned, so it measured my construction rather than
the board. Keeping it would have been a failing test that proves nothing. The two genuine fixes from this session (stale
terminal reason, stale retained identity) keep their REDs, and the identity one is board-confirmed.

## The offer path also resolves natively — so the remaining difference is board-only (2026-10-18)

`test_a_committed_demand_is_offered_progress_after_the_frozen_handoff` builds the board's state with a **positioned**,
identity-current Committed demand, the frozen successor adopted into `current` and a second complete Capture successor in
`ready`, then drives the production owners. It **passes**: the offer's DEC-136 re-stamp block (which is gated on
`qualifiedFrozenRecordCaptureSourceForSlot`) is not needed here because the retry path itself makes progress, and the
root-backed successor is produced and served.

That closes the last predicate-level question this harness can ask. Every state I can construct from the board's own
measurements — root current, frozen reader served, complete Capture successor in `ready`, positioned identity-current
demand, no stale reason, free role, audible transport — **resolves** natively once the deadline clock is qualified. So the
board's remaining difference is *not* in these predicates; it is either in what the board's clock/epoch does across the
handoff (the harness stubs it and is driven synthetically, so its transitions are smooth by construction) or in real
storage/DMA timing that no fixture reproduces.

**Therefore the next probe must be board-side, not native:** report the *offer* for the slot — `holder.eligible`,
`holder.canProgress`, `hasDeadline` and the lead — on the same `hold_lane` row (its `length` field still has room). One
board occurrence then says whether the lane declines to offer the slot or offers it and the owner turn does nothing, which
is the last distinction this session could not make from the desk.

**Kept:** the three new controls from this stretch (observed-state, stale-revision, offer-path) are green and pin the
invariants; the two genuine fixes (stale terminal reason, stale retained identity) keep their REDs, and the identity one
is board-confirmed (`revEq` OFF → ON). Full native **3,310 cases / 7 skipped / 3,303 succeeded / 0 failed**; both builds
inside the floor.

## The offer is healthy; `revEq` flips between runs — two writers, one of them not ours (2026-10-18)

The offer is now packed into the `hold_lane` row's `length` (bits: 0–2 rolling phase, 3 eligible, 4 canProgress,
5 hasDeadline, 6–11 lead in 16-tick units). Run `captures/i5-rolling-base-2bar-15` reads:

```
gates = 50948 (revEq = 0, roleFree = 0)
offer: phase=0  eligible=1  canProgress=1  hasDeadline=1  lead = 176 ticks
mask = 110      #STOP_STATUS … pending=1, phase=16, queue=1/4
```

Two findings, and one of them contradicts an earlier measurement:

1. **"The lane never offers the slot" is falsified.** The offer is `eligible`, `canProgress`, with a real deadline and a
   lead of 176 ticks — the dispatcher has a servable holder with an urgent promise, and the owner turn still does nothing.
2. **`revEq` is 0 here but was 1 in run 14.** The refresh added for the stale retained identity runs at the top of
   `serviceRollingPlaybackWindow`, so this means either that service did not run for the track in this state, or the
   identity is **re-written to a pre-publication revision after the refresh** — i.e. there are two writers of
   `pendingViewRequest_.source.playbackRevision` and only one of them is the live root.

**That second point is the sharpest remaining lead**: a non-deterministic currency term (OFF → ON → OFF across three
consecutive runs on the same build family) is a *race between writers*, not a predicate that is simply false. The next
probe is therefore to find the other writer: grep the write sites of `pendingViewRequest_.source` / the offer's re-stamp
block, and report which one ran (a one-bit "written by the offer re-stamp" flag on the same row would settle it).

**Note on the earlier conclusion:** the offer row does **not** refute the idea that the owner turn is the problem — it
narrows it to "the turn is offered and does not act", which is a different question from "the lane declines".

## The second writer is `retireCoverageSource`'s coalescing copy (2026-10-18)

Enumerating the writers of the retained demand (`pendingViewRequest_`) found the mechanism behind the `revEq` flip:

- `src/Track/TrackPlaybackRuntime.cpp::retireCoverageSource` — on every retirement of the coverage source it **coalesces
  the frozen obligation back** into the retained record: `if (!pendingViewRequest_.active) pendingViewRequest_ =
  frozenViewRequest;`, a **whole-record copy that carries the frozen request's source identity** — i.e. the
  pre-publication revision. It also copies `deadlineTick`/`work.admittedTick` from the older attempt in the compatible
  branch.
- `Track::refreshRetainedDemandIdentityForSlot` — the fix added earlier, which re-stamps that identity from the live root
  at the top of each lane turn.

So the two writers alternate: the refresh writes the live revision at the start of a turn, and a retirement during that
same turn's work writes the frozen (pre-publication) revision back. The gate is then evaluated against whichever ran last,
which is why `revEq` read **OFF → ON → OFF** across three consecutive runs on one build family. That is a race between
owners, not a predicate that is merely false — and it is the first fully mechanical explanation of the non-determinism.

**The fix this names (next increment, RED first):** the coalescing copy must not resurrect a stale identity. Either
(a) `retireCoverageSource` keeps the *newer* identity when it merges the frozen obligation, or (b) the refresh runs again
**after** any coalescing (i.e. the refresh belongs where the record is final for the turn, not only at the turn's start).
(a) is preferable: it keeps one owner for the identity rule and leaves the coalescing copy responsible only for the
position it is entitled to keep. The RED is constructible natively — retain a Committed demand, retire the coverage source
so the frozen record coalesces back, and require the retained artifact to still name the live root.

## Correction before the fix: the coalescing copy **installs**, it does not overwrite a newer identity (2026-10-18)

Read in full, `LoopPlaybackRuntime::retireCoverageSource` does two different things and only one of them is the copy in
question:

- `if (!pendingViewRequest_.active) pendingViewRequest_ = frozenViewRequest;` — a whole-record copy **only when the
  retained record is inactive**, i.e. when there is no newer retained identity to overwrite. It *installs* the frozen
  (possibly pre-publication) identity where there was none.
- the `else if (…)` compatible branch copies `deadlineTick`, `work.deadlineTick`, `work.admittedTick`, `requestedPhase`,
  `positionInvalidated` and `countedMisses` — **position and cost only, never `source`**. That branch cannot overwrite an
  identity at all.

So the mechanism is narrower than stated: the coalescing does not clobber a newer retained identity; it **installs an
older one** when the record was empty. That still matters — the installed identity is what the retry gate compares for
that turn — but it changes the fix, and it also means the OFF→ON→OFF sequence needs a second explanation (an install can
only happen when the record is empty, i.e. after the gate consumed/cleared it).

**Why the obvious ownership fix does not work as described.** "Preserve the newer/live identity" is not expressible at
this owner: when the branch runs there *is* no live identity in the record, and this class cannot see the published root.
Leaving `source` blank would make the record fail the gate's `sourceKind == CommittedSnapshot` test outright; leaving the
frozen identity is what it does today. The only two coherent shapes are:

1. **Defer the identity**: install the frozen obligation with its position and cost but leave the record marked as needing
   an identity refresh (a flag the track's per-turn refresh honours), so no gate sees a stale identity as current; or
2. **Refresh after, not before**: run the identity refresh at the *end* of the lane turn as well as the start, accepting
   that one turn may judge a stale stamp.

Neither is a one-line change, and choosing between them needs the discriminating measurement this session has not been
able to make: **does the installed identity ever reach the gate while the root is current?** The `hold_lane` row can
answer it — the demand stamp (`why`) is already reported next to the offer, and the two are taken ~20 µs apart.

**No code changed this round.** Shipping either shape without that measurement would be the sixth inert patch of the
session; the correction above is the deliverable, and it is what keeps the next attempt from being built on a mechanism
the code does not implement.

## The cached-revision gate is fixed (RED + GREEN) — and the board still stalls on the **role** (2026-10-18)

**RED:** `test_the_retry_gate_accepts_a_committed_demand_whose_cached_revision_is_stale` asked the gate directly with a
Committed demand of the current root carrying a pre-publication cached revision: `canProgress=0 demandRev=0 loopRev=1
rootCurrent=1` — exactly the board's `revEq = 0` at an instant when the offer names the slot progressable.

**Fix (narrow):** in `committedPlaybackRetryCanProgress`, for a **Committed** demand the cached identity is a *cache*, not
the truth — the board's recorded sequence shows the record flipping active/inactive inside a turn (so a start-of-turn
refresh misses it), and the build reads the live root through `beginCoverageQualification` anyway. When the published root
is current, a Committed demand is for it, whatever its stamp says; a genuinely foreign demand is refused by the
source/generation/coverage terms above. RED → GREEN, suite 12/12, full native **3,311 / 7 skipped / 3,304 succeeded /
0 failed**, both builds inside the floor.

**Board: unchanged.** Run `captures/i5-rolling-base-2bar-16` (HEX `ef8b97ee…`) is behaviourally identical to run 15 — same
`gates = 50948`, same stamps (`96`, `15`, `96`, `21`, `15`), same `#STOP_STATUS … pending=1, phase=16`.

**And the row corrects an earlier reading of mine:** `roleFree = 0` on the board — `coverageRoleAvailable()` is **false**,
with the ready role holding a complete successor (mask bit3). So on hardware the build cannot start for want of a role,
while my native fixture frees one (which is why the earlier role case passed and was withdrawn). The **role occupancy is
real on the board** and it is the term that now stops the chain: `beginCoverageQualification` refuses without a role.

So the next fix candidate is the one the owner's earlier review proposed and that the desk could not validate: **retire a
non-adoptable complete successor from the ready role when the root is current and the bound job is owed a Committed
successor**, so a role frees. What is missing is not the idea but a *native* reproducer of the occupancy — and that is
where the effort must go next, because two board runs now agree on it.

## The role argument: fixed, RED-proven — and it *does* change the board, without finishing it (2026-10-18)

**RED, and it is the cleanest of the session** — the fixture printed both answers side by side:

```
canProgress=0  roleAvailable=0  roleAvailableKeepCapture=1  readyComplete=1  bound=1
```

`coverageRoleAvailable(false)` refuses whenever a **complete** successor sits in the ready role — by design — and the
`keepCaptureReady` parameter exists exactly so the DEC-136 private route may keep a **Capture-origin** successor while it
builds the Committed one (it still requires that successor to be unpublished, Capture-origin, with the build role distinct
from it). The retry gate called the default, so for the one shape the route exists for the role looked unavailable.

**Fix:** `committedPlaybackRetryCanProgress` now passes `privateRootOwed` as that argument. RED → GREEN, suite 13/13, full
native **3,312 / 7 skipped / 3,305 succeeded / 0 failed**, both builds inside the floor.

**And unlike the previous four candidates, this one moves the board.** Run `captures/i5-rolling-base-2bar-17`
(`4a18287a…`) emits **71 receipts** where run 16 emitted 17, and it reaches a state the board has not shown before:
`retire_hold mask = 239` (root current, the job's frozen window **served**, ready complete, loop match, start = playhead,
frame serving) interleaved with `e2_boundary mask = 207` — i.e. the hold now sits in the fully-formed DEC-136 state. The
stall remains (`#STOP_STATUS … pending=1, phase=16, queue=1/4`, `refused=track_work`), and the demand stamp in that state
reads `why = 96` — **no active demand** — then reappears as `15` / `21` later.

So the chain is now: the role is no longer the blocker; the hold reaches the complete-successor state; the retained demand
is **empty exactly when the state is right**, and stale when it is re-installed. That is the next and much narrower
question: what consumes the demand while the DEC-136 state is formed?

## Run-to-run variance is now measured, and the frozen record is empty when it matters (2026-10-18)

The frozen request's own state now rides the `hold_lane` row's `result` (bit0 active, bit1 CommittedSnapshot, bit2 its
identity names the live root, bit3 positionInvalidated, bits 4-7 its reason) — the pending stamp alone cannot say whether
the chain moved or stopped, because `beginCoverageQualification` **freezes** the retained demand and clears the pending
record.

Run `captures/i5-rolling-base-2bar-18` (same build as run 17) read **one** row:

```
mask = 110   roleFree = 0   frozen: active = 0  committed = 0  liveRev = 0  invalidated = 0  reason = 0
#STOP_STATUS … state=OVERDUBBING, pending=1, phase=16, queue=1/4
```

i.e. it never reached the `mask=239` state run 17 spent 71 receipts in, and at the sampled instant **nothing was in
flight at all** — no pending obligation, no frozen obligation, no build. Two runs on one build therefore differ in *how
far* the chain gets before stopping, which is consistent with everything measured so far: the stall is a **race**, not a
deterministic predicate failure.

**That is where this session's desk-side work ends, and it is the honest boundary:** three real defects are fixed with
REDs (one board-confirmed), every predicate that can be named from the desk has been measured or falsified, and the
remaining difference is timing-dependent. Continuing to reason from single runs would keep producing candidates that
cannot be discriminated; the next step needs either a run-to-run instrumented campaign (several runs, per-turn trace of
the three records) or a decision to bound the route differently — both of which are the owner's call, not a guess this
agent should keep making.

## Five-run campaign on one image: the illegal ownership transition is identified (2026-10-18)

Owner-approved route 1: a **fixed-image 5-run campaign** with **transition** instrumentation rather than more end-state
predicates. Image pinned before and after the whole campaign (`sha256 28479ac7…`, unchanged); all five runs stalled
identically (`INCONCLUSIVE_CAPTURE`, `second overdub transition not confirmed`, `refused=track_work`).

The instrument is one transition-cadenced row (`win_frozen_readiness` with `holder=transition`, label `owner_trace`) that
reports the **ownership set** of the coverage work — `pending` (`pendingViewRequest_`), `frozen` (`frozenViewRequest`),
`build` (`viewBuild`) and `job` (the Record `CaptureStopJob` still pending) — and raises a flag when the job is pending
while **none** of the three records owns the work. It emits only when the set changes, so a row is a transfer.

Run 1's trace around the stall (all five runs agree within milliseconds; row counts 59–62):

| time | owners | flag |
|---|---|---|
| 28.630 s | `pending` | — |
| 28.641 s | `pending+frozen+build` | — |
| 28.668 s | `pending+frozen` | — |
| **29.517 s** | **`job` alone** | **ILLEGAL (no owner)** |
| 29.529 s | `build+job` | — |
| 29.576 s | `job` alone | ILLEGAL |
| 30.735 / 30.877 / 31.006 / 31.289 s | `build+job` ↔ `job` | alternating |

**The first transition that creates the ownerless state is `pending+frozen → job` at ~29.5 s**, i.e. as the Record job
becomes pending, **both** the retained demand and the frozen obligation are released with **no satisfaction or retirement
recorded** for that release. After it, the chain only alternates between "a build exists" and "only the job exists", which
is exactly the shape of a stall that never resolves.

**That transition is the native RED target** (per the owner's instruction), and it is small: the Record-stop commit path
clears the retained demand (`TrackCaptureStopCommit.cpp` sets `pendingViewRequest_ = {}`) — executing that while the frozen
obligation is already empty, with the job pending, is the illegal handoff. The fix must keep an owner across that edge
(retain the demand, or record the satisfaction that lets the job retire).

**Not changed:** the lane-ineligible / DEC-136 contract, as instructed. No production change was made for the campaign
itself; the only addition is the transition row, which fits (`padding 856 ≥ 840`).

## Correction to the campaign verdict: that transition **is** excused by the criterion (2026-10-18)

The owner's definition was explicit — `job pending && !pending && !frozen && !build` is the illegal state *"unless the same
transition records satisfaction/retirement"*. The campaign rows satisfy that exception, so my previous verdict was wrong:

- at the flagged instant (`#CAP,29517269`, owners = `job`, flag 128) the run carries **`RECS,stop` ×1 and
  `Recording stopped` ×1** — the Record stop committed (`SC_REC_STOP`), which *is* the recorded satisfaction;
- the very next row is `win_gate … x=win_track_not_playing … od=1 …`, i.e. the lane is **deliberately** excluded while the
  job owns the work — the designed DEC-136 exclusion, not a lost owner;
- and the native control written for the edge (`test_the_record_stop_commit_keeps_a_retained_committed_demand`) **passes**:
  `commitRecordStop`'s matching branch only clears the **live-capture** recipes it actually supersedes, so a retained
  `CommittedSnapshot` demand survives the commit. (The branch clears both records only when the live one-bar recipe matches
  the job; the fallback already preserved Committed demands.)

So the Record-stop commit handoff is **sound**, no fix is owed there, and the "illegal transition" was my misreading of a
legal, recorded handoff.

**What the trace still shows and has no explanation yet:** after that handoff the owners alternate `build+job ↔ job` about
three times (~29.5–31.3 s) — a build is created and dropped repeatedly — and the final state is **`pending+job`** with no
`frozen` and no `build` (34.6 s onward, in 5/5 runs). That last state is *not* ownerless: the retained demand exists. It is
the "owner exists, nothing acts" shape measured before, now with its transition history attached, and it is the next
target — a demand that is re-installed and never served while the job waits.

**Kept:** the new control (commit edge preserves a non-live-capture demand) joins the other six. Full native **3,313 /
7 skipped / 3,306 succeeded / 0 failed**; both builds inside the floor.

## Reproduce

```bash
# build where the harness reads the pin (the configured build_dir), then flash exactly that file
pio run -e teensy41-capture-serial-window-qualification
pio run -e teensy41-capture-serial-window-qualification -t nobuild -t upload
shasum -a 256 ~/Library/Caches/pio-build-250513-215524-teensy41/teensy41-capture-serial-window-qualification/firmware.hex

.venv/bin/python scripts/host_midi_hitl.py run --preset base --track 8 --loop-slot 4 --midi-channel 8 \
    --build-env teensy41-capture-serial-window-qualification --boot-reset --out-dir captures/<rolling>
```

## Task 3.15h: the build-drop cycle, measured — **there is no release site, and no build is dropped** (2026-10-18)

Instrumentation added for this question only (`build_drop`, a `SC_FROZEN_WINDOW_READINESS` row so no new capture symbol and
no RAM1 cost): every release site (`applyPlaybackViewTransition`, `cancelPlaybackLaunchForSlot`,
`cancelRollingPlaybackWindow`, the three `retireCoverageSource` edges, both `discardCoverageBuild` source-superseded edges,
the late-completion discard, and the DEC-136 root-return hold expiry) probes the builder **before** it releases
(`start`: active, `nextGrain()`, coverage-owned, completed-destination, live-borrow, qualified/ordered counts), then emits
the reason (`adoptMask`), the owner set afterwards (`length`, same bit set as `owner_trace`), and the outcome flags
(`adoptResult`: active / progressed / window serving / successor prepared / frozen / pending / job pending /
**retryable-no-progress**). Two reasons exist for the transition no site owns: `completed_unpublished`
(`active()` is false for `Phase::Complete`, so a finished build leaves the owner set without any release running) and
`released_without_site`.

Two runs (`captures/i5-builddrop-01`, `captures/i5-builddrop-02`, image `d849d755…`, qualification padding **856**):

- **One** release-site row in the whole first run (`demand_invalidated`, deadline expired, a window serving and a
  successor prepared — a legal handoff, `retryable-no-progress` clear). **Zero** release-site rows in the stall region of
  the second run. The premise "a site drops the builder and the work returns" is **falsified by measurement**: none of the
  ten instrumented sites fires during the stall.
- The `build` bit that alternates in `owner_trace` (`7 ↔ 3` = `pending+frozen+build ↔ pending+frozen`, every ~85 ms) is
  the *live-capture* builder finishing a grain batch: `completed_unpublished`, phase `Complete`, qualified/ordered counts
  growing (2/2 → 128/…), demand `LiveCapture` + `AwaitingSourceQualification`. `Phase::Complete` is that route's designed
  resting state (`extendLiveCapture` accepts `Fill`/`Complete`), so this is extension, not a dropped build.
- The stall's own rows (41.9 s → 44.0 s, the 4-bar record slot, `loopLength 1536`, `owners = 8` = **job only**) read
  `completed_unpublished`, phase `Complete`, 48 qualified / 48 ordered, `windowServing` and `successorPrepared` set,
  and **`captureToken = 0`, `revision = 0`** — i.e. *no published Committed root at all*.

**Conclusion.** The build-drop cycle has no defect to fix: the builder completes, its delivery is prepared, and no release
site runs. What the rows do show is that the slot reaches that state with **no published root** (`snapshotIdentity = 0`)
and the Record job still pending, which is the same wall the run's own verifiers report (`INCONCLUSIVE_CAPTURE`, first
overdub never stops, `#HEAP_STATS,refused=track_work`). The obligation therefore never reaches the adoption boundary it
waits for — the DEC-136 hold's *audible* branch (`rootReturnHoldGrains = 0; return Pending;`, "a boundary can still come")
is where that wait is **unbounded**, and it is upstream of every drop site.

**Deliberately not written:** a native RED for the drop sites. The measurement does not show a dropped build, so a test
asserting one would encode a hypothesis the board contradicts. The next RED targets the measured state instead: a slot
whose coverage build completes and prepares a successor while the loop has **no published root** and the Record job is
pending must not park that job on an adoption boundary the audible branch never bounds.

**Gates:** focused suite 14/14; full native **3,313 cases / 7 skipped / 3,306 succeeded / 0 failed**; qualification
`392,360 + 856`, default `392,136 + 1,080`; the trace is `#ifndef`-guarded like every sibling row and compiles out of the
default image.

## The creation half: **no coverage build is ever created**, and the causal chain closes on a contract deadlock (2026-10-18)

Creation and publication are now traced on the same rows (`qualification_started`, `build_admitted`, `published_as_ready`,
`published_as_current`, `publish_ready_refused`, `publish_current_refused`), so the job → `build+job` → `job` lifecycle is
instrumented end to end. Run `captures/i5-buildmake-01` (image `d849d755…` + creation reasons):

- **Zero** `qualification_started`, `build_admitted`, `published_*` or `publish_*_refused` rows. The coverage builder is
  never created for this slot at all; every `build_drop` row is the live-capture extension cycle, and the whole
  `pending+frozen+build ↔ pending+frozen` alternation is that route's `Phase::Complete` rest. **There is no `job → build+job
  → job` cycle in the coverage path — the build half never happens.**
- The stall's own verdict (`win_gate`, 21 rows): `x=win_track_not_playing … pl=0, od=1, rw=1, sv=1, rt=1, rl=0, dm=1,
  bld=0, gr=0` — transport **not playing**, a **deferred overdub stop pending**, the **bound record-return window** still
  bound, root **current**, demand **active**, role **unavailable**, no builder, no grains.
- Run end state: `#STOP_STATUS state=OVERDUBBING,pending=1,phase=16` — the first overdub never leaves.

**The chain, every link measured:**

1. The Record job is pending (`pending=1, phase=16`) and its **frozen Capture window is the served reader**
   (`recordReturnBound=1`), so `stepDeferredRecordRetirement` returns `Pending` at `frozenCaptureServed`
   (`src/Track/TrackOverdubLifecycle.cpp`, "release waits for a complete root-backed window to take over this physical
   reader") — **unconditionally**, with no audible test and no grain bound.
2. The DEC-136 hold above it is bounded only when the slot is **not** audible; while audible
   (`isAudiblePlaybackActive()` = PLAYING **or OVERDUBBING or OVERDUB_STOPPING**) it reads "a boundary can still come" and
   resets the count under floor, so `kRootReturnStallGrainLimit` can never be reached during an overdub.
3. The next overdub's stop is **accepted and latched but deferred** (`od=1`, `hasPendingDeferredOverdubStop()`), because
   `serviceAcceptedInteractionTurn` yields behind the pending predecessor job (DEC-067 predecessor ownership).
4. Returning to PLAYING therefore requires that stop, which requires the job to retire.

So: **the overdub stop waits for the job, the job waits for a root-backed reader, the root-backed reader needs the window
lane, and the window lane requires `isPlaying()` — which is false exactly because the overdub has not stopped.** The two
contracts contradict each other in the OVERDUBBING state: DEC-136's audible branch assumes "a boundary can still come"
from the lane, and the lane's own entry (`!isPlaying() || hasPendingDeferredOverdubStop() ||
hasBoundRecordReturnWindowForPendingJob()` → `win_track_not_playing`) guarantees it cannot. No production change was made:
three repairs are plausible and they differ in externally observable semantics, so this is the decision boundary.

## Confirmed: `CaptureStopJob` carries **two** responsibilities on one `pending` flag (2026-10-18)

Owner's hypothesis examined against the structure and the run: **it holds, and it is the deadlock's root.**

- `PendingStopPhase` ends at **`RecordPublished`** (index 16, exactly the `phase=16` every stalled run reports). That phase
  means "the pass, its `RecordPassAdded` undo entry and the once-only persistence admission are published". So at the stall
  the **capture finalization half is already complete and immutable**.
- What remains after it is *not* capture work: `stepDeferredRecordRetirement` keeps returning `Pending` on reader terms —
  `frozenCaptureServed` (the job's frozen Capture window is still the served physical reader, line quoted above),
  the DEC-136 root-return hold, and `currentRootWindowServed`.
- Both duties are carried by **one** flag, `job.admission.pending`, and that flag is what the *next* interaction is refused
  or deferred on (DEC-067 predecessor ownership → `stop_job_pending`, and `hasPendingDeferredOverdubStop()` stays set →
  the window lane's `od` term). So a **reader** obligation blocks **slice closure**, which is the conflation.

**The slice (next, not implemented — this session's budget is spent on the diagnosis):**

1. Split the job's terminal state in two: `RecordPublished` ⇒ *capture finalization complete*, and a separate
   **reader obligation** record that carries the frozen window's identity (`rawRange`, `frozenCaptureContentGeneration`,
   `interval`, `loopId`/`slot`, `rootReturnHoldKey`/`rootReturnHoldGrains`).
2. At the `RecordPublished` edge, **transfer** that obligation out of the job and release the job's interaction role
   synchronously (one bounded step at the interaction edge): `admission.pending = false`, the deferred admission role
   frees, `stop_job_pending` no longer denies the successor, `hasPendingDeferredOverdubStop()` clears.
3. The transferred record becomes **coalesced deferred work** that cannot block slice closure: it keeps the frozen range
   alive, is served/held exactly as today (`serviceFrozenRecordWindow`, the DEC-136 hold, `releaseFrozenRecordWindow`), and
   releases when a root-backed window takes the reader — with the existing bounded expiry, now reachable because the
   transport can return to PLAYING/STOPPED.
4. **Invariants that must not change:** the frozen range is never released before a root-backed reader takes over *or* the
   existing bounded expiry fires; DEC-136's hold semantics are unchanged; the lane-ineligible contract is unchanged; the
   accepted stop still executes exactly once.

**RED for the slice** (the state is now measured, so it is constructible): a wrapped 2-bar take reaching `RecordPublished`
with its frozen window still the served reader, then a live overdub whose stop is **accepted** — the stop must not be
deferred by the predecessor once capture finalization is complete, and after it the reader obligation must still release
only through its own edge. Today the stop is deferred and the run ends `state=OVERDUBBING,pending=1,phase=16`
(`captures/i5-buildmake-01`).

## Slice design, decided against the consumer map (2026-10-18; not yet implemented)

The split "finish the interaction when the slice is immutable; keep presentation as coalesced deferred work" was costed
against the code before writing anything, and two tempting variants are **rejected**:

- **Move the frozen range into a per-slot runtime record** — `LoopEventStore::CaptureRange` is a non-copyable owning
  object embedding a whole `LoopEventStore` (chunk ids + metadata), so this is a heavy per-slot EXTRAM addition and the
  exact `static_assert(sizeof(LoopPlaybackRuntime) == 7880)` would have to be re-based. That is a budget re-base, not an
  ownership separation.
- **Narrow `hasPendingDeferredOverdubStop()`** from `count != 0` to "some slice-incomplete job" — superficially the whole
  bug, but it has **~20 consumers** with three different meanings (predecessor ownership in `TrackOverdubWrap`,
  playback-emission permission in `TrackPlaybackHotPath`, interaction refusal in `TrackOverdubLifecycle`), and one of them
  is the **retirement driver itself** (`TrackOverdubLifecycle.cpp:1386 if (!hasPendingDeferredOverdubStop()) return;`).
  Narrowing it would stop servicing the very job whose reader obligation is outstanding.

**Chosen: a ring-role change with zero new storage.** The ring already holds `CaptureStopJobs::entries[4]`; one entry
becomes the slot's **presentation holder** rather than a second queue:

1. At the `RecordPublished` edge (slice immutable, content published, persistence admitted), the job **hands its
   presentation role over**: it stays in the ring until its reader obligation completes, but the ring marks it
   presentation-only by **phase** (`PendingStopPhase::RecordPublished`) — no new field, no size change.
2. The interaction gates consult "a *slice-incomplete* job is pending", not "the ring is non-empty": the `stop_job_pending`
   refusal (`TrackOverdubLifecycle.cpp:3024/3044`), `TrackOverdubWrap`'s predecessor-ownership term, and the lane's `od`
   input all stop treating a slice-complete head as an interaction predecessor. The retirement **driver** keeps its own
   predicate (it must still run), so the reader obligation is serviced exactly as today.
3. **Coalescing by generation:** a newer committed slice for the same slot supersedes an older presentation obligation
   (the existing `supersedeDeferredStopJob` release path), so successive generations advance one obligation instead of
   stacking entries — the invariant the owner asked for explicitly.
4. Unchanged: the frozen range is still released only after a root-backed reader takes over **or** the existing bounded
   expiry fires; DEC-136's hold semantics; the lane-ineligible contract; the accepted stop executing exactly once.

**RED 1 (architectural boundary, not the deadlock):** given `RecordPublished`, immutable committed slice, frozen Capture
reader still served, no root-backed replacement, and an accepted overdub STOP pending → expect the Record interaction
ownership to read **not pending** (`!hasPendingDeferredOverdubStop()` with respect to interaction), the reader obligation to
remain represented, the accepted STOP to close the next slice, and the reader to converge later.

**RED 2 (coalescing):** reader obligation for generation G, then a new committed slice producing G+1 → expect **one**
presentation obligation targeting G+1, and not two stacked heavyweight finalization jobs.

Both REDs fail on today's tree for the measured reason (`captures/i5-buildmake-01`: `phase=16`, `pending=1`,
`state=OVERDUBBING`, lane excluded by `od`). The slice touches the ring roles, the three gate families above, the wrap
predecessor term and the hot-path emission permission, plus these two cases and a 2-bar device campaign — a coherent
multi-file slice rather than a late-session patch.

## The split's implementation path is falsified by the ring's own structure (2026-10-18; measurement only, no code changed)

The slice above was carried one step further: RED 1 was to be written, then the ring-role change. Reading the code
against the design shows the design's **step 2** ("release the job's interaction role synchronously … the deferred
admission role frees") is not expressible, for two independent structural reasons, so the slice was stopped before any
production line was touched.

**1 — the ring is serviced at its head only.** `Track::processDeferredOverdubStopGrain` binds `captureStopJobHead()`
once for the whole grain and never re-resolves the ring ("no intermediate step may re-resolve the ring and pick up a
different job"); its Record arm dispatches on `stop.phase`. A successor stop admitted *behind* a presentation-only
holder is therefore never serviced — its slice can never close. Freeing the interaction **gate** does not free the
**service**.

**2 — one finalizer role per slot, which the presentation obligation itself holds.** `LoopPlaybackRuntime` has a single
`captureStopOwner`, and the serving/retirement/hold machinery keys on it:
`hasBoundRecordReturnWindowForPendingJob()` is `captureStopOwner != nullptr && captureStopOwner->pending`, the E2
frozen-reader route requires the Record job to be the head **and** that predicate, and both successor admission paths
deny on the same pointer (`beginDeferredRecordStopForSlot` → `finalizer_owner_held`;
`beginDeferredOverdubStopForSlot` → that term beside `hasPendingDeferredOverdubStop()`). Handing the pointer to the
successor re-keys the presentation and stops serving the frozen range; keeping it leaves the successor unreachable. A
second per-slot role is a new field in `LoopPlaybackRuntime`, which is size-pinned at 7,880 B with no re-base authorized.

## The harness cannot hold the board's state — measured, with two numbers (2026-10-18)

A temporary probe in `test_rolling_record_retirement` (removed; the suite is back to its unchanged **14/14**) measured
why the earlier fixtures keep completing a chain the board stalls in:

- **`lastTickInLoop` is `UINT32_MAX` at the Record stop** unless the slot is played back across a wrap first, so
  `stepDeferredRecordRetirement`'s S7.5b.5 block (`… && isAudiblePlaybackActive() && loop.lastTickInLoop != UINT32_MAX`)
  is skipped and the job retires almost at once:
  `[probe] at stop: overdubbing=1 pending=1 phase=16 queue=1 latch=1 owner=1 bound=1 lastTick=4294967295 playing=0 state=6`
  → `[probe] deferred turn=16 overdubbing=0 pending=0 queue=0 latch=0 state=5`.
- With the loop **wrapped** first (`[probe] wrapped: lastTick=608 pending=1 phase=16 bound=1 playing=1 state=5`), a live
  overdub started and served for two bars **stops being the published reader**, and the job retires again:
  `[probe] overdub served: pending=0 phase=255 latch=0 served=0 origin=0 … loopLen=0/1536 playing=0 state=6`.

The board's held state (`retire_hold` **mask 239**: served = the job's frozen window, `recordReturnBound=1`,
`pending=1, phase=16`) needs a **serving** frozen reader that survives an overdub start. That construction — not another
gate change — is the prerequisite for RED 1, exactly as the 2026-10-17 session recorded, now with numbers.

**Consequence for the slice, stated as the decision it needs:** the presentation obligation outliving the interaction
requires an owner answer on the finalizer role. (A) A second per-slot presentation role: one pointer field in
`LoopPlaybackRuntime` (EXTRAM, no RAM1 effect) and a measured size-pin re-base. (B) First — and cheaper — make the
harness able to build a serving frozen reader that survives an overdub start (test-only), so RED 1 and RED 2 exist
before either production shape is chosen.

## RED 1 landed: the board's held state is now native, and slice closure is required of it (2026-10-18; test-only)

`test_a_recordpublished_presentation_obligation_does_not_hold_the_successor_stop` (`test_rolling_record_retirement`,
rolling compiled in) constructs the measured state and nothing else — it changes no production behaviour:

1. a wrapped two-bar take (`raw > final`, bucket rounds to 1536) reaching **`RecordPublished`** through the production
   deferred owner, with the job still owning its sealed extent;
2. the session-exit root published and **current** while that job is pending;
3. the frozen Capture window built by `serviceFrozenRecordWindow` and **adopted** (`adoptPreparedPlaybackWindow`), so it
   is the *served* reader for the slot (asserted: `origin == Capture` and the token is the job's own);
4. the transport **audible but not playing** — a live overdub started on the same slot — and the reader crossing a wrap
   so `lastTickInLoop` is real (the S7.5b.5 block's own term);
5. the successor's stop pressed and **accepted** (DEC-078), then only the owners this state admits are driven: the
   clock-edge reader and the deferred owners. The window lane is excluded exactly as on the board
   (`win_gate x=win_track_not_playing`).

**It REDs**: `steps=4096 overdubbing=1 latch=1 pending=1 phase=16 queue=1 bound=1 state=6 lastTick=1424`. The accepted
stop is never driven while the immutable slice's job holds the interaction role — the board's
`state=OVERDUBBING,pending=1,phase=16,queue=1/4`, reproduced natively for the first time.

The three assertions are the architectural boundary, and they are what Phase A must satisfy: the latch is **consumed**,
the track **leaves OVERDUBBING**, and the reader obligation is **still represented**
(`hasBoundRecordReturnWindowForPendingJob()`), i.e. not dropped with the job. The case captures its verdict before
cleanup (a same-slot `clear()` supersedes the deferred stop) so the other 14 cases in the suite are unaffected — they
are all green, so the state is faithful and separate.

## Phase A's stated first step does not compile: `CaptureRange` is neither copyable nor movable (2026-10-18)

The approved shape was "add a per-slot presentation owner in EXTRAM and **transfer** the remaining reader obligation to
it at the `RecordPublished` boundary". The transfer is not expressible:

- `LoopEventStore::CaptureRange` declares `CaptureRange(const CaptureRange&) = delete`, `operator=` deleted **and** a
  user-declared destructor, so no move constructor is generated either; `std::swap`/`std::move` are equally unavailable.
  It is also a heavy object — it embeds a whole `LoopEventStore` (`storage_`, chunk-id vector, pool receipt).
- 26 sites touch `rawRange` (23 of them in `TrackOverdubLifecycle.cpp`), and the Loop's read borrow is keyed on the
  **admission object's address** (`borrowSealedCaptureRange(range, owner)` / `releaseSealedCaptureRangeBorrow(owner)`), so
  a new owner token has to be threaded through the borrow as well.

**The only shape that compiles, stated as the design decision it is:** the sealed extent must be **slot-owned from
admission** — the presentation record owns `rawRange` for the whole interval, `beginDeferredRecordStopForSlot` seals into
*it*, the job holds a reference to it while it works, and the Loop borrow is keyed on the presentation record's own
admission identity. Then the `RecordPublished` boundary needs no move at all: the job is popped (ring head advances), the
presentation record flips `active`, and the reader keeps being served from the same objects. That is a rewrite of the
Record finalization chain's source ownership rather than a field addition; it is the coherent slice the owner asked for,
and it is **not** started here rather than left half-applied.

**State of the tree after this session:** the registered RED is the only failing case (`test_rolling_record_retirement`
15 cases, 14 green); no production line changed; no build, upload, SD or device action; device on the pinned default
`08f43e45…`.
