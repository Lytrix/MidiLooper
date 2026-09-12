# Record-stop snapshot publication gap — display and MIDI consequences

**Status:** **CONDITIONAL PASS** — RC1/RC2 + stages 4–5 **device PASS** ([`130411`](../../captures/session_20260912_130411.log)); 6A/6D **shipped**; **6C design approved** ([capacity plan](record_stop_snapshot_live_record_candidate_capacity_design.md)); **6B deferred**; stage 7 open (preview parity).
**Priority:** P0 — MIDI timing damage on long loops.
**Evidence:** [`session_20260912_033502.log`](../../captures/session_20260912_033502.log) (114,083 lines)
**OpenSpec:** [`immutable-loop-snapshot-passes`](../../openspec/changes/immutable-loop-snapshot-passes/) — **6.4** closed; next **6.5** archive; related open task `4b.3`
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
| 4 | RC1 candidate retention across record stop | design session — **SHIPPED** Option A | **PASS** — native 1701/1707; device [`130411`](../../captures/session_20260912_130411.log) |
| 5 | RC2 admission convergence | design session — **SHIPPED** Option A | **PASS** — native 1703/1709; device [`130411`](../../captures/session_20260912_130411.log) |
| 6 | 6A drain + 6D draw-source bound | design session — **SHIPPED** | anchors [`135133`](../../captures/session_20260912_135133.log) / [`140318`](../../captures/session_20260912_140318.log) |
| 6B | First-root PLAYING construction-unit boost | design session — **DEFERRED** | superseded by 6C; see [capacity design](record_stop_snapshot_live_record_candidate_capacity_design.md) |
| 6C-A | Live-record candidate capacity extension | design session — **SHIPPED** | native 1706/1706; device PASS [`144434`](../../captures/session_20260912_144434.log) |
| 6C-B | Reduce `kExtraBars` oversizing | after 6C-A device proof | [plan](record_stop_snapshot_live_record_candidate_capacity_design.md) |
| P-persist | LSV1 `EditTail` monolith reboot (parallel) | separate bugfix | [`deferred_persist_lsv1_edittail_slice_bugfix.md`](deferred_persist_lsv1_edittail_slice_bugfix.md) — [`144434`](../../captures/session_20260912_144434.log); **not** 6C-B |
| 7 | Preview compose parity (`missed_preview` / `compose_miss`) | stage 7 | **IN PROGRESS** — [`record_stop_stage_7_preview_compose_parity_bugfix.md`](record_stop_stage_7_preview_compose_parity_bugfix.md); anchor [`130411`](../../captures/session_20260912_130411.log) |
| 8 | Record-stop bars 2–5 publication gap (UX) | bundled follow-up | **SHIPPED** — `supplementDrawSourceFromUnpublishedSnapshotForUncoveredBars` (display-only); native + device [`191442`](../../captures/session_20260912_191442.log) stop→publish 0.94 s |
| 8b | Stale published root during session-exit gap (overdub) | GAP-8b | **REVERTED** — bundled in `c16ec598`, reverted `3c9dcd4a`; deferred |
| 8c | Record-stop viewport gap — bars 2–4 empty until publish | GAP-8c | **REVERTED** — device [`204310`](../../captures/session_20260912_204310.log); design session — [`record_stop_viewport_gap_supplement_bugfix.md`](record_stop_viewport_gap_supplement_bugfix.md) |

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

1. `snapshot_unit,kind=publish` occurs at least once, and `snapshot_idle` `root` becomes non-zero before the overdub starts. **Amended for conditional closeout:** root may publish **during** overdub if convergence and self-lock criteria pass; pre-overdub publication remains a stage-6 latency goal, not an RC2 gate.
2. No `snapshot_idle` `inflight=0` stall longer than one bar while `exit=1`.
3. Draw-source frame row count stays within a small constant of unique note count — no `(pitch, start)` duplicate in the stopped-frame `DNTE` dump.
4. `DFRAME` resolve stays inside the bound agreed in stage 6.
5. `DIAG,midi_gap` maximum stays within the existing post-stop bound reference; `BAR` tick error returns to 0 at every bar.
6. Every bar in the detailed window carries its recorded note density after a wrap. **After the publish anchor**, bars 2–5 must carry committed snapshot coverage (see device closeout below).

### Overall plan status — CONDITIONAL PASS (2026-09-12)

**Closed (RC1 / RC2 / stages 4–5):**

| RC / check | Verdict | Anchor |
|---|---|---|
| RC1 record-stop handoff | **PASS** | discard 44 µs (was 23,917 µs) — [`130411`](../../captures/session_20260912_130411.log) |
| RC2 admission convergence | **PASS** | construction restarts during OD; no 66 s self-lock — [`130411`](../../captures/session_20260912_130411.log) |
| Publication converges | **PASS** | 2× `kind=publish`; `root=0x702e8ad4` at 96.074 s — [`130411`](../../captures/session_20260912_130411.log) |
| Timing regression | **PASS** | zero `midi_gap` in OD window; zero BAR error lines — [`130411`](../../captures/session_20260912_130411.log) |
| Linear frame | **PASS** | ~298–300 rows (not 2890) — [`130411`](../../captures/session_20260912_130411.log) |

**Residual (not RC1/RC2 — do not reopen convergence):**

> The published snapshot is now **convergent during overdub**, but **not necessarily available at overdub entry**. Architecture review (2026-09-12) identified the dominant cause as a **live-record candidate capacity cliff** (~17 bars), not construction throughput. Incremental construction stops during RECORDING while the take continues; session-exit debt scales as `(bars − 17)`. **Deferred to stage 6C** ([capacity design](record_stop_snapshot_live_record_candidate_capacity_design.md)). Stage 6B (units/turn boost) is **deferred** — it would pass a 21-bar capture without fixing the cliff. **Do not block overdub entry.**

**Stage 7 — IN PROGRESS (2026-09-12):** post-publish parity gate **failed** on [`130411`](../../captures/session_20260912_130411.log) — first `snap=1` clean at `96142977`; first non-zero at `111807617` (`7/7`); tail plateau `22/15`. Plan: [`record_stop_stage_7_preview_compose_parity_bugfix.md`](record_stop_stage_7_preview_compose_parity_bugfix.md).

**Stage 8 (GAP-8) — SHIPPED (2026-09-12):** during session-exit publication gap, uncovered bars in the paint window are supplemented from `Loop::unpublishedSnapshot()` while the RECORDING handoff frame is retained (`shouldHoldSessionExitPublicationGapHandoff`). Display-only — no DEC-045 synchronous publish on stop. Native: `test_gap_handoff_supplements_uncovered_bars_from_unpublished_snapshot`, `test_supplement_draw_source_from_unpublished_snapshot_predicate`. Device: [`191442`](../../captures/session_20260912_191442.log) stop→publish **0.94 s** (vs [`164947`](../../captures/session_20260912_164947.log) ~1.8 s); long LSV1 save is unrelated to display refresh.

**Stage 8b (GAP-8b) — REVERTED (2026-09-12):** stale-root predicate was in `c16ec598`, reverted `3c9dcd4a` with GAP-8c; deferred pending design session.

**Stage 8c (GAP-8c) — REVERTED (2026-09-12):** passes-fallback supplement + adequate bar coverage shipped in `c16ec598`, reverted in `3c9dcd4a` after device [`204310`](../../captures/session_20260912_204310.log) — notes lost on record stop (`DISP` `win_notes` 22→15), `BAR,376,0` after stop. Architectural design required before re-implementation. Plan: [`record_stop_viewport_gap_supplement_bugfix.md`](record_stop_viewport_gap_supplement_bugfix.md).

No further DEC-045 amendment for this result unless post-publish bars 2–5 verification exposes a semantic problem (it did not).

### Device closeout — stages 4–5 ([`130411`](../../captures/session_20260912_130411.log))

21-bar record (`16128` ticks) → ~56 s overdub. Compared to [`124756`](../../captures/session_20260912_124756.log) (RC1 only) and [`033502`](../../captures/session_20260912_033502.log) (pre-fix):

| Check | `033502` | `124756` | `130411` |
|---|---|---|---|
| Record-stop discard | 23,917 µs | 44 µs | **44 µs** |
| `kind=publish` | 0 | 0 | **2** |
| First non-zero `root` | never | never | **96.074 s** |
| Root before overdub | — | — | **No** (+1.41 s) |
| OD `exit=1,inflight=0` self-lock | 66.4 s | 227 samples | **4 brief gaps** (restarts) |
| OD `draw_src snap` | 0 | 0 | **67× `snap=1`** after publish |
| End overdub frame rows | 2890 / 359 unique | ~348 linear | **~298–300 linear** |
| OD `midi_gap` max | 336 ms | — | **0 samples** |

**RC2 proof window (94.662 s → 96.074 s):** 4 construction starts; alternating `inflight=0/1` with `exit=1`; first publish at 96.074 s (`root=0x702e8ad4`, `exit=0`).

**Bars 2–5 after publish anchor (96.074 s):**

- **Pre-root (93.35 s → 96.07 s):** PLAYING/early-OD `DISP` still uses handoff window start **3840** (bar 5) — expected gap-handoff behavior; not an RC2 failure.
- **Post-root (96.07 s+):** `draw_src snap=1` on every sampled line; `DISP` window starts include **0** and other low ticks (46 samples with `win≤768`, `notes≥250`); wrap-era OD (136 s+) shows `win=0`, `notes≈303–304`. **Committed snapshot coverage for bars 2–5 is present after the publish anchor**, including across wrap.
- **Conclusion:** display/source correctness is bounded to **overdub entry → first snapshot publication (~1.41 s)**; not a persistent missing-bar pathology after root lands.

Intermediate RC1-only anchor [`124756`](../../captures/session_20260912_124756.log): RC1 PASS (44 µs discard); RC2 still required (zero publish, 227× `exit=1,inflight=0`).

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

**Status:** Decisions recorded; **RC1 Option A approved** and **device PASS**; **RC2 Option A shipped** and **device PASS** ([`130411`](../../captures/session_20260912_130411.log)).

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

- **RC1:** Option A — widen `pendingSnapshotFitsSealedLength`; handoff at session-exit; amend DEC-045. **Device PASS** [`130411`](../../captures/session_20260912_130411.log).
- **RC2:** Option A — `allowSnapshotConstructionStart` during OVERDUBBING when no root and session-exit queued. **Device PASS** [`130411`](../../captures/session_20260912_130411.log). Do **not** reopen for publication-latency residual (stage 6).

---

## Design session — Stage 6 (2026-09-12)

**Status:** **6A + 6D SHIPPED**. **6B DEFERRED** (2026-09-12 architecture review). **6C design
APPROVED** — see [`record_stop_snapshot_live_record_candidate_capacity_design.md`](record_stop_snapshot_live_record_candidate_capacity_design.md).

### Problem (residual after RC1/RC2/6A) — superseded framing

RC2 solved **admission** (construction restarts during overdub; no self-lock). 6A solved **turn
frequency** (first-root session-exit drain). The **superseded** Stage 6B framing attributed
residual latency to **construction throughput** in the PLAYING window:

```text
throughput ≈ Snapshot idle turns in PLAYING × construction units / turn
```

**Superseded by 6C diagnosis (2026-09-12):** the architecture is already incremental during
RECORDING. A **fixed ~17-bar candidate tick capacity** (`liveRecordNewParentLength`, `kExtraBars
= 16`) stops live construction; debt accumulates until session-exit drain. Distinction:

| Term | Role |
|---|---|
| **Throughput** | Drain rate (units/turn, Ring B) — healthy (~5.5× headroom post-stop in `140318`) |
| **Capacity** | Candidate tick room — **binding cliff at bar 17** |

Full evidence, invariants, and implementation gate:
[`record_stop_snapshot_live_record_candidate_capacity_design.md`](record_stop_snapshot_live_record_candidate_capacity_design.md).

### Device scaling (6A only — anchors)

| Capture | Bars at stop | PLAYING window | Root vs OD | Notes |
|---|---|---|---|---|
| [`135133`](../../captures/session_20260912_135133.log) | 17 | 0.82 s | **−0.29 s** | pre-OD root |
| [`135759`](../../captures/session_20260912_135759.log) | 20 | 1.01 s | +0.58 s | brief OD1 then long OD2 |
| [`130411`](../../captures/session_20260912_130411.log) | 21 | 1.31 s | +1.41 s | pre-6A baseline |
| [`140318`](../../captures/session_20260912_140318.log) | 25 | 0.78 s | +2.49 s | stress beyond plan shape |

[`140318`](../../captures/session_20260912_140318.log) confirms: RC1 discard **9 µs**; RC2
converges (**27** OD-pre-publish idle turns); RC5 counter **0**; live freezes **saturate at 34**
during 49 s RECORDING then **stall 14 s** before stop — capacity cliff, not throughput-only.

**Do not** use [`140318`](../../captures/session_20260912_140318.log) preview parity (37/28) to
motivate 6B — that remains **Stage 7**.

### Architecture gate — Stage 6 (6A/6D — shipped)

| Question | Answer |
|---|---|
| **Owner (6A)** | `main.cpp::takeDeferredIdleOwnerTurn` → `MidiServiceDrain::allowSessionExitSnapshotDrain` |
| **Owner (6D)** | `DisplayManager::resolveDisplayNotesByDrawSource` |
| **Primary invariant** | Extra Snapshot turns on first-root session-exit; draw-source gather ≤ 5000 µs with prior-frame reuse |
| **Execution bound change?** | Row 3 → empirically bounded; row 22 turn **frequency** only (units/turn unchanged) |
| **Phase scope** | `main.cpp`, `TrackSlotPool.cpp`, `MidiServiceDrain.h`, `DisplayNoteWindowGather.cpp`, `ExecutionBounds.md` |

### Stage 6 — options (6A/6D)

| Option | Description | Verdict |
|---|---|---|
| **6A** | `allowSessionExitSnapshotDrain(postOverdub \|\| firstRootPending)` | **Ship** |
| **6D** | 5000 µs budget + prior-frame reuse on draw-source gather | **Ship** |
| 6C / 6E | Sync burst / block overdub | **Reject** |

---

## Design session — Stage 6B (2026-09-12)

**Status:** **DEFERRED** (2026-09-12) — superseded by Stage 6C. Retained for history; **do not
implement as next slice.** A 21-bar acceptance capture could PASS with faster drain while the
17-bar capacity cliff remains.

**Superseded by:** [`record_stop_snapshot_live_record_candidate_capacity_design.md`](record_stop_snapshot_live_record_candidate_capacity_design.md).

### Design question (precise)

> Can first-root session-exit construction receive a **bounded, temporary higher
> construction-unit budget during PLAYING**, without changing row 21 per-unit work, row 22
> queue/publish semantics, or MIDI reserve/yield policy?

This is **not** another admission mechanism. RC2 already solved admission. 6B only increases
how much **existing** bounded construction work runs per Snapshot idle turn while the
pre-overdub window lasts.

### What 6B increases (and does not)

| In scope | Out of scope |
|---|---|
| `maximumChanges` passed to `Loop::stepPendingSnapshotPublish` | `LoopSnapshotConstruction::step` bar/page caps (row 21) |
| Units **per Snapshot idle turn** only | Snapshot allocator / node limits |
| First-root session-exit only | Global `kSnapshotIdleConstructionUnitsPerTurn = N` for all modes |
| Revert to **2** units/turn once a published root exists | Overdub admission (`allowSnapshotConstructionStart`) |
| | Synchronous PLAYING-gap burst |
| | Block overdub until root |
| | New Manager / scheduler |

### Where the boost is allowed

Predicate (all required):

```text
sessionExitQueued
&& !publishedSnapshot()          // first root only
&& isPlaying()                    // pre-overdub PLAYING window only
```

**Not** during OVERDUBBING (RC2 already admits construction there). **Not** after
`storePublishedSnapshot` (successor / wrap journal uses normal 2 units/turn). **Not** during
STOPPED first-paint unless the existing session-exit drain already selected Snapshot (unchanged).

Proposed helper owner: `MidiServiceDrain::snapshotIdleConstructionUnitsForTurn(...)` in
[`MidiServiceDrain.h`](../../include/Utils/MidiServiceDrain.h); caller remains
[`Track::processDeferredIdleMaintenance`](../../src/Track/TrackDeferredMaintenance.cpp) →
`stepPendingSnapshotPublish(maximumChanges, ...)`.

### MIDI protection (unchanged semantics)

- **No** synchronous burst; **no** “finish snapshot before overdub” loop.
- Keep `kSnapshotIdleReserveYieldEnabled` / `allowSnapshotIdleReserve` — when yield is
  re-enabled, reserve admission must use the **dynamic** unit count for classified cost
  (`units × kSnapshotIdleConstructionUnitCostUs`), not the constant 2.
- `kSnapshotIdleReserveCeilingUs` (10 ms) and `kSnapshotIdleSafetyMarginUs` (1 ms) stay the
  policy ceiling; classified cost at 4000 µs/unit:
  - 2 units → 8000 µs (current)
  - 3 units → 12000 µs (exceeds ceiling — yield would reject unless interval is wide)
  - 4 units → 16000 µs
- With yield **parked** (current device default), reserve reject does not gate admission;
  **device MIDI gap / BAR** remains the acceptance gate for any candidate N.

### Candidate maximum (benchmark, not guess)

Do **not** pick an arbitrary N. Evaluate on device against:

1. Pre-overdub root latency on the scaling table above (target: 21-bar / ~1.3 s PLAYING → root
   before OD).
2. `DIAG,midi_gap` and `BAR` tick error during PLAYING construction window.
3. `snapshot_idle` child spans vs existing 60%-of-clock diagnostic proxy (not a contract).

**Native first:** policy predicate tests in [`test_midi_service_drain`](../../test/test_midi_service_drain/test_midi_service_drain.cpp) — boost only when
PLAYING + first-root; normal 2 after publish.

**Device candidates to bracket:** N ∈ {3, 4} (N=2 is current). Pick the **smallest N** that
passes 21-bar acceptance; if neither passes without MIDI regression, stop and reassess (do not
stack 6B with a global throughput raise).

### If 6B still cannot finish before overdub

- Overdub entry **remains allowed** (user rejected UX gate).
- RC2 convergence **remains the fallback** (root during overdub — CONDITIONAL PASS class).
- No new RC; no reopen RC2.

### Architecture gate — Stage 6B (pre-implementation)

| Question | Answer |
|---|---|
| **Owner** | `Track::processDeferredIdleMaintenance` → `stepPendingSnapshotPublish(maximumChanges)`; policy in `MidiServiceDrain` |
| **Primary invariant** | More row-21/row-22 **units per idle turn** during first-root PLAYING only; per-unit work unchanged |
| **Ownership change?** | **NO** |
| **State transition change?** | **NO** |
| **Execution bound change?** | **YES** — row 22 idle admission cardinality becomes **2 normally; N temporarily** under predicate (row 21 unchanged) |
| **Behavior-preserving?** | **NO** — intentional throughput boost |
| **Reuse** | **YES** — dynamic `maximumChanges`; extend reserve cost helper with unit parameter |
| **Phase scope** | `MidiServiceDrain.h`, `TrackDeferredMaintenance.cpp`, `ExecutionBounds.md` row 22 note, `test_midi_service_drain`, DEC-045 amendment |

### Implementation order

1. **6B firmware** — one candidate N (start with **3** unless bench says 4 required).
2. Native policy tests + `pio test -e native`.
3. `pio run -e teensy41-capture-serial` (RAM1 ≥ 4000).
4. **Formal 21-bar → long overdub acceptance capture** (same class as `033502` / `130411`).
5. Stage 7 parity re-check on that anchor only.

### User decision (2026-09-12, updated after architecture review)

- **6B:** **DEFER** — tactical optimization; wrong evidence if shipped before 6C.
- **6C:** **APPROVE design** — implement from [capacity plan](record_stop_snapshot_live_record_candidate_capacity_design.md) only; **not yet firmware**.
- **No** change to `kSnapshotIdleConstructionUnitsPerTurn` (stay at 2).
- **6C-A first:** remove cliff with `kExtraBars = 16` unchanged; **6C-B second:** reduce oversizing.
- Stage 7 preview parity **out of scope** for 6C.

---

## Design session — Stage 6C (2026-09-12)

**Status:** **DESIGN APPROVED — NOT YET IMPLEMENTED**

Canonical plan: [`record_stop_snapshot_live_record_candidate_capacity_design.md`](record_stop_snapshot_live_record_candidate_capacity_design.md)

**One-line:** Candidate length is extendable; candidate length is not a reason to reject
otherwise valid completed live-record content.

**Slices:**

| Slice | Scope | Verification |
|---|---|---|
| **6C-A** | Extend capacity at three gates; in-flight `handoffLoopLengthTicks`; keep `kExtraBars = 16` | Freeze count scales with bars during RECORDING (25–40 bar capture) |
| **6C-B** | Reduce `kExtraBars` to 1–2 | No extension/regression pathologies |

**Parked (not 6C):** RC-W wrap-journal rebase, RC-WQ journal depth, DEC-045/DEC-047 DOC reconciliation.

---

## Out of scope

- Capture, commit, seal, occupy/overlap resolve — correct in this capture.
- Persistence and the 5.7 `already_pending` flake class.
- OpenSpec 6.4 / 6.5 closeout, which proceeds independently.
- Removal of the dead `resolveDisplayNotesLiveCapture` / `resolveDisplayNotesCommitted`
  translation units: worth doing, but as hygiene, not inside a stage above.
- Stage-7 `missed_preview` / `compose_miss` work before stages 4–6 complete.
