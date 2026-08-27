# Snapshot Publication Timing Bugfix

## Overview

Resolve the capture findings as separate, verified root-cause stages. The active
implementation scope is the remaining 3.5/3.6 snapshot-admission and construction
verification work; diagnostic and boot follow-ups remain separately gated. Preserve
atomic publication: a candidate remains private until construction, queued passes,
wrap journals, and companion changes are complete.

## Evidence and boundaries

- `RuntimeTimingTelemetry::noteMidiInputEnter()` reports the interval since the
  previous `handleMidiInput()` exit; `midi_gap` is not an owner-duration field.
- `Loop::stepPendingSnapshotPublish()` correctly blocks final publication while
  construction or session-exit work remains.
- `main.cpp::runDeferredIdleMaintenance()` already permits a Snapshot turn for a
  stopped track with pending snapshot work while the transport clock runs.
- `MidiServiceDrain::allowSnapshotIdleStep()` still suppresses that Snapshot turn
  when `visualCacheNeedsFirstPaint` is true, even when pending session-exit work
  exists.
- `Track::processDeferredStoredMidiVerification()` performs a first-slice full
  `mergeActiveCapturePasses()`.
- `logPreparedDisplayRangeSample()` performs a full native gather/reconstruction
  validation pass and is capture-diagnostic work.
- `runDeferredLoadAndDisplayFrame()` owns the independent boot `load_frame` gap.

## Staged implementation

### Stage 1 — Session-exit snapshot admission

- Extend only the existing `MidiServiceDrain::allowSnapshotIdleStep()` policy so
  pending session-exit Snapshot work wins over the visual first-paint predicate
  in `STOPPED`. Do not duplicate the already-landed global exception in
  `main.cpp::runDeferredIdleMaintenance()`.
- State the invariant explicitly: `STOPPED` means active MIDI ownership has
  ended; it does not mean pending snapshot work is complete.
- Keep `Loop::stepPendingSnapshotPublish()` as the sole construction/publication
  owner and retain atomic candidate selection.
- Extend the pure `MidiServiceDrain` native policy test with the exact stopped,
  first-paint, pending-work combination. Keep the existing `Loop` take-survival
  test as the publication-order regression; do not create a new end-to-end native
  test architecture for `Track`/`main.cpp`.
- Verify the ordering on device: the candidate remains private, queued work and
  construction complete, the completeness gate passes, and only then does
  publication occur. Assert that resumed admission cannot make an incomplete
  candidate authoritative.
- Treat MIDI timing as an observation in Stage 1, not an unchanged-timing
  acceptance criterion. The construction reserve gate belongs to Stage 2.
- Run capture-serial build, native tests, and the existing HITL publication gate.

### Stage 2 — Construction admission cost

- First add capture-gated child timing around
  `Loop::startNextPendingConstruction()`,
  `Loop::startNextPassChangeFragment()`,
  `LoopSnapshotConstruction::step()`,
  `finishPendingConstruction()`, and final publication. The current
  `snapshot_idle` span covers the whole `stepPendingSnapshotPublish()` call and
  cannot identify the limiting operation.
- Keep the child timing capture-gated and quantify its measurement overhead.
  Interpret instrumented totals as diagnostic observations unless the overhead
  is demonstrated negligible or explicitly accounted for; normal firmware must
  compile out the probes.
- Reconcile those measurements with `ExecutionBounds.md` rows 21 and 22. The
  old 79 ms `snapshot_idle` observation is not the current representative
  class; `225659` attributes the 20.984 ms record-stop parent to discard and
  leaves the 20.016 ms session-exit start internally unresolved. Keep the
  rows as target goals, not verified implementation guarantees, until those
  work units satisfy their declared caps.
- Define the numeric `T_RT_reserve` measurement procedure and acceptance
  thresholds, including its measurement origin and deadline. `T_RT_reserve`
  means the measured runtime interval remaining for deferred maintenance after
  accounting for the current MIDI scheduling boundary; it is not the full
  `T_clock`, 60% of `T_clock`, or an already-consumed maintenance interval.
  The 60% of clock interval value remains an operating proxy, not a reserve
  contract.
- Identify the actual indivisible operation before selecting the fix.
- Extend existing persistent progress and journal-cap mechanisms, or the
  relevant existing construction cursor, so the identified operation has a
  demonstrated execution bound. Do not prescribe a membership-page operation
  until measurement establishes that it is the limiting unit.
- Prove all three contracts: admission stays within the remaining
  `T_RT_reserve`, the admitted unit itself has a demonstrated bound, and an
  overrun can yield and resume without losing correctness.
- Retain failed work for retry and keep the previous complete snapshot selected.
- Use native tests for structural caps, progress, retry, and atomicity. Use
  capture-serial device runs for the execution-time and deadline gates.

### Follow-up Stage 3 — Capture-only verification isolation

- This is a separate follow-up scope after active OpenSpec 3.5/3.6 closes; it is
  not authorized by the current snapshot phase alone.
- Make diagnostic work have no implicit admission into timing-sensitive
  production maintenance.
- Refactor or defer the initial full merge in
  `Track::processDeferredStoredMidiVerification()` so it cannot create a timing
  gap during active validation captures.
- Schedule `logPreparedDisplayRangeSample()` outside the timing-sensitive
  maintenance turn, or make it explicitly opt-in; do not introduce a separate
  diagnostic scheduler or owner.
- Preserve the existing materialization oracle and keep all diagnostic code
  capture-gated. Change when/where validation runs, not what it validates.
- Verify that normal builds execute none of this diagnostic work.

### Follow-up Stage 4 — Boot load-frame bound

- This is a separate boot investigation, not the open post-stop `4b.3`
  load/display investigation.
- Split or further cap `runDeferredLoadAndDisplayFrame()` work so boot loading
  cannot monopolize the interval between MIDI polls.
- Preserve `StorageManager` ownership of deferred persistence/load work and the
  existing display/load ordering.
- Keep this stage independent from the snapshot conclusions: boot
  `load_frame` timing is a separate problem from snapshot admission,
  construction, and diagnostic interference.
- Add or extend only testable load-budget policy coverage on host; validate the
  full `main.cpp` frame behavior on device.

## Architecture and delivery gates

- Execution ownership remains separate from admission ownership:
  `Loop` owns snapshot construction/publication, `Track` and
  `MidiServiceDrain` decide whether existing snapshot work may run, and
  `StorageManager` owns persistence/load work.
- No new manager, scheduler, snapshot authority, or compatibility owner.
- Post the architecture gate before each firmware stage that changes interval
  admission or indivisible work bounds.
- The current active OpenSpec scope authorizes only Stage 1 and Stage 2.
  Follow-up Stages 3 and 4 require their own active plan/phase scope and must
  not be checked off under tasks 3.5/3.6.
- Treat each confirmed root cause as its own verified local commit; do not bundle
  diagnostic-only timing work with snapshot correctness.
- Every stage must demonstrate a positive result and a non-regression result.
  Positive result means the targeted root cause no longer produces the observed
  interval or publication failure. Non-regression means previously established
  correctness, atomicity, timing, and deferred-work behavior remains intact.
- Update `CURRENT_WORK.md`, `PROJECT_STATE.md`, the active OpenSpec `tasks.md`,
  `ExecutionBounds.md`, and the relevant timing/validation plan as each stage
  ships.
- For firmware changes, run `pio run -e teensy41-capture-serial` first. RAM1
  local-variable headroom below 4000 bytes is a hard precondition failure and
  blocks HITL or stage acceptance. After RAM1 passes, run `pio test -e native`
  and the relevant HITL capture.

## Stage dependency

1. Stage 1 remaining-policy fix, native policy test, and device recapture.
2. Stage 2 child measurement and bound reconciliation.
3. Stage 2 implementation only after the measured indivisible operation and
   `T_RT_reserve` contract are defined, followed by device recapture.
4. Create a separate follow-up scope for Stage 3.
5. Create a separate boot scope for Stage 4.

Do not implement Stage 2 and Stage 3 in the same change; diagnostic isolation
must not hide the construction execution cost.

## Pre-implementation review — Stage 1

### Ready
- `Track::processDeferredIdleMaintenance` already passes `snapshotPending` into
  `MidiServiceDrain::allowSnapshotIdleStep()`.
- `main.cpp::runDeferredIdleMaintenance()` already admits a Snapshot turn for a
  stopped track with pending work while the transport clock runs.
- `Loop::stepPendingSnapshotPublish()` remains the sole construction/publication
  owner; Stage 1 does not add a second completeness gate.

### Resolved
| Topic | Decision |
|---|---|
| First-paint vs pending work | Pending session-exit, in-flight, or wrap-journal work wins in `STOPPED`. |
| No pending work | Empty dirty visualCache still wins (`231632`). |
| `main.cpp` exception | Do not duplicate; Stage 1 extends only `allowSnapshotIdleStep`. |
| MIDI timing | Observation only; `T_RT_reserve` is Stage 2. |

### Open before coding
1. Device recapture after upload — not a firmware blocker.

### Proceed?
- YES — implemented 2026-08-27.

## Pre-implementation review — Stage 2 child timing

### Ready
- `Loop::stepPendingSnapshotPublish()` already calls the five named operations.
- `appendSnapshotIdleCaptureLine()` already emits the parent `snapshot_idle` span.
- Existing pending-snapshot accessors show the Loop-to-Track read pattern.

### Resolved
| Topic | Decision |
|---|---|
| Child output | Named fields on the existing `snapshot_idle` line, not 50 ms `loop_rem` one-shots. |
| Nested fragment | `pass_change_us` is nested inside `start_us`. Exclusive leftover uses `start_us + step_us + finish_us + publish_us`. |
| Overhead | `probe_us` is one empty consecutive `micros()` pair per idle step. |
| Construction code | Unchanged in this slice. |
| Tier-A | `DIAG,snapshot_idle` is Tier-A so children survive ring pressure. |

### Open before coding
1. Device recapture after upload — not a firmware blocker.

### Proceed?
- YES — child timing implemented 2026-08-27. Construction-bound change waits for attribution.

## `T_RT_reserve` measurement procedure

`T_RT_reserve` is the remaining runtime interval for deferred maintenance after
the current MIDI scheduling boundary. It is not `T_clock`, not 60% of
`T_clock`, and not an already-consumed maintenance interval. The 60% clock
proxy stays an operating observation only.

| Item | Definition |
|---|---|
| **Origin** | Start of the admitted Snapshot idle turn (`snapshot_idle` work-window start `t − us`). |
| **Deadline** | Next timing-critical `handleMidiInput()` entry. |
| **Observed interval** | A per-turn correlation from the Snapshot idle start to the next timing-critical `handleMidiInput()` entry. The existing `DIAG,midi_gap` line is a five-second maximum since the previous input exit and cannot serve as this per-turn value. |
| **Deadline lateness** | `DIAG,late_clk` / `DIAG,late_on` / `DIAG,late_off` in the same window, from `notePlaybackServiceEnter`. |
| **Clock reference** | Nearest `#CAP,BPM` before the idle line: `T_clock_us = 1_000_000 × 60 / (BPM × 24)`. Use it only to name the scheduling period, not as the reserve. |
| **Per-turn reserve** | Candidate definition: time from idle-turn start until the next timing-critical `handleMidiInput()` entry. This is an observation until the deadline, acceptance ceiling, and overrun policy are explicitly defined; it is not the full observed `midi_gap`. |
| **Child attribution** | Exclusive children are `start_us`, `step_us`, `finish_us`, and `publish_us`. `pass_change_us` is nested inside `start_us`. `probe_us` is measurement overhead for one timestamp pair; five child timers cost about `5 × probe_us`. |
| **Acceptance after freeze** | Not defined yet. It must specify an enforceable ceiling, the relation between the observed interval and admission, and the overrun action. An overrun must yield and retry without publishing an incomplete candidate. |

Do not freeze a numeric reserve until the per-turn correlation and acceptance
policy are valid. The old 79 ms parent span is not a reserve. Rows 21 and 22
stay target goals rather than verified implementation guarantees until their
actual work units satisfy the declared caps.

## Stage 1 status

- Policy: `STOPPED` + `visualCacheNeedsFirstPaint` + pending snapshot work now
  admits the Snapshot turn.
- Native: `test_stopped_pending_snapshot_wins_over_first_paint`.
- Host gates: `teensy41-capture-serial` RAM1 locals **7008**; native
  **1487/1487**.
- Device recapture [`214832`](../../captures/session_20260827_214832.log):
  **not closed**. Completeness held (`journals=0`, `queued=4`, `root=0x0`).
  After `PLAYING -> STOPPED` the first-paint `VCACHE` (`notes=0`, `dirty=1`)
  and persist work ran; no `STOPPED` `snapshot_idle` appeared. Last PLAYING
  idle was still `pending=1`, `queued=4`, `construction=1`. Publication
  never reached `pending=0` with a root. MIDI timing is observation only.
- Device recapture [`215420`](../../captures/session_20260827_215420.log):
  **not closed**. Completeness held (`root=0x0` through journal drain and
  `queued>=1`). Last PLAYING idle at 87.733 s is `pending=1`, `queued=1`,
  `applied=1`, `construction=31`. `PLAYING -> STOPPED` at 87.802 s; capture
  continues past 104 s with persist and `idle_maint`, zero `STOPPED`
  `snapshot_idle`, and zero post-stop `VCACHE`. Last pre-stop cache is
  `slice_clean` `notes=166` `dirty=0`, so this hold is not the first-paint
  combination. Undo/redo are present during PLAYING (`82.421` / `84.047`),
  not after publication.
- Device recapture [`215845`](../../captures/session_20260827_215845.log):
  **not closed**. File starts mid-stream. Completeness held (`root=0x0`).
  Last PLAYING idle at 323.017 s is `pending=1`, `queued=3`, `journals=2`,
  `construction=28`. `PLAYING -> STOPPED` at 323.033 s; the hold continues
  past 452 s with persist and `idle_maint` (~300 µs after 327 s), zero
  `STOPPED` `snapshot_idle`, and zero post-stop `VCACHE`. Last pre-stop
  cache is `slice_clean` `notes=153` `dirty=0`. No undo/redo.
- Remaining admission hole (2026-08-27): first-paint policy is not the
  STOPPED miss. `runDeferredIdleMaintenance` only scanned `getActiveLoop()`
  and then serviced `sStoppedIdleTrackCursor`. STOPPED Snapshot turns now
  scan every slot and select a track that still has pending publication.
- Device recapture [`221054`](../../captures/session_20260827_221054.log):
  **Stage 1 closed**. `PLAYING -> STOPPED` at 195.648 s. First STOPPED
  `snapshot_idle` at +0.070 s (`pending=1`, `queued=3`, `journals=8`,
  `root=0x0`). Construction continued through the hold: journals drained at
  +18.054 s, then queued passes applied at +55.350 s and +70.683 s. The
  candidate stayed private (`root=0x0`) until +77.765 s, when one idle
  published `pending=0`, `root=0x70400ac8`, `root_rev=10`, `playback_rev=10`,
  `queued=0`, `construction=0`, `journals=0`. No earlier nonzero root. No
  post-stop undo/redo. MIDI timing remains observation only.

## Stage 2 status — child timing

- Capture-serial `snapshot_idle` now emits exclusive `start_us`, `step_us`,
  `finish_us`, `publish_us`, nested `pass_change_us`, and `probe_us`.
- `DIAG,snapshot_idle` is Tier-A. Native fixture covers that tag.
- Host gates: `teensy41-capture-serial` RAM1 locals **6976**; native
  **1489/1489**.
- Construction, publication, and admission owners are unchanged.
- Child durations now emit on a second `DIAG,snapshot_idle,child` line so
  the 191-byte capture ring no longer clips them. Host gates: RAM1 locals
  **6976**; native **1490/1490**.

## Stage 2 recapture — [`222401`](../../captures/session_20260827_222401.log)

- Child fields after `start_us` never appear. Every `snapshot_idle` line is
  191 bytes; `DebugSessionCapture` `kMaxCaptureTextBytes` is 192. `probe_us`
  overhead is not measurable from this file.
- 13551 `snapshot_idle` rows. Parent max is **16.252 ms**, not the 79 ms
  class.
- Start-attributed slices (`start_us` ≈ parent `us`): first-record live
  starts at 12.691 / 10.294 / 10.676 / 9.956 / 9.829 / 9.355 / 9.169 ms;
  STOPPED session-exit starts at 8.254 / 6.634 / 5.774 / 5.751 / 5.574 /
  5.457 / 5.029 ms. Those costs are `startNextPendingConstruction`, outside
  the classified `LoopSnapshotConstruction::step` unit.
- Record-stop PLAYING idle at 107.662 s is **16.252 ms** with
  `start_us=7070` and **9.182 ms leftover**. `maximumChanges=1` starts then
  returns, so the leftover is not `step` / `finish` / `publish`. Other child
  fields were truncated; the leftover owner is uninstrumented work inside
  `stepPendingSnapshotPublish` plus the post-return `snapshot_idle` emit.
- Slices of 2.7–4.3 ms with `start_us=0` are not start. Truncation prevents
  splitting `step` / `finish` / leftover.
- Completeness held: `root=0x0` until the final STOPPED idle at +38.487 s
  (`pending=0`, `root=0x7039436c`, `root_rev=7`, `queued=0`,
  `construction=0`, `journals=0`). No earlier nonzero root.
- Stage 1 hold: `PLAYING -> STOPPED` at 144.012 s; first STOPPED
  `snapshot_idle` at +0.056 s (`pending=1`, `queued=3`, `root=0x0`).
- After record start, construction-window `midi_gap` max is 101.216 ms
  beside the 12.691 ms first-record start. STOPPED `midi_gap` 57.128 ms
  sits beside a 62 µs idle, not a construction child. `late_clk=0`;
  `late_on` max 1.292 ms after stop.
- `T_RT_reserve` is not frozen. At BPM 120, `T_clock` is 20.833 ms; several
  start children exceed the 12.5 ms 60% proxy. Do not change construction
  bounds until a recapture with untruncated children names the 9.182 ms
  leftover and the `start_us=0` 2.7–4.3 ms class.

Next measurement slice: keep the same child timers and fit the
`snapshot_idle` payload inside 191 bytes, or emit a second short child
line. Do not add a new telemetry owner.

## Pre-implementation review — Stage 2 child line budget

### Ready
- `appendSnapshotIdleCaptureLine()` already owns parent and child emit.
- `kMaxCaptureTextBytes` is 192 (191 payload). [`222401`](../../captures/session_20260827_222401.log)
  clipped every parent line at `start_us`.

### Resolved
| Topic | Decision |
|---|---|
| Fit method | Parent line keeps the pre-child fields. Children move to a second `DIAG,snapshot_idle,child` line from the same function. |
| Pairing | Both lines share the same `#CAP` micros. |
| New owner | No. Same tag prefix stays Tier-A. |
| Construction | Unchanged. |

### Open before coding
1. Device recapture after upload.

### Proceed?
- YES — implemented 2026-08-27.

## Stage 2 recapture — [`224842`](../../captures/session_20260827_224842.log)

- Child-line budget **closed**. 12789 parent rows, 12789 complete
  `DIAG,snapshot_idle,child` rows, 0 truncated. Parent length 171–175;
  child length 94–100.
- `probe` is 0 or 1 µs (886 nonzero of 12789). Five timestamp pairs are
  at most a few microseconds; instrumented totals can be read as the
  named children.
- First-record live starts (4.0–5.4 ms) are `start` with `pass_change=0`.
- STOPPED/session-exit starts (5.5–5.8 ms) are `start` ≈ `pass_change`
  (`startNextPassChangeFragment`).
- The `start=0` 2.1–3.4 ms class is `step` (`LoopSnapshotConstruction::step`).
  Leftover on those rows is 8–30 µs.
- Record-stop PLAYING idle at 23.896 s is **12.438 ms**: `start=4880`
  (`pass_change=2225` nested), `step=0`, `finish=0`, `publish=0`,
  leftover **7558 µs**. That leftover is outside the five named children.
- `finish` max is 537 µs. `publish` is 0; the file ends before publication.
- Completeness held through the captured window: `root=0x0` on every row.
  `PLAYING -> STOPPED` at 61.929 s; first STOPPED idle at +0.068 s
  (`pending=1`, `queued=3`). Capture ends at 69.020 s still `pending=1`,
  `queued=2`, `construction=4`, `root=0x0`. This file does not close
  publication.
- After record start, `midi_gap` 260.228 ms at 13.867 s sits beside a
  48 µs idle, not a construction child. The 34.801 ms gap at 23.874 s
  sits beside the 12.438 ms leftover slice (`late_on=7054`,
  `late_off=6053`, `late_clk=0`).
- `T_RT_reserve` is not frozen. Named children max at 5.732 ms start /
  3.434 ms step. The 7.558 ms leftover is the largest exclusive piece of
  the longest parent. Do not change construction bounds until that
  leftover is named.

Next measurement slice: leftover probes are in tree on the existing
`DIAG,snapshot_idle,child` line (`enqueue`, `discard`, `emit`). Parent
`us` is stamped at the start of `appendSnapshotIdleCaptureLine`, so
`emit` is after that stamp and is not part of parent leftover. Exclusive
leftover is `us − (start + step + finish + publish + enqueue + discard)`.
`pass_change` stays nested in `start`. Recapture the record-stop PLAYING
class before changing construction bounds. Do not add a new telemetry
owner.

## Stage 2 recapture — [`225659`](../../captures/session_20260827_225659.log)

- Leftover probes **closed**. 31701 parent rows, 31701 complete
  `DIAG,snapshot_idle,child` rows with `enqueue` / `discard` / `emit`.
  Parent length 171–177; child length 121–132. 0 truncated child rows.
- Record-stop PLAYING idle at 30.164 s is **20.984 ms**:
  `discard=15262` (`discardPendingIfLoopLengthChanged`), `start=5704`
  (`pass_change=2810` nested), `enqueue=5`, exclusive leftover **13 µs**.
  This is the 224842 leftover class.
- That 15.262 ms is the two `reset()` calls, not the length check. The
  last RECORDING idle (30.101 s) still has `exit=0` / `inflight=1`, so
  discard is a no-op. After seal, `loopLengthTicks` is 9216 and the
  unpublished / in-flight objects still have the provisional live-record
  length, so `unpublishedCandidate.reset()` and `construction.reset()`
  run in one turn. Those are the last owners of the live-record
  `LoopSnapshot` graph and `LoopSnapshotConstruction` (`candidate_`,
  `parent_`, mutable span/event roots, apply state, change set). Each
  node is a slab `shared_ptr`; last-ref drop calls
  `snapshotSlabDeallocate` (`.flashmem`, `noinline`) and frees a 32 KB
  slab when `liveCount` hits 0. Last `snapshot_alloc` before discard:
  1906 nodes / 256084 bytes / 9 slabs (cumulative; counters do not
  decrement). `start=5704` is the rebuild after that destroy.
- Exclusive leftover after `enqueue`/`discard` is ≤ 39 µs on every paired
  row. `enqueue` max is 14 µs. `emit` max is 52 µs (after the parent
  `us` stamp). `probe` is 0 or 1 µs.
- First-record live start at 8.571 s is 5.355 ms `start` with
  `pass_change=0`.
- STOPPED session-exit starts remain `start` ≈ `pass_change`, now
  **19.996 / 19.968 ms** at 125.868 s.
- The `start=0` multi-ms class remains `step` (max 5.992 ms).
- Completeness held: `root=0x0` until the final `pending=0` idle at
  183.121 s (`root=0x70431844`, `root_rev=8`, `finish=671`,
  `publish=10`). No earlier nonzero root.
- Stage 1 hold: `PLAYING -> STOPPED` at 105.089 s; first STOPPED idle
  at +0.066 s (`pending=1`, `queued=4`, `root=0x0`).
- After record start, `midi_gap` 260.252 ms at 9.606 s sits beside the
  first-record window. The 36.415 ms gap at 34.610 s sits in the window
  that includes the 20.984 ms discard slice (`late_on=0`, `late_off=0`,
  `late_clk=0`).
- `T_RT_reserve` is not frozen. Named maxima: `start`/`pass_change`
  19.996 ms, `discard` 15.262 ms, `step` 5.992 ms. Do not change
  construction bounds until those named units are reconciled with
  `ExecutionBounds.md` rows 21–22.

## Proposed solution — session-exit length retarget (implemented 2026-08-28)

**Problem:** On session-exit the unpublished live-record tree and the
sealed `Loop::loopLengthTicks` differ. `discardPendingIfLoopLengthChanged`
then `reset()`s the last owners of that graph. [`225659`](../../captures/session_20260827_225659.log)
measured that destroy as `discard=15262` on the first PLAYING idle after
record stop. The notes already fit in the sealed length (ticks 0–9048
inside 9216). The mismatch is the provisional parent length from
`liveRecordNewParentLength`, not notes crossing the sealed end.

**Not this problem:** Overdub wrap journals. Those require a parent that
already has the correct length and only mutate notes open at an unchanged
loop boundary. Wrap and membership walks now read
`LoopSnapshot::loopLengthTicks_`. The journal cap is 32 changes.

**Not this slice:** Removing the +16 extra bars in
`liveRecordNewParentLength`. That room is the [`121754`](../../captures/session_20260826_121754.log)
/`DEC-045` fix so live RECORDING can append completed bars without
creating a new empty parent and replaying the settled prefix. Dropping it
without a sliced live-record length-grow reopens that rebuild (2611
RECORDING `snapshot_idle` steps, 17.7 s CPU) or stalls append when the
parent is too short.

**Model invariant (shipped):** `LoopSnapshot::loopLengthTicks_` is the
sole loop-boundary authority. `LoopSnapshotNote` stores identity, pitch,
velocity, start, and end only. Wrap detection, canonical spans, bar
cursors, and affected-bar estimates receive the snapshot root length at
the call site. Note duration is `startTick`/`endTick` for a linear note
and those ticks plus the root boundary for a wrap.

**Implemented direction**

- Owner stays `Loop::stepPendingSnapshotPublish` /
  `LoopSnapshotConstruction`. No new manager, scheduler, or wrap-journal
  path.
- `discardPendingIfLoopLengthChanged` first fit-checks
  `appliedLiveRecordCompletedTicks` against the sealed length and, when
  the applied range already fits, replaces only the unpublished /
  in-flight root length through `LoopSnapshot::withLoopLengthTicks`.
- `discardPendingIfLoopLengthChanged` still destroys the pending tree
  when the applied range does not fit or header allocation fails.

Keep +16 live-record room. A later, separate slice may replace it with
sliced live-record length-grow when the parent fills.

**Transition / bound**

- Session-exit now continues the unpublished candidate when the applied
  live-record range fits the sealed length. Architecture checkpoint
  question 2 is yes and was approved for this slice.
- The admitted unit is one header clone plus a cursor compare. A
  note-walk retarget remains unbounded and is not used.
- Non-fit still destroys the pending tree.

**Gates**

- Native fixtures:
  `test_session_exit_handoff_when_live_record_notes_fit_sealed_length`
  (provisional parent vs sealed 9216; notes fit) and
  `test_session_exit_discards_when_applied_ticks_exceed_sealed_length`.
- Device recapture of the same class: `discard` near 0 on that idle;
  candidate stays `root=0x0` until `pending=0`; no `121754`-style
  RECORDING prefix rebuild.
- Device recapture [`003015`](../../captures/session_20260828_003015.log)
  **closes** that discard gate: first PLAYING idle after record stop is
  `discard=47` (was `15262` on [`225659`](../../captures/session_20260827_225659.log)).
  `root=0x0` until `pending=0` / `root=0x703248cc` / `root_rev=1`.
  RECORDING used 2669 idles / 234 ms CPU, not a prefix rebuild.
- Do not freeze `T_RT_reserve` on this slice alone.

## Stage 2 remaining work — falsification review

Measurement of the named children and publication correctness are closed.
The following conclusions were tested against the code and
[`225659`](../../captures/session_20260827_225659.log). Stages 3 and 4 stay
out of scope.

| Item | Finding | Status |
|---|---|---|
| Session-exit length retarget | The 15.262 ms discard is closed on [`003015`](../../captures/session_20260828_003015.log): first PLAYING idle after record stop is `discard=47`. Snapshot-root length is the sole note-boundary authority. Session-exit clones one header when applied live-record ticks fit the sealed length; non-fit still destroys the tree. | Device recapture closed |
| `startNextPassChangeFragment` | The 20 ms start was `note_lookup`. [`005527`](../../captures/session_20260828_005527.log) drops that class: `note_lookup` max **857 µs**, `pass_change` max **3.219 ms** (was 18.293 / 21.050 ms on [`004119`](../../captures/session_20260828_004119.log)). Remaining pass-change cost is refill-dominated. Same-pitch scan remains content-sized. | Device recapture closed for the other-pitch walk |
| `LoopSnapshotConstruction::step` | The 5.992 ms observation does not justify a rewrite by itself. | No firmware change justified yet |
| `T_RT_reserve` | Per-turn measurement remains necessary; freezing the minimum observed reserve is not a valid guarantee. | Contract remains open |
| ExecutionBounds rows 21–22 | Intended goals remain valid, but the current implementation does not fully enforce them. | Update only with matching firmware |

Do not check off OpenSpec `3.5` / `3.6` until the reserve contract and
device gate are closed.

## Proposed solution — caller-scoped pitch lookup (implemented 2026-08-28)

**Problem:** On [`225659`](../../captures/session_20260827_225659.log) at
125.868 s, STOPPED session-exit idle is **20.016 ms** with
`start=19996` and `pass_change=19968`. That is
`startNextPassChangeFragment` inside `startNextPendingConstruction`, not
`step`. Row 22 already caps the fragment at 32 changes. The 20 ms is the
start call as one admitted unit.

**What that start does:** `appendNextPassChangeFragment` refills a
journal-capped batch (`CommittedEventRange::appendWindowBudgeted`), then
for each event calls `eventAlreadyInSnapshot` (one bar) and
`LoopSnapshot::findNote`. On a miss, `findNote` searches the hinted
pitch and then **every other pitch's bar pages and memberships**.
`appendNextPassChangeFragment` always passes the event pitch. A new
pass note is a miss at that pitch, so the 128-pitch walk runs for each
new note in the fragment. The parent at that idle already had
`snapshot_alloc` 6416 nodes / 31 slabs. Then
`LoopSnapshotConstruction` copies the parent header (`shared_ptr`
roots only).

**Not this problem:** `LoopSnapshotConstruction::step` (0 on that row).
Overdub wrap journals (same 32-change cap, different parent-length
contract). Discard / +16 live-record room.

**Caller-specific identity invariant**

The cross-pitch fallback remains required by generic
`LoopSnapshot::findNote`, but it is not required by
`appendNextPassChangeFragment` when processing committed record/overdub
passes:

1. `committedChunksForPass` returns only the committed record pass or an
   overdub pass. Note-edit rows are handled separately by
   `tryPublishNoteEditSnapshot` and `appendEditRowChange`.
2. Recording and overdubbing create incoming NoteOns through
   `appendCaptureEventWithResult`, which allocates a fresh `NoteId`.
   Their NoteOffs inherit the identity from the matching same-channel,
   same-pitch NoteOn.
3. Note-edit pitch changes intentionally preserve the selected
   `NoteId` while changing its pitch. That path therefore retains the
   generic cross-pitch lookup and is outside this pass-fragment change.

The applicable invariant is:

> A committed record/overdub pass event reaching
> `appendNextPassChangeFragment` cannot refer to a parent note at a
> different pitch.

The improved overdub ledger and incoming `[startTick, endTick)` span
support the same-pitch participant contract. The pass-specific proof
also depends on the capture-pass `NoteId` lifecycle and on keeping
note-edit replay outside this caller.

**Implemented lookup change**

Keep `LoopSnapshot::findNote` unchanged for note-edit and other generic
callers. `LoopSnapshot::findNoteAtPitch` searches only the event's pitch
and is used only from `appendNextPassChangeFragment`.
The existing `eventAlreadyInSnapshot` check remains unchanged; only the
parent-note identity test is caller-scoped.

This preserves the current publication transitions and the 32-change
journal cap. It removes the unrelated-pitch walk without changing
same-pitch identity semantics. It does not yet establish a complete
microsecond bound: a full scan of all bar memberships within one pitch
remains content-dependent and must be measured.

**Inner attribution (in tree 2026-08-28)**

Capture-gated inner timings on a third `DIAG,snapshot_idle,inner` line
from `appendSnapshotIdleCaptureLine`, paired by the same `#CAP` micros:

- `refill` — `refillPassChangeBatch`;
- `event_scan` — `eventAlreadyInSnapshot`;
- `note_lookup` — the current `findNote` operation;
- `change_build` — open-note tracking and change-vector construction;
- `construct` — `LoopSnapshotConstruction` creation.

Keep current lookup semantics and the 32-change cap during this
measurement. The lookup change follows only after the invariant and the
limiting inner cost are covered by the gates below.

<!-- Historical proposal superseded by the caller-scoped invariant proposal; retained for audit:
1. Confirm with capture-gated inner timings on the existing child line
   (`refill`, `find`, `ctor`) on one recapture of the same STOPPED
   session-exit class. Do not change construction until that recapture
   names the inner 20 ms.
2. If `find` owns it: add a pitch-scoped lookup used by
   `appendNextPassChangeFragment` (and the live-record
   `parent->findNote(note.noteId, note.note)` append path) that does
   **not** walk other pitches on a miss. `NoteId` is loop-scoped.
   Keep the current 128-pitch fallback only for callers that pass the
   default `pitchHint=0`.
3. If `refill` owns it: persist `appendWindowBudgeted` so one idle
   turn fills at most one existing window-budget slice, not an
   unbounded chunk walk to satisfy 32 events.
4. If `ctor` owns it: stop treating construction create as part of the
   same admitted start unit.

Owner stays `Loop` / `LoopSnapshot`. No new manager.

**Transition / bound**

- Pitch-scoped lookup does not change publication transitions.
- Splitting refill or ctor out of start **does** change the pass-change
  start transition; architecture checkpoint question 2 is yes for that
  split only.
- Do not lower the 32-change journal cap until the inner owner is named.

**Gates (when implemented)**

- Native fixture: the caller-scoped lookup does not visit other
  pitches; a note-edit lookup still finds a note stored at another
  pitch.
- Native fixture: record/overdub pass identities satisfy the stated
  parent-pitch invariant; note-edit pitch changes remain covered by the
  generic lookup.
- Device recapture of the 125.868 s class: `pass_change` drops with the
  named inner fix; completeness still `root=0x0` until `pending=0`. -->

Owner stays `Loop` / `LoopSnapshot`. No new manager.

**Remaining gate**

- Native fixture
  `test_snapshot_find_note_at_pitch_stays_on_event_pitch` covers scoped
  versus generic lookup.
- Device recapture [`005527`](../../captures/session_20260828_005527.log)
  **closes** the other-pitch walk: `note_lookup` max 857 µs. Completeness
  held (`root=0x0` through that file). Tail
  [`005819`](../../captures/session_20260828_005819.log) publishes
  `pending=0` / `root=0x70413fec` / `root_rev=10` at 125.041 s.
- Same-pitch membership scan and refill remain measured but not bounded.
- Do not freeze `T_RT_reserve`. Do not check off OpenSpec `3.5` / `3.6`.

## No justified `LoopSnapshotConstruction::step` fix

**Problem:** [`225659`](../../captures/session_20260827_225659.log)
`step` max is **5.992 ms** (`start=0` class at 109.704 s). Row 21
already caps one persistent-cursor bar and at most 8 membership pages.
That classification is a work cap, not a µs cap. The observation does
not by itself falsify the row-21 work cap or justify a rewrite.

**Finding**

Keep the construction code unchanged until a valid reserve contract
exists. If that contract proves the indivisible step too large, open a
separate bounded-step design; do not assume that splitting
`begin()` from `applySoonestBar` is the fix.

**Not this slice:** Membership-page redesign. Journal-cap changes.

**Remaining gate**

- Compare the 5.992 ms step against a valid per-turn reserve contract.
- Native one-bar / one-page progress coverage remains applicable; add
  coverage only if the construction unit changes.

## `T_RT_reserve` measurement — freeze rule rejected

**Problem:** Stage 2 cannot close the three contracts (admission within
reserve, admitted unit bounded, overrun yields and resumes) without a
numeric reserve. The 60% of `T_clock` value (12.5 ms at BPM 120) is an
operating proxy, not the contract. [`225659`](../../captures/session_20260827_225659.log)
has no per-idle remaining-interval field. `midi_gap` is a 5 s max since
the previous `handleMidiInput()` **exit**, not the reserve.

**Proposed measurement**

- Origin: `snapshotIdleStartUs` already taken in
  `Track::processDeferredIdleMaintenance` before
  `stepPendingSnapshotPublish`.
- Deadline: next `RuntimeTimingTelemetry::noteMidiInputEnter`.
- Add capture-gated `reserve_us` on the existing
  `DIAG,snapshot_idle,child` line: `deadline − snapshotIdleStartUs`.
  When the next enter has not been seen yet, omit the field or emit 0
  and pair on the following `midi_input` / enter sample. Do not invent
  a second telemetry owner.
- Recapture the [`225659`](../../captures/session_20260827_225659.log)
  class (record, overdub, STOPPED hold through `pending=0`).

**Falsification of the freeze rule**

Freezing the minimum observed reserve as a timing guarantee is rejected.
An observed minimum is not an enforced deadline or an overrun policy.
The measurement remains necessary, but the acceptance ceiling and
overrun behavior must be defined before a reserve can be frozen.

Before adding or freezing `reserve_us`, define how the next
`handleMidiInput()` correlation is recorded after the idle line has
already been emitted. Keep the existing cursor-yield, unpublished
candidate, and previous-root behavior while that contract is open.
Do not freeze from the 12.5 ms proxy or use the old 79 ms parent span as
the reserve.

**Not this slice:** Changing discard, start, or `step` bounds. The
measurement contract must be valid before any of those decisions.

## ExecutionBounds rows 21–22 — goals versus implementation

The inventory states the **intended goals**, but the current
implementation does not fully enforce them. Do not reclassify from the
parent `snapshot_idle` span or from documentation alone.

**Row 21 goal:** One admitted `LoopSnapshotConstruction::step` applies
at most one persistent-cursor bar and copies at most eight membership
pages of sixteen references. It does not rebuild a snapshot, enumerate a
pass, destroy a tree, or publish. An incomplete step leaves the
candidate unpublished and resumable.

**Row 22 goal:** Wrap and session-exit only **queue** journal-capped
fragments (exact parent identity) at stop / wrap commit. One idle turn
admits at most one fragment start **or** one row-21 step.
`storePublishedSnapshot` runs only when wrap journals, session-exit
passes, construction, and companions are complete. Failure keeps the
previous published root and never publishes a prefix.

**Confirmed gaps**

- `discardPendingIfLoopLengthChanged` first clones one snapshot header
  when applied live-record ticks fit the sealed length. Non-fit discard
  remains a content-sized destructor. It is not a row-21 step and not a
  journal-capped fragment start.
- `startNextPassChangeFragment` is the row-22 fragment-start unit, but
  its lookup work is not bounded by the journal change cap.
- `step` at 5.992 ms is the row-21 unit. Compare it to a valid
  `T_RT_reserve` contract before considering any split of `begin` /
  `applySoonestBar`.

Keep the goals as the target contract. Update Class / Bound only with
firmware that enforces the corresponding goal.

## Pre-implementation review — Stage 2 leftover probes

### Ready
- `discardPendingIfLoopLengthChanged` and `enqueueLiveOverdubPasses` are
  the named untimed calls before start.
- `appendSnapshotIdleCaptureLine` already owns the child line.
- Child-line budget on [`224842`](../../captures/session_20260827_224842.log)
  left room for three 6-digit fields.

### Resolved
| Topic | Decision |
|---|---|
| Named units | `enqueueLiveOverdubPasses`, `discardPendingIfLoopLengthChanged`, parent-line emit |
| Fit method | Same `DIAG,snapshot_idle,child` line |
| Parent `us` | Stops at emit start; `emit` is diagnostic only |
| Construction | Unchanged |

### Open before coding
1. Device recapture after upload.

### Proceed?
- YES — implemented 2026-08-27.

## Pre-implementation review — Stage 2 session-exit length handoff

### Ready
- `discardPendingIfLoopLengthChanged` already runs at the start of
  `stepPendingSnapshotPublish`.
- Snapshot-root length is the sole note-boundary authority.
- `appliedLiveRecordCompletedTicks` is the persistent fit cursor.

### Resolved
| Topic | Decision |
|---|---|
| Fit check | Applied live-record ticks `> 0` and `<=` sealed length; reject grow |
| Handoff | One `LoopSnapshot` header sharing span/event roots |
| Non-fit | Existing `reset()` discard |
| +16 live-record room | Unchanged |

### Open before coding
1. Device recapture after upload — not a firmware blocker.

### Proceed?
- YES — implemented 2026-08-28.

## Stage 2 recapture — [`003015`](../../captures/session_20260828_003015.log)

- Session-exit length handoff **closed**. 11085 parent rows, 11085
  complete child rows, 0 truncated.
- Record stop seals `loopLengthTicks=6144`. First PLAYING idle at
  28.734 s is **3.182 ms**: `discard=47`, `start=3118`
  (`pass_change=3084` nested), leftover **16 µs**. This is the 225659
  leftover class with the destructor removed.
- Post-record `discard` max is **47 µs**. No `discard >= 100`.
- Completeness held for the first root: `root=0x0` until 30.207 s
  (`pending=0`, `root=0x703248cc`, `root_rev=1`, `queued=0`,
  `construction=0`, `journals=0`). No earlier nonzero root.
- RECORDING used 2669 idles / **234 ms** CPU, `us_max=7.934 ms`. That
  is not the [`121754`](../../captures/session_20260826_121754.log)
  prefix rebuild (2611 idles / 17.7 s CPU).
- Later successor construction keeps the published root selected
  (`root_rev=1` then `7`). Final `pending=0` at 81.096 s publishes
  `root=0x703ae154` / `root_rev=7`. `PLAYING -> STOPPED` at 97.204 s
  has no later `snapshot_idle` in this file.
- Named maxima still open: `start`/`pass_change` **12.405 / 12.357 ms**,
  `step` 4.391 ms. `T_RT_reserve` is not frozen.

## Pre-implementation review — Stage 2 pass-change inner timings

### Ready
- `pass_change` already wraps `startNextPassChangeFragment`.
- Child line is at nine fields; five more fields do not fit 191 bytes.
- `DIAG,snapshot_idle,` is already Tier-A.

### Resolved
| Topic | Decision |
|---|---|
| Fit method | Third `DIAG,snapshot_idle,inner` line; reuse the child buffer |
| Pairing | Same `#CAP` micros as parent/child |
| Lookup / journal cap | Unchanged this slice |
| Construction | Unchanged |

### Open before coding
1. Device recapture of the 12.4 ms `pass_change` class — not a firmware blocker.

### Proceed?
- YES — implemented 2026-08-28. Host gates: capture-serial RAM1 locals
  **6944**; native **1494/1494**. Device recapture
  [`004119`](../../captures/session_20260828_004119.log) names
  `note_lookup`.

## Stage 2 recapture — [`004119`](../../captures/session_20260828_004119.log)

- Inner-line budget **closed**. 18216 parent / child / inner rows, 0
  truncated. Inner length 101–115.
- Exclusive `pass_change` leftover after the five inners is 4–28 µs on
  every `pass_change >= 1000` row.
- The 20 ms STOPPED session-exit class is `note_lookup`. At 100.119 s:
  parent **21.096 ms**, `start=21073`, `pass_change=21050`,
  `note_lookup=18293` (86.9%), `refill=1299`, `event_scan=1253`,
  `change_build=162`, `construct=15`. Every ≥10 ms fragment is 82–88%
  lookup. `step=0` on those rows.
- A separate 2–3.5 ms cluster at 80.8 s (after `OVERDUBBING -> PLAYING`)
  is `refill`-dominated. That is not the 20 ms class.
- Discard gate remains closed: `discard` max **30 µs**.
- Completeness held: `root=0x0` until 108.551 s
  (`pending=0`, `root=0x703f2eb0`, `root_rev=6`, `queued=0`,
  `construction=0`, `journals=0`). No earlier nonzero root.
- Stage 1 hold: `PLAYING -> STOPPED` at 91.640 s; first STOPPED idle at
  +0.121 s (`pending=1`, `queued=1`, `root=0x0`).
- RECORDING used 3923 idles / **318 ms** CPU, `us_max=7.745 ms`. That
  is not a [`121754`](../../captures/session_20260826_121754.log) prefix
  rebuild.
- `T_RT_reserve` is not frozen. `step` max is 5.399 ms. Do not check
  off OpenSpec `3.5` / `3.6`.

## Pre-implementation review — Stage 2 caller-scoped pitch lookup

### Ready
- `findNote` already has a one-pitch search.
- `appendNextPassChangeFragment` is the measured 20 ms caller.
- Generic `findNote(90, 64)` still covers note-edit cross-pitch.

### Resolved
| Topic | Decision |
|---|---|
| New method | `LoopSnapshot::findNoteAtPitch` |
| Caller | `appendNextPassChangeFragment` only |
| Generic `findNote` | Unchanged |
| Journal cap | Unchanged |

### Open before coding
1. Device recapture of the [`004119`](../../captures/session_20260828_004119.log)
   21 ms class — not a firmware blocker.

### Proceed?
- YES — implemented 2026-08-28. Host gates: capture-serial RAM1 locals
  **6944**; native **1495/1495**. Device recapture
  [`005527`](../../captures/session_20260828_005527.log) drops
  `note_lookup` from 18.293 ms to 857 µs.

## Stage 2 recapture — [`005527`](../../captures/session_20260828_005527.log)

- Other-pitch walk **closed**. 18704 parent / child / inner triples, 0
  truncated.
- `note_lookup` max **857 µs** (was 18293 on
  [`004119`](../../captures/session_20260828_004119.log)). `pass_change`
  max **3.219 ms** (was 21050). No `pass_change >= 10000`.
- Largest pass-change rows are refill-dominated (`refill=2236` of
  `pass_change=3219` at 86.350 s). Lookup share on `pass_change >= 1000`
  is 0.05–0.39.
- Exclusive leftover after the five inners is 3–29 µs on those rows.
- Discard gate remains closed: `discard` max **35 µs**.
- Completeness held: `root=0x0` on every row. File ends at 103.013 s
  still `pending=1`, `queued=3`, `construction=10`, `journals=0`.
- Tail [`005819`](../../captures/session_20260828_005819.log) **closes
  publication**: `root=0x0` until 125.041 s, then `pending=0` /
  `root=0x70413fec` / `root_rev=10` / `publish=9`. `note_lookup` max
  in the tail is 136 µs; `pass_change` max is 1.038 ms. One
  `RING,overflow` at the start of that file.
- Stage 1 hold: `PLAYING -> STOPPED` at 94.769 s; first STOPPED idle at
  +0.071 s (`pending=1`, `queued=3`, `root=0x0`).
- RECORDING used 3780 idles / **379 ms** CPU, `us_max=7.480 ms`. That
  is not a prefix rebuild. The 7.480 ms parent is a live-record `start`
  with `pass_change=0`.
- `step` max is 5.835 ms. `T_RT_reserve` is not frozen.
