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
  existing structural classification conflicts with the 79 ms
  `snapshot_idle` observation; resolve whether the cost is outside the classified
  unit or the classification is incorrect before changing construction code.
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
| **Observed interval** | The `DIAG,midi_gap` sample whose window contains that work window. `midi_gap` is the interval since the previous `handleMidiInput()` exit, not an owner duration. |
| **Deadline lateness** | `DIAG,late_clk` / `DIAG,late_on` / `DIAG,late_off` in the same window, from `notePlaybackServiceEnter`. |
| **Clock reference** | Nearest `#CAP,BPM` before the idle line: `T_clock_us = 1_000_000 × 60 / (BPM × 24)`. Use it only to name the scheduling period, not as the reserve. |
| **Per-turn reserve** | Time from idle-turn start until the next `handleMidiInput()` deadline. When Snapshot is the first deferred owner in the gap, that remaining time is the gap still available at admission, not the full observed `midi_gap`. |
| **Child attribution** | Exclusive children are `start_us`, `step_us`, `finish_us`, and `publish_us`. `pass_change_us` is nested inside `start_us`. `probe_us` is measurement overhead for one timestamp pair; five child timers cost about `5 × probe_us`. |
| **Acceptance after freeze** | The largest exclusive child, construction-attributable `midi_gap`, and construction-attributable deadline lateness all stay within the measured reserve. An overrun yields and retries without publishing an incomplete candidate. |

Do not freeze a numeric reserve until the instrumented recapture attributes the
79 ms class. Rows 21 and 22 stay structurally classified until that attribution
shows whether the cost is outside the classified unit or the classification is
wrong.

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
- YES — implementing the second child line.
