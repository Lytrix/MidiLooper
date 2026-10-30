## Why

`session_20261029_190227.log` shows the main loop blocking for hundreds of milliseconds inside a
single deferred idle-owner turn while one track was overdubbing: `idle_maint` **94.9 / 332.2 / 88.9 /
230.1 ms** and `persist_save` **50.9 / 213.5 ms**. The downstream effects in the same window are the
owner's reported symptoms: `midi_gap` **332 ms** (101 late events in that 5 s block), `clk` 70.5 ms,
`usbdev`/`usbdisp` 70.5 ms of catch-up dispatch, `extclk_arrival` drifting **46 → 108 → 172 → 243 ms**,
and 12 `win_gap` overruns. The audible result is tempo wobble; the visible result is dropped display
frames.

The firmware already declares the bar — `LoopContentResolution::kDeviceGateSliceBudgetUs = 50000`
(the "5.7 `idle_maint` bar", same as `kLoopRemainderOneShotUs`), so **every `loop_rem` row is a
declared-bar breach**. Nothing stops a turn from breaching it, because `runDeferredIdleMaintenance`
(`src/main.cpp`) dispatches children whose per-turn cost is not bound anywhere. The maintained
inventory already says so: [`ExecutionBounds.md`](../../../docs/Authority/Architecture/ExecutionBounds.md)
row 19 (`processDeferredIdleMaintenance × 8`, **"Unbounded in aggregate"**), row 12
(`prewarmOverdubSourceSpanCache`, **"Unbounded"**), rows 5/8/9/10 (the whole-loop
`resolveDisplayNotes(passes, …)` fallback and its helpers, **"Unbounded"**).

This matters now because two recent families of work made the lane busier without making it bounded:
the rolling playback window lane (DEC-203/200) and the boot/focus visual-cache prewarm (DEC-210). The
measured evidence says those lanes are **not** the overrun — their grains are 0.175–0.684 ms — but
they compete for the same turn, so every existing unbounded child now starves them and the MIDI
boundary with it.

## What Changes

**Owner-level contract first.** While `timingCriticalTrackActive`, `runDeferredIdleMaintenance` may
only dispatch work whose per-turn execution bound is **declared and enforced**. An unbounded child
returns a **named deferral** rather than executing. The contract is one predicate at the owner
boundary, not a rewrite of each preparation; the deferred-owner rotation, the single-grain discipline
and every existing owner keep their identity.

**The scheduling budget is not the breach threshold.** The 50 ms declared bar stays the alarm and the
acceptance criterion. Children are admitted against a strictly smaller **owner work budget** that
leaves room for the turn's own scans, the MIDI-service polls, telemetry and the breach record, and
admission **reserves a child's declared maximum before dispatching it** — a child that does not fit is
not run, and a child may take a smaller remaining deadline only if it is resumable at a finer bounded
grain.

- **BREAKING (internal):** an unbounded child that used to run on a realtime turn now runs on a
  non-realtime turn or not at all until it is bounded. Late display/content settling is the accepted
  cost; a blocked MIDI/clock boundary is not.
- **First behavioural repair — touched-coverage source preparation.** The two whole-pass
  `resolveDisplayNotes(passes, …)` calls in `Loop::rebuildOverdubSourceSpanCache`
  (`src/Loop/LoopCapture.cpp`) have already left the realtime lane, replaced by a windowed,
  wrap-padded derive. That derive becomes the **grain** of the chosen architecture, not the mechanism:
  preparation is driven by the **loop-relative phase regions the session has actually touched**
  (monotonic, tiled, `sourceGeneration + requiredCoverage + preparedCoverage + pendingCoverage`), and
  candidate lookup is widened by **conservative closure** — the maximum committed source-note span,
  which may over-admit but never under-admit. Playhead movement and loop wrap neither move nor
  invalidate prepared coverage, so work is paid once per touched region per generation instead of once
  per wrap. `kOverdubSourceWindowBars` and the playhead-centred window stop being correctness
  mechanisms; whole-loop prewarm becomes optional.
  Required coverage is **"phases touched so far"**, not the final note span: NoteOn marks the pressed
  tile, held advancement marks each newly crossed tile, and NoteOff/stop seals the complete span — none
  of them derives. Three **named prerequisites** replace the inline derives: occupancy at the pressed
  phase, held participation, and span finalization; none blocks the MIDI path and "fallback refused" is
  never read as "not occupied". A **tile is a correctness/publication unit, not an execution bound** —
  the worker's grain is its own resumable event/operation cursor, so a dense tile or a long closure
  cannot enlarge a turn. Closure composes by note identity and saturates to the whole loop rather than
  producing ambiguous modular ranges. This child is load-bearing for overlap correctness, so it **cannot
  simply be deferred**: the admission rule and this bounded replacement become effective atomically.
- **Attribution before restructuring — persistence.** A report-only diagnostic names the indivisible
  `persist_save` step that produced a breach (the 213 ms span emitted no `PERS,work` row, so the step
  is currently unnamed). Persistence *splitting* is a later slice, sequenced after the first device
  oracle, not a first target; **the persistence owner's bound class does not move until that split
  enforces it.**
- **Capture diagnostics separated from production correctness.** `deferredStoredMidiVerification`'s
  effective-notes phase (`copyEffectiveCommittedEvents` + `resolveDisplayNotes` +
  `appendOverdubPassDisplayNotes`) and the `SEVT` dump run whole-loop resolves on the same owner. Both
  are marked unbounded and denied while timing-critical **in the first admission slice**, so a capture
  run cannot hide whether the production lane is clean and the zero-breach acceptance is reachable.
- **Acceptance oracle (same overdub shape, next device run):** zero `loop_rem,idle_maint`, zero
  `win_gap` overruns, **maximum MIDI-service and clock-dispatch gaps within their declared bounds** and
  collapsed against the pre-change run, and any remaining `persist_save` breach identified by named
  step. "Every turn under the bar" is not sufficient on its own.


Out of scope for this change: a new scheduler or work-budget framework; raising or amortising the
`loop_rem` threshold; removing the whole-pass resolve from STOPPED paths (only the realtime lane
loses it); the rolling playback window builder's internal grain; persistence restructuring before the
oracle run.

## Capabilities

### New Capabilities

- `deferred-idle-owner-admission`: the contract that governs what a deferred idle-owner turn may
  execute while the transport is timing-critical — declared/enforced per-turn bounds, named deferral
  for unbounded children, the aggregate-turn requirement across all tracks and owners, the
  observation rows that make a breach self-describing, and the separation of capture-only
  diagnostics from production correctness.

### Modified Capabilities

- `overdub-pass-overlap-resolution`: the `overdubSourceView` semantic contract does not change, but
  its **preparation** does — coverage is the set of loop-relative **tiles the session has touched**
  (monotonic, generation-qualified), prepared by a resumable **event/operation grain** off the realtime
  lane; a tile in flight is never a source, and the three decisions that need coverage are **persistent
  named prerequisites** rather than inline derives.

## Impact

**Firmware**

- `src/main.cpp` — `runDeferredIdleMaintenance` owner-boundary admission; the `idle_maint` span and
  its `loop_rem` report.
- `src/Track/TrackDeferredMaintenance.cpp` — the overdub source-view branch, the VisualCache lane's
  children, and `deferredStoredMidiVerification` eligibility.
- `src/Loop/LoopCapture.cpp` — `rebuildOverdubSourceSpanCache` / `rebuildOverdubSourceView` windowed,
  budgeted, resumable build; `overdubSourceSpanCacheFullLoop_` completion contract.
- `src/Loop/LoopVisualCache.cpp` — only where an unbounded child is reached from the same turn
  (rows 8/9/10 of the inventory); the slice itself is already bar-sized.
- `src/StorageManager/*` — report-only step naming first; splitting later.
- `include/Utils/MidiServiceDrain.h` — the admission predicate beside the existing owner policy
  helpers (no new scheduler).

**Diagnostics and docs**

- New/extended capture rows: the deferral reason, and the named indivisible persist step. Any new
  literal must be registered with `CaptureLineTier` or it is shed exactly where it is needed.
- `docs/Authority/Architecture/ExecutionBounds.md` — inventory rows 5/8/9/10/12/14/19 move class or
  gain a declared bound in the same change that enforces it (rule 3 of that document).
- `docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md` and
  `docs/Guides/OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md` — closeout, if the respective behaviour
  description moves.

**Verification**

- Native: a production-path fixture that runs the overdub chain with a timing-critical track and
  asserts zero `loop_rem`-class overruns and a named deferral from every unbounded child; a RED that
  fails if an unbounded child is dispatched on a timing-critical turn; a windowed-build fixture
  proving parity with the whole-pass resolve and that an incomplete build is never served.
- Device: one run of the same overdub oracle, read against the four acceptance numbers above.

**Related**

- `docs/DELIVERABLE_TRACKING.md` — the timing-boundary entry this change closes.
- [`windowed-playback-generation`](../../specs/windowed-playback-generation/spec.md) — its "bounded
  service opportunity" requirement is the sibling contract for the playback lane; this change adds
  the aggregate owner rule that keeps that lane's opportunity from being eaten.
- [`deferred-job-scheduler`](../../specs/deferred-job-scheduler/spec.md),
  [`resumable-midi-boundary-playback-maintenance`](../resumable-midi-boundary-playback-maintenance/proposal.md) —
  precedent for bounded deferred owners on this same machinery.

## Known, assumed, unknown

**Known (measured, `session_20261029_190227.log`)**

- The 332 ms `idle_maint` turn is `rebuildOverdubSourceView(tick,"idle")`; its sub-spans are 0.4 ms
  incremental gather, 17.3 ms `cer_win`, **201 ms** `lcr,pairrows,at=passes,count=610`, 110 ms of
  remainder.
- The same call cost 36.5 ms at 495 notes and 201 ms at 610 notes in one session — two samples that are
  consistent with growth in the take, not with wall time. A later native characterization measured
  **approximately linear** cost in operations/notes (310 notes/311 ops/523 us host; 160/161/221;
  40/41/52). No superlinear production derive is demonstrated; the earlier O(n^2) hypothesis was not
  confirmed and is not relied on.
- `persist_save` breached at 213 ms against a 300 µs active budget with no `PERS,work` row naming the
  step.
- The rolling window lane served 0.175–0.684 ms grains and was denied turns (`win_gap_overruns`).

**Assumed (evidence-backed, not proven)**

- `persist_save`'s 213 ms is one indivisible finalize/SD step, matching the already-recorded defect in
  `openspec/changes/loop-content-resolution/tasks.md` ("Persist slice budget while PLAYING is 300 µs;
  one finalize/SD step overruns"). The named-step diagnostic is what will confirm it.

**Unknown**

- The exact indivisible persistence step and its cost distribution.
- Device cost per prepared tile at the supported density. The figures used so far (a 4-bar tile being
  roughly 20 ms, a 2-bar tile roughly 10 ms) are **estimates extrapolated from host measurements**, not
  direct device measurements; the device oracle is what must confirm them.
- Whether a worst-case dense tile plus long closure fits one worker grain; the dense-tile RED measures it
  rather than asserting it.
