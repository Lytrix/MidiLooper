# Record session-exit snapshot queue — the deferred Record publication never queues it

**Status:** root cause pinned and fixed (2026-09-29); fix + both fixtures landed and green, mutation
red-proved, full native and `teensy41-capture-serial` build verified, **device verified** — the base HITL
passes with a complete published root, and the pre-fix image reproduces the reported symptom on the same
stimulus.
**Kind:** bugfix inside **S4f.20 — mode-correct atomic publication and retirement** (parity of the
deferred Record publication with the inline Record stop), reachable only through the production
Record stop converted by **S4f.17 — common interval and stop admission** and **S4f.21 — Record-stop
caller migration**.
**Governing change:** [`runtime-working-set-ownership`](../../openspec/changes/runtime-working-set-ownership/proposal.md).
**Packet:** [`S4F17-CUTOVER-PACKET.md`](../../openspec/changes/runtime-working-set-ownership/S4F17-CUTOVER-PACKET.md)
§ "S4f.20 — the deferred Record publication queues the record session-exit snapshot".
**Decision:** `DEC-094` — a passing pass-commit queues the session-exit publication on every route, and
the snapshot coverage watermark may not advance over a pass the box did not enumerate.
**Parent plan (assumption this slice invalidates):**
[`record_stop_snapshot_publication_gap_display_bugfix.md`](record_stop_snapshot_publication_gap_display_bugfix.md)
— RC1 ("candidate retention across record stop") is predicated on "the session-exit queue is already
owned by `Loop::commitCapturePass` → `queueSessionExitSnapshot` … RC1 does not need a new queue call
site".
**Device evidence:** [`captures/session_20260929_225529.log`](../../captures/session_20260929_225529.log)
(the base HITL run in which the user reported the symptom: 3-bar Record take, two overdub passes,
an overdub undo at 64.8 s). Content-loss checks are **not** the subject here; the take's stored and
effective content is correct throughout.

---

## Debugging boundary

```text
Record take (sealed 2304, stop tick 2020 → completed-bar frontier 1536)   ← content correct
  → live-record candidate (content = events < 1536 only)                  ← truncation origin
  → record stop → deferred job → Track::stepDeferredRecordPublication     ← queue call MISSING here
  → pending box: record pass never enumerated (q=0, a=0)
  → overdub stop queues its own session exit → box publishes              ← publishes a truncated root
  → published root stamped current (coverage == committedPassRevision)    ← consumers trust it
  → visual cache + overdub source view rebuilt from the snapshot          ← user-visible truncation
  → any pass-stack change (overdub undo, transport stop) → PassesResolve   ← notes return
```

---

## Symptom (reported by the user, reproduced in the log)

Running the base HITL, the display loses the Record pass's last bar after the first overdub stop; the
notes come back when the second overdub pass is undone. Measured in the run:

| Observation (device time) | Value |
|---|---|
| Record leg | `Recording stopped @ tick 2020 (recStart=0 length=2304)` |
| Display frame during OD1 (pass-derived) | 48 notes = 32 Record + 16 OD1 |
| `snapshot_unit,kind=publish` (40.135 s) | published root, `q=0, a=1` |
| `VCACHE,idle_src,SnapshotNotes,…` → `VCACHE,slice_clean,…,notes,42` (40.17 s) | cache rebuilt **from the snapshot** = 42 notes |
| `DIAG,lcr,src,why=open,from=snapshot,…,notes=42` (47.87 s) + `srcspan` list | source view = 26 Record spans (278…1477) + 16 OD1 spans |
| `DIAG,lcr,pairrows,at=passes,count=32` (33.66 s) | the committed Record pass really holds all 32 notes |
| Frame after the OD2 stop | 50 notes = 26 Record + 16 OD1 + 8 OD2 |
| `VCACHE,idle_src,OverlayRejected,src,PassesResolve` → `slice_clean,notes,48` (64.9 s, the undo) | rebuilt from the passes → **notes restored** |
| STOPPED frame dump (`DNTE` note 75…79 at 1573…1765) | content present |

So the excluded set is exactly the Record notes at/after tick 1525 — note `74` (1525…1573) plus
`75…79` — i.e. `32 − 6 = 26`. That is not a Shorten/Hide transform (the geometry would survive it) and
not a display-window selection: it is a **membership** difference between the published snapshot and
the committed passes.

---

## Root cause

**Two facts that are individually correct, and one missing call between them.**

1. **The live-record snapshot candidate stops at the completed-bar frontier by design.**
   `MidiServiceDrain::liveRecordCompletedTicks` (`include/Utils/MidiServiceDrain.h`) returns
   `(recordElapsedTicks / ticksPerBar) * ticksPerBar` — the exclusive end of the *completed* bars, "the
   bar still being written is not included". A take stopped mid-bar therefore has candidate content
   short of its declared length. Here: stop tick 2020 → frontier **1536** (2 of 3 bars) → the candidate
   holds the 26 Record notes whose NoteOn *and* NoteOff are both `< 1536`; note `74` has no Off inside
   the frontier, so it is not closed and not emitted.
2. **RC1 retains that candidate across the stop and extends only its length.**
   `Loop::pendingSnapshotFitsSealedLength` accepts the candidate because
   `appliedLiveRecordCompletedTicks > 0`; `Loop::handoffPendingSnapshotLength` then rewrites the
   candidate's declared length to the sealed length (`src/Loop/LoopSnapshotPublish.cpp`). Content is
   untouched — correct *only if* the committed Record pass is enumerated into the same box.
3. **The deferred Record publication never queues the record session-exit publication**, which is the
   only mechanism that enumerates the Record pass. In the inline route the record pass is committed by
   `Loop::commitPendingCapturePass`, whose last act is
   `queueSessionExitSnapshot(reason, {})` guarded by `passes.recordPass.id == lastCommittedPassId_`
   (`src/Loop/LoopCapture.cpp`). The deferred route publishes the same pass through
   `Loop::commitPreparedCapturePassAtomic` (`src/Track/TrackOverdubLifecycle.cpp`,
   `Track::stepDeferredRecordPublication`) and mirrors the inline stop's other side effects
   (`RecordPassAdded` undo entry, display staleness, `invalidateCommittedContentForSlot`, the
   once-only persistence admission) — but not that queue. No other production caller exists:
   `Loop::commitCapturePass` is reachable from `Track::commitCaptureForStop`, which production calls
   only for `OverdubStop`/`OverdubStopToStopped`.

**Consequence.** A successor's session exit (the first overdub stop) queues the publication with only
its own pass id; the box publishes the retained truncated candidate plus the overdub pass and stamps
the root with the *current* committed-pass coverage
(`queueSessionExitSnapshot` writes `box.coveredPassRevision = committedPassRevision()`,
`publishedSnapshotIsCurrent()` compares that watermark against `committedPassRevision()` —
`src/Loop/LoopSnapshotLifetime.cpp`). Consumers that require a current root (visual cache slice
rebuild, overdub source view) then serve that membership, so the Record pass's last bar disappears.
Any later inventory change (the overdub undo) advances `committedPassRevision`, the root stops being
current, the next rebuild takes the passes-authoritative route, and the notes return.

Numbers close exactly: 26 (candidate prefix) + 16 (OD1 pass) = **42**; + 8 (OD2 pass) = **50**;
undo → **48** = 32 + 16.

### Why the native gate was green

`test_loop_take_survival` drives the **inline** `Loop::commitCapturePass`, which queues the session
exit inside the same call, so the Record pass is enumerated and the published root is complete. The
gap is a property of the *caller migration* (S4f.17/S4f.21), not of the Loop/display owners, which is
why the recorded "issue 2" note narrowed the target correctly but could not reproduce it.

---

## Invariant

* A pass commit that owns a session exit queues that session-exit publication on **every** route
  (inline and deferred), before any successor's publication can complete.
* The live-record handoff may extend a candidate's declared length beyond its applied content frontier
  only with the pass that carries the remaining content enumerated into the same box.
* A published root's coverage watermark describes the content it actually incorporated (DEC-073); it
  must not advance over a committed pass the box did not enumerate.

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | **NO** — the Loop keeps the publication owner; the Track publication calls its existing public API |
| State transition change? | **NO** — same stop → publication → session-exit queue sequence the inline route always had |
| Unbounded interval owner? | **NO** — one box queue push on the cold publication path (once per take); no new per-turn work |
| Behavior-preserving? | **NO** for the defect (intentional parity fix); the inline route is unchanged |
| Reuse | **YES** — `Loop::queueSessionExitSnapshot`, the existing box, the existing publisher |

---

## Slice — what changed

1. `Track::stepDeferredRecordPublication` (`src/Track/TrackOverdubLifecycle.cpp`) queues the record
   session-exit publication once the pass is committed, with the mode-correct reason
   (`PendingStopDestination::Playing` → `CommitReason::RecordStop`, `Stopped` →
   `CommitReason::RecordStopToStopped`), mirroring the inline route. The declared contract comment in
   `include/Track.h` now names it.
2. `Loop::queueSessionExitSnapshot` (`src/Loop/LoopSnapshotPublish.cpp`) takes a **session-exit receipt
   only for an overdub exit**. A receipt is an *overdub* session's identity handoff (DEC-068), so for a
   Record exit the request enumerates `lastCommittedPassId_` — its own just-committed pass. Without this,
   a ready-but-unconsumed receipt from an earlier overdub session (or a legacy one-shot handoff whose
   interval committed no pass, leaving `lastPassId` unfilled) made the Record exit enumerate that
   session's pass, or nothing at all, while the coverage watermark still advanced — the same
   truncated-current-root shape, and the reason the fix must not depend on the ring being drained. The
   inline route gains the corrected selection too.
3. Fixtures (below) pin the contract on both routes and red-prove both edits.

**Non-goals.** No watermark rewrite (see *Residual*), no `RemainingSessionExitPublishWork` change, no
publication-latency work, no HITL/stimulus change, no upload.

## Fixtures

| Fixture | Suite | Assertion |
|---|---|---|
| `test_deferred_record_publication_queues_the_record_session_exit_snapshot` | `test_shared_capture_finalization` | after a mid-bar deferred Record stop the box has the session exit queued with the Record pass on the queue, and the drained root is current and carries the take's note beyond the completed-bar frontier |
| `test_record_session_exit_with_a_truncated_candidate_keeps_the_take_tail` | `test_loop_take_survival` | Loop-level invariant: a handoff-extended candidate (frontier 1 bar < sealed 2 bars) with a note beyond the frontier publishes a current root that still holds that note |
| `test_record_session_exit_ignores_a_pending_overdub_receipt` | `test_loop_take_survival` | with a ready receipt (unfilled pass id, the legacy handoff's shape) still in the ring, the `RecordStop` arm still queues its own pass and publishes a current root holding the take's note |

Mutations (red-proof, measured, each restored byte-identically — `diff -q` clean, no marker left):
deleting the queued call in `stepDeferredRecordPublication` fails **only** the deferred fixture
(`the deferred Record publication must queue the record session exit`); restoring the unfiltered
`oldestPendingSessionExitReceipt()` selection fails **only** the receipt fixture
(`the Record exit must enumerate its own pass, not the stale receipt`); removing the
`queueSessionExitSnapshot` call in `Loop::commitPendingCapturePass` fails the Loop fixtures at
`a Record session exit must queue its publication` (and, with that assertion bypassed, at
`publishedSnapshotIsCurrent()` — no root publishes at all).

## Validation — measured 2026-09-29

| Check | Command | Result |
|---|---|---|
| Focused, deferred route | `pio test -e native -f test_shared_capture_finalization` | **55/55 passed** (new fixture included) |
| Focused, Loop route | `pio test -e native -f test_loop_take_survival` | **143/143 passed** (141 + the two new fixtures) |
| Both together | `pio test -e native -f test_loop_take_survival -f test_shared_capture_finalization` | **198/198 passed** |
| Broad | `pio test -e native` | **2 636 cases / 2 629 succeeded / 7 skipped / 0 failed** |
| Build | `pio run -e teensy41-capture-serial` | **SUCCESS**; `variables 96,192 / ITCM 393,216 (code 391,288 + padding 1,928) / locals 34,880`, floor 840 — padding unchanged from the pre-slice tree |
| Mutation | as above | three removals red-proved, each restored byte-for-byte |

## Device run (2026-09-29)

Fixed image uploaded, then the authoritative base HITL
(`host_midi_hitl.py run --preset base --track 5 --loop-slot 1 --midi-channel 1 --boot-reset`, registered
stimulus `STIM-BASE-2-2-01`, reused unchanged) run three times: twice on the fixed image and once on an
image with only the queued call removed (one-line mutation, source restored byte-identically and the fixed
image re-flashed afterwards). Identity from `metadata.json`: `git_commit 300af1fa`, `git_dirty true`; fixed
`firmware_hex_sha256 0e7d549e…`, A/B `df665ce5…`.

| Observation | `s4f20-ab-prefix-02` (pre-fix) | `s4f20-lencheck-01` (**fixed**) | `s4f20-record-exit-snapshot-02` (fixed) |
|---|---|---|---|
| Take / content | 2304, notes to 1770 (crosses frontier 1536) | 2304, notes to 1744 (crosses frontier 1536) | 1536, notes to 1494 (below frontier) |
| `snapshot_unit,kind=publish` after the record stop, before the first overdub | **no** — first publish only after the overdub stop | **yes** (`a=1`) | **yes** (`a=1` at 24.459 s vs `Live Overdub` 24.862 s) |
| Root after the first overdub stop | `a=1` (OD1 only) | **`a=3`**, 48 notes = 32 + 16 | `a=2` |
| Record notes in the published root | **26/32** — omitted 74–79 at 1530–1770 (= the frontier set) | **32/32** (`from=snapshot,notes=32`, `srcspan count=32`) | 28/32 (see below) |
| Snapshot-derived display after the first overdub stop | `visual=42` = record's last bar missing (**the reported class**) | `DISP … frame=48 visual=32`; `srcspan count=48` (**complete**) | `visual=44` |
| Verdict | `INCONCLUSIVE_CAPTURE`, `overall_ok=false` | **PASS**, `overall_ok=true` | `INCONCLUSIVE_CAPTURE` on the documented residuals |

**The reported symptom is fixed on device**, on the *matched* pair: two 3-bar takes whose note content
crosses the completed-bar frontier (both at raw 1997/2022, notes to 1744/1770) — the pre-fix image publishes
26/32 and loses the last bar, the fixed image publishes 32/32 and the display is complete (the user's
recorded observation — no disappearing bar-3 notes, no shortened note at the end of bar 2 — matches the
fixed run's numbers: `visual=32` on a 32-note record pass, `48` after the first overdub). All
change-relevant gates are green in every run: record grid `32 delta 47-49 bad=0`, `CLN record sent=32`,
overdub `16/16`, second overdub `8/8`, `display … overdub_ok=True transport_ok=True`, `MIDI-boundary …
overflow_free=True call_sites=True`, `RAM2 77 824 B`.

**The 28/32 observation is one unreproduced run, not a standing defect.** The fixed 2-bar run held 28 of 32
record notes (missing pitches 76–79) — a set the frontier cannot explain (its content ends at 1339, under
its 1536 frontier) — but the fixed 3-bar run reproduces neither the shape nor the shortfall (32/32), and the
pre-fix run's shortfall is fully frontier-explained. So it is **seen once in two fixed runs**; its
distinguishing feature is that run's host pacing state (the host's tick model reported 355 ticks where the
device ran raw 1891 — a ~5× divergence of the wall-clock tempo proxy: all runs print `Bar sync via
wall-clock tempo proxy (120.0 BPM) — Teensy clock not on USB MIDI in`). Open as "28/32 seen once";
candidates remain the note-pair resolution across fragment caps or a loop-phase/rotation projection, and the
tree's `vch`/sameid instrumentation emitted no rows in these builds, so its gate or call site needs checking
before it can attribute this. No content loss in any run: the passes hold the notes.

**Take length is host stop-press timing; this slice cannot change it.** The *same fixed image* produced a
2-bar take (raw 1891) and a 3-bar take (raw 1997) on consecutive runs. The length is stamped in the stop
**admission** (`Track::beginDeferredRecordStopForSlot` → `computeRecordStopLengthTicks(rawLength,
captureMaximumTick())`, `src/Track/TrackOverdubLifecycle.cpp` ~641), i.e. *before* `stepDeferredRecordPublication`
and its new queue call. The 2-vs-3 flip is the device's bar quantization (`quantizeTransportRecordLength`:
nearest bar while the overshoot ≤ `ticksPerBar/2` = 384, else up): raw 1891 − 1536 = 355 ≤ 384 → **1536**;
raw 1997 − 1536 = 461 > 384 → **2304**. Every run so far stops ~0.46–0.63 bar late (original runs raw 1989,
2020; new runs 1891, 1997, 2022), so the outcome sits on that edge and moves with the *host*, not the
firmware. The instrument for it is the host's strict MIDI-clock mode (`--require-midi-clock`, refusing
wall-clock pacing), which needs the device's clock on USB MIDI in — the existing open item, unchanged here.
Evidence dirs: `captures/s4f20-record-exit-snapshot-0{1,2}`, `captures/s4f20-ab-prefix-0{1,2}`,
`captures/s4f20-lencheck-01` (the `-01` variants of the first two are refused short-stream attaches).

## Residual (recorded, not silently dropped)

The **enabler** is DEC-073's watermark being written unconditionally by `queueSessionExitSnapshot`,
while the completeness direction ("every committed pass is enumerated before a root is stamped
current") is not enforced anywhere: no guard compares the committed-pass inventory with the box's
tracked set before `storePublishedSnapshot`. This slice removes the one production path that violated
it and pins that path with fixtures; a general completeness guard is a **separate** change because it
alters when a session-exit root may publish at all (a wrong predicate would turn the current silent
truncation into a **permanent publication gap**, the class `record_stop_snapshot_publication_gap_display_bugfix.md`
RC3 documents). DIRECTION: on a session exit, gate the watermark write on the box covering the
committed inventory, and keep an incomplete enumeration pending rather than current — with its own
fixtures for the "no content beyond the frontier" and "filtered-empty take" shapes.
