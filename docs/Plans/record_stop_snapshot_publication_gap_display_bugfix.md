# Record-stop snapshot publication gap — display and MIDI consequences

**Status:** Stages 1–5 shipped (2026-09-12); stage 6 design-gated; stage 7 post-closeout.
**Priority:** P0 — MIDI timing damage on long loops.
**Evidence:** [`session_20260912_033502.log`](../../captures/session_20260912_033502.log) (114,083 lines)
**OpenSpec:** [`immutable-loop-snapshot-passes`](../../openspec/changes/immutable-loop-snapshot-passes/) — active phase 6.4; related open task `4b.3`
**Parent publication plan:** [`snapshot_publication_timing_bugfix.md`](snapshot_publication_timing_bugfix.md)
**Sibling (overdub-stop side, closed):** [`overdub_stop_snapshot_publication_convergence_refinement.md`](overdub_stop_snapshot_publication_convergence_refinement.md)
**Gap-hold origin (shipped):** [`immutable_loop_snapshot_6_3_2_post_record_visual_continuity_refinement.md`](immutable_loop_snapshot_6_3_2_post_record_visual_continuity_refinement.md)

---

## Debugging boundary

```text
capture store / commit / seal        ← trust; ODUB stop seal reports `published`
  → session-exit publication         ← RC1, RC2 (this plan)
  → visual cache idle fill           ← RC3 consequence only
  → draw-source compose              ← RC4 (this plan)
  → paint phases on the loop         ← RC5 (this plan, design-gated)
```

Record and overdub capture content is correct in this capture. Do not re-open commit,
seal, or occupy/overlap resolve. The stored second-overdub verification passed
(`ODUB,stop,seal,...,published`).

## Session under analysis

| Phase | Window | Notes |
|---|---|---|
| RECORDING | 2001.15 → 2043.46 | 21 bars, 16128 ticks |
| PLAYING gap | 2043.48 → 2044.72 | 1.2 s |
| OVERDUBBING | 2044.72 → 2105.07 | 30 bars, one loop wrap |
| Deferred stop tail | 2105.07 → 2111.16 | `ODUB,stop,display,...,hold` |

---

## RC1 — Record stop discards the entire live-record candidate

`Loop::stepPendingSnapshotPublish` calls `discardPendingIfLoopLengthChanged` on every turn
once `sessionExitQueued` is set. That helper keeps the candidate only when
`pendingSnapshotFitsSealedLength` holds, and that predicate returns **false** whenever
`unpublishedCandidate->loopLengthTicks() < sealedLength`. A growing live-record candidate is
always shorter than the sealed length, because the final bar is still in progress when
recording stops. The discard therefore fires deterministically at record stop and resets
`unpublishedCandidate`, `construction`, `passChangeProgress`, `appliedLiveRecordEventIndex`,
and `appliedLiveRecordCompletedTicks`.

`resolvePendingParent` then returns `LoopSnapshot::createEmpty(...)` for a record session
exit, so the record pass is rebuilt from an empty parent and `eventAlreadyInSnapshot` has
nothing to dedupe against.

**Proof.** Exactly one discard spike exists in the whole capture:
`snapshot_idle,child,...,discard=23917` at `2043.569`, the first Snapshot idle turn after
record stop. Every other turn's discard check costs at most 14 µs. The same turn shows
`construction=1` appearing (the record pass id) and `queued` returning to 0, which is the
`handoffLiveRecordProgressToCommittedPass` transition.

**Cost.** 42 seconds of incremental live-record snapshot work is thrown away, and the
23.9 ms free runs inline on the deferred-idle path.

**Invariant to establish.** A live-record candidate that covers a prefix of the sealed loop
must be retained across record stop and extended to the sealed length, not discarded.

**Gate.** `pendingSnapshotFitsSealedLength` currently rejects the normal case while
`handoffPendingSnapshotLength` already supports lengthening via
`LoopSnapshot::withLoopLengthTicks` and `LoopSnapshotConstruction::handoffLoopLengthTicks`.
Widening the predicate changes when a candidate survives a session-exit transition, so this
is architecture-checkpoint question 2 — **design session required**, not a patch.

## RC2 — Snapshot admission self-locks for the whole overdub

In `Track::processDeferredIdleMaintenance`:

- `snapshotIdleAllowed` requires `liveRecordPastFirstBar || hasInFlightSnapshotConstruction() || hasWrapJournal`. During OVERDUBBING the first and third are false.
- `allowStartConstruction` is passed as `(!isOverdubbing() && liveRecordPastFirstBar) || hasWrapJournal`, which is false during OVERDUBBING.

So an overdub turn may only advance a construction that is already in flight, and the
admission condition is exactly the thing that turn consumes. When the last in-flight
construction finishes during OVERDUBBING, no new one can start and no further turn is
admitted until the overdub ends.

**Proof.** `snapshot_idle` `inflight` goes to 0 at `2044.792` — 72 ms after the overdub
began — and stays 0 until `2111.159`, the `ODUB,stop,debt_handoff` timestamp. That is a
**66.4 s** stall. Counted by phase, the 60 s overdub window contains **0 starts, 5 steps,
1 finish**; the record window contains 49 starts / 35 finishes / 2652 steps and the
post-stop window 27 / 26 / 2085.

**Result.** No snapshot was ever published in the session. `snapshot_unit,kind=publish`
count is **0**; all 15,413 `snapshot_idle` samples read `root=0x0`, and the final sample at
`2131.786` still reads `queued=1, applied=0, construction=1, exit=1, inflight=1`. The
snapshot allocator reached **7,782 nodes / 1,070,824 bytes / 39 slabs** with zero applied
passes.

**Invariant to establish.** Pending session-exit publication work must converge under a
bounded schedule that does not depend on a construction already being in flight.

**Gate.** Admitting construction starts during OVERDUBBING adds work to the MIDI interval
path — architecture-checkpoint question 3 and
[`ExecutionBounds.md`](../Authority/Architecture/ExecutionBounds.md) / DEC-043. **Design
session required.**

## RC3 — No source for committed display rows (symptom 1: bars 2–5 never redrawn)

With `publishedSnapshot()` null for the entire post-record session,
`isPlaybackSessionExitPublicationGap()` is continuously true, so every consumer's
hold-until-publish bridge becomes the steady state:

- `Loop::rebuildVisualCacheIdleSlice` clears its slice in the gap. All 126 idle attempts
  report `VCACHE,idle_src,OverlayRejected,src,None,root_rev,0`, and every one of them
  happened inside the 1.2 s PLAYING gap. There were **zero** during the overdub (DEC-045:
  `allowVisualCacheReconstructSlice` excludes OVERDUBBING) and zero after it. The cache
  stayed `VCACHE,stale_all,notes,0,total,21,dsz,21,dcnt,21`.
- `shouldGatherPublishedSnapshotForDrawSource` returns false without a published snapshot.

Both committed gather sources are therefore empty, which the draw-source telemetry states
directly: `draw_src,layers,visual,0,preview,56,sealed,0,snap,0,frame,1520`. The frame is
carried entirely by the record-stop handoff seed retained by
`DisplayWindowUtils::shouldRetainRecordStopHandoffSeed`, and that seed is the last
RECORDING frame, whose 16-bar window started at tick 3864 — bar 5. Bars 1–4 were never in
it and nothing can re-gather them.

**Proof.** Final unique-note-per-bar histogram: bar 0 = 19; bars 1–4 = 6, 7, 6, 7; bars
5–20 = 18–22; bar 21 = 3, against a recorded density near 16 notes per bar.

**Fix.** RC3 has no fix of its own. It resolves when RC1 and RC2 make the gap short again.

## RC4 — Committed prefix absorbs overlay rows (symptoms 2 and 3)

`DisplayManager::resolveDisplayNotesByDrawSource` full-rebuild path, gap-hold branch:

```cpp
if (holdSessionExitGapHandoff || retainHandoffSeed) {
    committedDisplayEnd = liveDisplayNotes.size();
    storeCommittedDrawSourcePrefix(liveDisplayNotes, committedDisplayEnd,
                                   liveDisplayCommittedPrefix_);
}
```

`liveDisplayNotes` at that point is the previous frame **including its overlay rows**, so
capture-preview rows are folded into `liveDisplayCommittedPrefix_`. On the next frame
`DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom` scans only
`[committedDisplayEnd, size)` while capture is active, so it cannot see the buried preview
rows and appends a fresh generation instead of replacing them. Each full rebuild in the gap
adds one more copy of the current preview set, and each stale copy keeps its note-on
geometry.

**Proof.**
- Stopped-frame dump: **2,890 rows containing 359 unique notes**; 71 notes duplicated, up to 70 copies each.
- `draw_src,diff,bar,0,id,340,old,71,624,624,new,71,624,672` — the retained row is zero-length at note-on time while the live preview has advanced.
- `draw_src,layers,...,missed_preview,31,compose_miss,31` on the same frames.
- `draw_src,layers,visual,0,snap,0,frame,1520` proves the rows cannot have come from a committed source.

**Consequence.** Gather and upsert are linear scans per row, so cost is superlinear in
frame size: `DFRAME` resolve grows from 1.0 ms to **252 ms**, draw from 2.4 ms to 27 ms,
frame elapsed to **348 ms**.

**Fix.** Store only the pre-overlay committed range in `liveDisplayCommittedPrefix_`. The
hold branch must keep the retained committed rows and re-derive overlays each frame, which
is what the Reuse and Slide branches already do.

**Gate.** Architecture checkpoint — ownership **no**, transitions **no**, unbounded
interval owner **no** (this removes work). Contained bugfix in one owner.

## RC5 — Display frames starve MIDI service (symptom 4)

The paint pipeline runs one phase per main-loop iteration inside
`DisplayManager::runPaintResolvePhase` / draw / transfer, so a 250–350 ms frame is
250–350 ms of MIDI starvation.

**Proof.**
- `DIAG,midi_gap` maximum gap grows 16.8 → 47 → 92 → 165 → 270 → **336 ms** across the overdub while its sample count falls from 168 to 19.
- `DIAG,loop_rem,display_frame` attributes 269–336 ms to the display frame.
- `BAR` tick error against exact bar boundaries: +0 at bar 13, +32 at bar 22, +48 at bar 24, +64 at bar 27, **+112 at bar 30**. `BAR` intervals swing 1,859–2,251 ms against 2,000 ms nominal — this is the BPM readout the user observed, and it reflects real tick drift.

`DIAG,counter,DisplayResolveOverBudgetCount` is **0** not because a budget held, but because
the only code that increments it is `DisplayManager::resolveDisplayNotesLiveCapture`, which
has **no callers**; `resolveDisplayNotesCommitted` is likewise dead. When the draw-source
gather became the sole owner, the per-resolve budget was not carried over.

**Fix.** Restoring a bound on the draw-source gather is architecture-checkpoint question 3.
**Design session required**, with the outcome recorded in
[`ExecutionBounds.md`](../Authority/Architecture/ExecutionBounds.md).

## RC6 — Bloated frame is adopted into the visual cache

`DisplayCacheLifecycle::refreshViewportAfterOverdubStop` calls
`adoptComposedDisplayNotesIfAuthoritative`, which forwards to
`Loop::adoptComposedDisplayNotesFromViewport`. The duplicate-laden frame therefore becomes
the visual cache and the bloat survives into PLAYING and STOPPED.

**Proof.** `VCACHE,adopt_partial,notes,2890` at overdub stop.

**Fix.** Reject or de-duplicate on adopt using the established
`keepShortestEndAtPitchStart` policy already used by the visual cache paths.

**Gate.** Architecture checkpoint all **no**. Contained bugfix.

## RC7 — Capture instrumentation distorts the measurement

- 61,652 `DIAG,snapshot_idle` lines plus 5,134 `DIAG,snapshot_unit` lines in 128 s (three lines per idle turn, `emit=32–46 µs` each).
- `DisplayNoteResolve::logDrawSourceComposeDifferential` runs an O(visual × frame) scan with linear `findByNoteId` / `findByPitchStart` lookups every 250 ms, plus one `preview_miss` line per mismatched preview note — 1,057 of those lines in this capture.

Per [Instrumentation-Session-Capture-Gating](../../.cursor/rules/Instrumentation-Session-Capture-Gating.mdc)
these must not shape the hot path. Downsample before re-measuring, or the next capture
cannot be trusted to attribute timing.

---

## Dependencies

```text
RC1 (candidate discard)  ──┐
RC2 (admission self-lock) ─┴─→ publication gap becomes permanent
                                   │
                                   ├─→ RC3  bars never redrawn
                                   └─→ RC4  prefix absorbs overlays
                                            │
                                            ├─→ RC5  MIDI starvation, tick drift
                                            └─→ RC6  bloat adopted into cache
```

RC4 and RC6 are independently wrong and independently fixable. RC3 disappears only when the
gap closes. RC5 is a missing bound that RC4 exposed; it must be closed on its own merits
because any future long gather can re-create it.

## Stages

| Stage | Scope | Gate | Verification |
|---|---|---|---|
| 1 | RC7 instrumentation downsampling | checkpoint all no | **SHIPPED** — RAM1 6848; native 1700/1706 |
| 2 | RC4 prefix contamination | checkpoint all no | **SHIPPED** — `retainDrawSourceHandoffCommittedPrefix`; native 1700/1706 |
| 3 | RC6 adopt de-duplication | checkpoint all no | **SHIPPED** — `dedupeComposedDisplayNotesForVisualCacheAdopt`; native 1700/1706 |
| 4 | RC1 candidate retention across record stop | design session — **SHIPPED** Option A | **SHIPPED** — native 1701/1707; HITL record → overdub pending |
| 5 | RC2 admission convergence | design session — **SHIPPED** Option A (`124756` trigger) | **SHIPPED** — native 1703/1709; HITL pending |
| 6 | RC5 draw-source execution bound | design session (execution bounds) | `ExecutionBounds.md` entry + HITL |
| 7 | Preview compose parity (`missed_preview` / `compose_miss`) | checkpoint after 4–6 | HITL recapture; fix only if still present |

Stage 1 first so stages 2–6 are measured on trustworthy captures. Stages 2 and 3 land before
the design sessions so the timing damage is removed while the publication questions are
still open.

### Stage 7 — post-closeout parity check (do not fix early)

After stages 4–6 land and a fresh record → overdub → wrap HITL capture shows `root` non-zero
and bars 2–5 redraw, **re-check** late-overdub `draw_src,layers` for non-zero
`missed_preview` / `compose_miss`.

**Baseline (stages 1–3 only):** [`session_20260912_122537.log`](../../captures/session_20260912_122537.log)
tail overdub lines report `missed_preview,12,compose_miss,12,trunc,0` while `frame` stays
linear (`348 = 268 committed handoff + 80 preview`). Lag and tick drift are fixed; this is
not the duplication pathology (RC4).

**Decision gate:**

- If `missed_preview=0` after publication converges → **no stage-7 firmware**; close as
  gap-handoff artifact.
- If non-zero after stages 4–6 → investigate preview vs compose identity (likely
  `upsertDisplayNoteAtPitchStartFrom` / hold-path suffix boundary with a current snapshot);
  fix only with native fixture from the new capture anchor.

Do **not** schedule stage 7 before RC1/RC2 — missing bars 2–5 and preview parity share the
gap-handoff context but have different owners; publication first, parity last.

## Validation criteria

Per stage: `pio run -e teensy41-capture-serial` with RAM1 locals ≥ 4000, then
`pio test -e native` in parallel with the dedicated HITL
([HITL-Implementation-Evaluation](../../.cursor/rules/HITL-Implementation-Evaluation.mdc)).

Capture-level acceptance for the whole plan, measured on a record-21-bars → overdub-30-bars
session comparable to `033502`:

1. `snapshot_unit,kind=publish` occurs at least once, and `snapshot_idle` `root` becomes non-zero before the overdub starts.
2. No `snapshot_idle` `inflight=0` stall longer than one bar while `exit=1`.
3. Draw-source frame row count stays within a small constant of unique note count — no `(pitch, start)` duplicate in the stopped-frame `DNTE` dump.
4. `DFRAME` resolve stays inside the bound agreed in stage 6.
5. `DIAG,midi_gap` maximum stays within the existing post-stop bound reference; `BAR` tick error returns to 0 at every bar.
6. Every bar in the detailed window carries its recorded note density after a wrap.

**Stage 7 (after 1–6):** late-overdub `draw_src,layers` shows `missed_preview=0` and
`compose_miss=0`, or a scoped fix is shipped with a native fixture — not required for stages
4–6 sign-off.

### Stages 1–3 device anchor

[`session_20260912_122537.log`](../../captures/session_20260912_122537.log) vs
[`session_20260912_033502.log`](../../captures/session_20260912_033502.log):

| Check | `033502` | `122537` |
|---|---|---|
| End overdub frame rows | 2890 / 359 unique | 348 linear |
| `DFRAME` resolve max | 252 ms | 2.6 ms |
| `midi_gap` max (late OD) | 336 ms | 8.3 ms |
| `BAR` tick error | +112 ticks | 0 |
| `root` published | never | never (RC1/RC2 open) |
| Bars 2–5 after wrap | missing | still missing (RC3; needs 4–5) |

## Design session — RC1 / RC2 (2026-09-12)

**Status:** Decisions recorded; **RC1 Option A approved** (2026-09-12); **RC2 deferred** until post-RC1 HITL.

### Problem chain (confirmed)

```text
record stop → queueSessionExitSnapshot (LoopCapture.cpp, RecordStop)
  → first Snapshot idle → discardPendingIfLoopLengthChanged
       → pendingSnapshotFitsSealedLength rejects candidate shorter than sealed
       → 23.9 ms discard; appliedLiveRecord* cleared; resolvePendingParent → createEmpty
  → PLAYING gap (~1.2 s): allowStartConstruction=true; rebuild from empty parent starts
  → OVERDUBBING: allowStartConstruction=false; last in-flight fragment finishes in ~72 ms
  → inflight=0 for entire overdub; root never publishes → permanent publication gap
```

`TrackCaptureStopCommit` record branch only calls `markDisplayCachesStale`; session-exit
queue is already owned by `Loop::commitCapturePass` → `queueSessionExitSnapshot`. RC1 does
**not** need a new queue call site.

### Architecture gate — Stage 4 (RC1)

| Question | Answer |
|---|---|
| **Owner** | `Loop::discardPendingIfLoopLengthChanged` / `pendingSnapshotFitsSealedLength` in `LoopSnapshotPublish.cpp` |
| **Primary invariant** | Live-record prefix retained across record stop; extend to sealed length via existing handoff, not discard |
| **Ownership change?** | **NO** |
| **State transition change?** | **YES** — session-exit length mismatch transitions to handoff-extend instead of discard-rebuild |
| **Execution bound change?** | **NO** — removes a 23.9 ms discard+empty-parent cold path on the idle turn |
| **Behavior-preserving?** | **NO** for this stage (intentional fix) |
| **Reuse** | **YES** — widen `pendingSnapshotFitsSealedLength`; keep `handoffPendingSnapshotLength` |
| **Phase scope** | `LoopSnapshotPublish.cpp`; native `test_record_stop_publishes_complete_snapshot`; DEC-045 amendment |

### Architecture gate — Stage 5 (RC2)

| Question | Answer |
|---|---|
| **Owner** | `Track::processDeferredIdleMaintenance` → `allowStartConstruction` arg to `stepPendingSnapshotPublish` |
| **Primary invariant** | Pending session-exit publication converges without requiring a construction already in flight |
| **Ownership change?** | **NO** |
| **State transition change?** | **NO** — overdub entry unchanged; idle admission policy narrowed |
| **Execution bound change?** | **NO** — same `kSnapshotIdleConstructionUnitsPerTurn` (2 units/turn); existing reserve yield |
| **Behavior-preserving?** | **NO** for first-root-only exception; post-root DEC-045 successor freeze unchanged |
| **Reuse** | **YES** — extend `allowStartConstruction` predicate; optional helper in `MidiServiceDrain.h` |
| **Phase scope** | `TrackDeferredMaintenance.cpp`; `MidiServiceDrain.h` (comment + predicate); DEC-045 amendment |

### RC1 — options

| Option | Description | Verdict |
|---|---|---|
| **A (recommended)** | Remove the `< sealedLength` rejection for `unpublishedCandidate` and in-flight `construction` when `appliedLiveRecordCompletedTicks > 0` and `<= sealedLength`. Let `discardPendingIfLoopLengthChanged` call `handoffPendingSnapshotLength` and retain. | **Ship** — `withLoopLengthTicks` / `handoffLoopLengthTicks` already exist; fixes the normal growing-record case |
| B | Reorder discard: attempt `handoffPendingSnapshotLength` before `pendingSnapshotFitsSealedLength` whenever `sessionExitQueued` | Equivalent to A; slightly less explicit |
| C | Synchronous publish burst on record-stop button path | **Reject** — violates DEC-045 idle-stepped session-exit; blocks MIDI on long loops |
| D | `queueSessionExitSnapshot` on `TrackCaptureStopCommit` record branch | **Reject** — duplicate; `LoopCapture.cpp` already queues at seal |

**DEC-045 reconciliation:** Amendment 2026-08-26 (“session-exit discards first-bar unpublished when sealed length differs”) targeted in-progress RECORDING length steps, not successful handoff at stop. **Amend DEC-045:** at record session-exit, discard only when `appliedLiveRecordCompletedTicks` is invalid or `handoffPendingSnapshotLength` fails — not merely because candidate `loopLengthTicks < sealedLength`.

**Native test:** extend `test_record_stop_publishes_complete_snapshot` with a multi-bar live-record fixture (applied completed ticks < sealed, candidate parent shorter than sealed) asserting `appliedLiveRecordCompletedTicks` survives first idle step and publish completes without empty-parent rebuild.

### RC2 — options

| Option | Description | Verdict |
|---|---|---|
| **A (recommended)** | When `!havePublishedSnapshot() && sessionExitQueued`, set `allowStartConstruction=true` during OVERDUBBING (first-root-only). Post-root successor freeze (DEC-045 2026-08-25 amendment) unchanged. | **Ship** — closes self-lock; structurally bounded |
| B | Block `beginOverdubSession` until `publishedSnapshot()` non-null | **Reject** — UX/transition gate; does not help fast overdub entry |
| C | `sessionExitDrainPending` extra Snapshot turns only | **Insufficient** — extra turns do not pass `allowStartConstruction` |
| D | Synchronous PLAYING-gap burst before overdub | **Reject** — same DEC-045 conflict as RC1-C; PLAYING gap may be too short alone |

**Predicate (proposed):**

```cpp
const bool allowStartConstruction =
    (!isOverdubbing() && liveRecordPastFirstBar) || hasWrapJournal ||
    (!havePublishedSnapshot && snapshotLoop.pendingSnapshotSessionExitQueued());
```

(`pendingSnapshotSessionExitQueued()` or inline `pendingSnapshotPublishBox_->sessionExitQueued` — match existing encapsulation.)

**DEC-045 reconciliation:** Amend §3 — OVERDUBBING may `startNextPendingConstruction` when **no published root exists** and session-exit is queued. OVERDUBBING still must not start **successor** construction after a root exists (prior amendments stand).

### Implementation order

1. **Stage 4 (RC1)** — reduces discard waste; may alone shorten PLAYING-gap work enough for some sessions.
2. **Stage 5 (RC2)** — convergence guarantee when PLAYING gap is shorter than remaining construction (21-bar `033502` case).
3. HITL recapture — acceptance criteria §1–2; stage 7 parity check only after both land.

### User decisions (2026-09-12)

- **RC1:** Option A — widen `pendingSnapshotFitsSealedLength`; handoff at session-exit; amend DEC-045.
- **RC2:** Shipped stage 5 after [`124756`](../../captures/session_20260912_124756.log) showed
  `exit=1,inflight=0` through overdub with zero `kind=publish`.

---

## Out of scope

- Capture, commit, seal, occupy/overlap resolve — correct in this capture.
- Persistence and the 5.7 `already_pending` flake class.
- OpenSpec 6.4 / 6.5 closeout, which proceeds independently.
- Removal of the dead `resolveDisplayNotesLiveCapture` / `resolveDisplayNotesCommitted`
  translation units: worth doing, but as hygiene, not inside a stage above.
- Stage-7 `missed_preview` / `compose_miss` work before stages 4–6 complete.
