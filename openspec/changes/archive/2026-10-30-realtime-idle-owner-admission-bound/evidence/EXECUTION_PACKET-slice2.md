# G2 execution packet — slice 2: windowed overdub source view (atomic with slice 1)

Per [`EXECUTION_PACKET.md`](../../../../docs/Templates/EXECUTION_PACKET.md). Tasks 1.4 and 4.1–4.13.

> **Lands in the same commit and image as `RIAO-1`.** The overdub source view is load-bearing for
> overlap correctness: an image in which its whole-loop refresh is interdicted while no bounded
> replacement exists would make the view stale (design D3).

## Identity and scope

- **Packet ID:** `RIAO-2`.
- **Approved references:** `specs/overdub-pass-overlap-resolution/spec.md` (bounded resumable
  preparation; coverage rule; assembled-cache parity), `design.md` D3/D3a, `evidence/DECISION_REVIEW.md`.
- **Governing decisions:** C3.1/C3.2/C3.5 (a scan in progress is not a source; no partial prefix mixed
  with a committed suffix), DEC-181 (windowed resolve with identical per-pass local pairing/`NoteId`s),
  DEC-043 (bound class gated; local budgets do not reclassify).
- **Status:** specified.
- **Owner and entry path:** `Track::processDeferredIdleMaintenance` (`src/Track/TrackDeferredMaintenance.cpp`),
  the `deferredDerivedViewMaintenance && isOverdubbing()` branch → `Loop::rebuildOverdubSourceView(refreshTick, "idle")`
  (`src/Loop/LoopCapture.cpp`) → span cache. Reader: `overdubSourceViewNotes_` on the overlap path
  (`src/Loop/LoopPendingNoteChange.cpp`).
- **Primary files:** `src/Loop/LoopCapture.cpp` (`rebuildOverdubSourceView`,
  `rebuildOverdubSourceSpanCache`, `resolveOverdubSourceWindow`), `src/Loop/LoopPrivateSource.cpp`
  (private-predecessor staging, unchanged), `src/Track/TrackDeferredMaintenance.cpp` (admission of the
  refresh), `include/Loop.h` (per-slot cursor/mask), `include/LoopContentResolution.h` (windowed
  primitive, unchanged).
- **Existing harness:** `test/test_shared_capture_finalization` (overdub chain, overlap deltas),
  `test/test_overdub_*`, the DEC-181 fixtures in `test/test_loop_content_resolution`,
  `test/test_live_capture_display_boundary`.
- **Preconditions present:** `resolveDisplayNotesInWindow` + `CommittedEventRange::inWindow`
  (DEC-181); `kOverdubSourceWindowBars = 16`; `overdubSourceSpanCacheReady/Reusable`;
  `privatePredecessorSourcePending`.
- **Prerequisites:** slice 1's predicate (the refresh is dispatched through it).
- **External blockers:** none. New per-slot state goes in the slot's external working set; any
  file-scope state goes to `DMAMEM`.
- **Explicit non-goals:** no change to overlap evaluation semantics, `resolveConstrainedGeometry`, the
  delta grammar (Add/Shorten/Hide), the private-predecessor scan, or the session-open path
  (`rebuildOverdubSourceView(playhead, "open")` keeps its existing contract, including its 50 ms gate).
- **Commit boundary:** same commit as `RIAO-1`.

## Two roles, fixed before any edit

1. **The view is the authority for its interval.** `overdubSourceViewNotes_` is cut for the window the
   overlap path will evaluate. It is derived **directly** from the committed passes over that window
   (plus the closure/wrap pad), not from the whole-loop vector.
2. **The whole-loop cache is a prewarm.** It is assembled window by window, is not usable until
   `fullLoop` holds **and** assembled-parity holds, and is never the reason a view refresh is denied.

**Coverage rule (the contract invariant).**

> An established view is authoritative only while its coverage contains the interval currently required
> for overlap evaluation. Outside that coverage, stale geometry is never served as current.

## Oracle fixed before behavior edits

### Case `RIAO-2.A` — windowed derive parity for an ordinary window

- **State:** loop > 16 bars; committed record + N overdub passes; a window inside the loop.
- **Expected:** per note, `(noteId, pitch, startTick, endTick)` identical between the windowed derive
  and the whole-loop derive restricted to the same window (plus pad).
- **Counterexample:** any identity, pitch or geometry difference; any note present in one and not the
  other within the window.

### Case `RIAO-2.B` — wrap-crossing window parity

- **State:** as A, window straddling `loopLengthTicks`.
- **Expected:** identical geometry including the wrap representation (`endTick < startTick` where the
  whole-loop path produces it); a note crossing the wrap appears exactly once.
- **Counterexample:** the windowed path drops the wrap pair (`appendOverdubPassWrapPairedNotes`' job)
  or emits both halves as two notes.
- **Note:** this is the design's stated UNKNOWN; if it cannot be satisfied by the pad, widen to two
  windows and record it in `design.md` D3.

### Case `RIAO-2.C` — `Prior` origin window completeness

- **State:** cache origin `Prior` (the capture's `from=prior` shape), a window whose notes are not all
  in the prior cache.
- **Expected:** the windowed derive supplies the complete `NoteId` set for the window; no whole-pass
  merge runs.
- **Counterexample:** a note present in the passes but missing from the served window.

### Case `RIAO-2.D` — assembled-cache parity gates `fullLoop`

- **State:** all windows derived incrementally into one cache.
- **Expected:** the assembled cache equals the whole-loop derivation on: boundary-crossing notes,
  loop-wrap notes, repeated/simultaneous same-pitch notes, `NoteId` identity, start/end geometry, and
  counts in both directions.
- **Counterexample (must red):** a boundary-crossing note dropped; a wrap note duplicated; a
  same-pitch pair collapsed; a re-issued `NoteId`.

### Case `RIAO-2.E` — coverage fixture A (previous view usable)

- **State:** established view whose coverage contains the requested interval; refresh incomplete.
- **Expected:** previous view used; deferral record states coverage held; no partial adopt.
- **Counterexample:** the incomplete refresh is merged or the deferral is reported as served.

### Case `RIAO-2.F` — coverage fixture B (stale never consumed)

- **State:** requested interval **outside** the established view's coverage; refresh incomplete.
- **Expected:** stale geometry is never returned as current. The bounded refresh runs (possibly at a
  finer grain) before that interval is evaluated; if it cannot run, the turn fails loudly with a named
  reason.
- **Counterexample:** the old window is returned and overlap evaluation proceeds against it.

### Case `RIAO-2.G` — prewarm is not load-bearing

- **State:** whole-loop cache permanently incomplete.
- **Expected:** session overlap results identical to a run where the cache completes.
- **Counterexample:** any difference in the committed deltas.

### Case `RIAO-2.H` — no whole-pass resolve on a timing-critical turn

- **State:** overdub chain, transport running, refresh owed.
- **Expected:** zero whole-pass display-note resolves on the turn; the turn's admitted work is inside
  the owner work budget.
- **Counterexample:** one `resolveDisplayNotes(passes, …)` reaches the turn (assert with a counting
  seam on the production function).

## Controls and coverage

- **Seams reused:** `LoopContentResolution::resolveDisplayNotes` / `resolveDisplayNotesInWindow` (both
  production), the DEC-181 `ResolutionCostCounters`, and the existing `dumpOps` counters for the
  windowing claim.
- **Production paths exercised:** `Track::processDeferredIdleMaintenance` is already driven directly by
  several suites; `rebuildOverdubSourceView` is production code with no test-only branch.
- **Busy/stale/failure:** publication gap (`isPlaybackSessionExitPublicationGap`), root not current,
  revision moved, memory admission refusal, source-identity change mid-build (must restart, not mix).
- **Numeric limits:** the per-turn derive budget is in work units natively and microseconds on device;
  the device claim comes from `loop_rem,idle_maint` being absent plus the named deferral rows.
- **Bounded drain:** each fixture uses a bounded turn loop and prints the cache mask / cursor on
  failure.

## Verification recipe

- Focused: `pio test -e native -f test_shared_capture_finalization -f test_loop_content_resolution
  -f test_overdub_pass_overlap_resolution` (name as present) and the new parity suite.
- New suite: `test_overdub_source_window_parity` (cases A–H).
- Broad: `pio test -e native`; build `pio build -e teensy41-capture-serial`; RAM1 unchanged.
- Device: same oracle run as slice 1 (tasks 5.2–5.5).
- Evidence path: `openspec/changes/realtime-idle-owner-admission-bound/evidence/`.

## Completion record

- **Oracle/contract unchanged, or authorized change and supporting evidence:** the parity fixture exists
  and is RED against the current code — see `2026-10-29-SLICE2-PARITY-RED.md`. **No production edit made
  to `LoopCapture.cpp`; the windowed derive is not wired.**
- **Fixture implemented and discriminating assertion demonstrated:** `test_overdub_source_window_parity`
  — B and E **PASS** (both only after the pad became loop-aware); A, C, D **FAIL** on the chunk-span
  gate. That failure is discriminating: it distinguishes "the window is wrong" from "the chunk span
  hides the content", which is what the gate exists for.
- **Native verification:** focused run reported above; full suite **NOT RUN** for this suite's slice
  (the suite is expected red until the chunk-span question is answered).
- **Build/RAM1 verification:** target build **SUCCESS**, RAM1 unchanged (see packet `RIAO-1`).
- **Device verification:** NOT RUN (blocked on the RED above).
- **Required reviews and durable docs:** `design.md` D3 updated with both consequences (loop-aware pad;
  chunk admission decides soundness) and `tasks.md` 4.1 annotated. `ExecutionBounds.md` row 12
  NOT DONE (no enforcing firmware yet).
- **Task IDs eligible for checkoff:** none. 4.1 is **partially** executed with the residual named.
