## Context

See `proposal.md` — Why. This section records only the constraints the approach has to respect.

**Measured offender (device, `captures/session_20261029_190227.log`).** During one overdub chain the
deferred idle-owner turn blocked four times past the declared 50 ms bar
(`LoopContentResolution::kDeviceGateSliceBudgetUs`, the "5.7 `idle_maint` bar", same value as
`kRuntimeTiming...kLoopRemainderOneShotUs`). The largest is fully attributed:

| offset in turn | row | work |
|---|---|---|
| +0.4 ms | `lcr,srcskip,at=copy,reason=not_finished` | incremental committed gather (resumable) |
| +17.3 ms | `cer_win,us=16660` | bounded 17-chunk window copy |
| +218.8 ms | `lcr,pairrows,at=passes,count=610` | whole-pass `resolveDisplayNotes(passes, …)` |
| +329.4 ms | `lcr,src,why=idle,from=prior,…` | `rebuildOverdubSourceView` return |
| +332.2 ms | `loop_rem,idle_maint` | turn over the bar |

The same call cost 36.5 ms at 495 notes and 201 ms at 610 notes in one session — the cost tracks the
take, not wall time. The second production breach (94.9 ms) is the sibling whole-pass seed in the same
function (`from=passes`, 36.5 ms resolve + 16.5 ms gather). The other two (88.9, 230.1 ms) are
capture-only paths: a `SEVT` dump and `deferredStoredMidiVerification`'s effective-notes phase, which
runs `copyEffectiveCommittedEvents` + whole-pass `resolveDisplayNotes` + `appendOverdubPassDisplayNotes`
on the same owner.

**What is already bounded (do not regress while fixing).**

- The rolling playback window lane (`Track::stepPlaybackViewMaintenance`) served 0.175–0.684 ms grains;
  it was **denied turns** (12 `win_gap_overrun`, `overMax=6` ticks in one 30 s block), not overrunning.
- `rebuildVisualCacheIdleSlice` is bar-budgeted and DEC-045 keeps it off during OVERDUBBING: the run
  has `VCACHE,stale_all` throughout the overdub and zero slices, i.e. the gate holds.
- `cer_win` (windowed chunk copy) and the incremental committed gather are bounded and resumable.

**Declared inventory state (authority).** `docs/Authority/Architecture/ExecutionBounds.md` rule 1
requires every interval-path owner to have a row; rule 2 says adding or extending an **unbounded**
owner is a design session; rule 3 says reclassifying a row updates the table **in the same commit as
the firmware that enforces it**. Current rows: 19 `processDeferredIdleMaintenance × 8` = **Unbounded
in aggregate**; 12 `prewarmOverdubSourceSpanCache` = **Unbounded**; 5/8/9/10 = the whole-loop
`resolveDisplayNotes` fallback and its helpers, **Unbounded**; 14 `processDeferredSaveState` =
**Unbounded in effect** — it carries a nominal 300 µs / 3000 µs budget that is checked only *between*
steps, so an indivisible step can and does exceed it (measured 213 ms); 27 playback window = **not
admitted**. Row 14 stays in that state until its step is split (this change's later section); naming
the step is not a bound.

**The `overdubSourceView` contract the fix must not weaken.** `overdubSourceViewNotes_` is the overlap
candidate domain; it is cut from the full-loop `overdubSourceSpanCacheNotes_` filtered by
`resolveOverdubSourceWindow` (16 bars, `Loop::kOverdubSourceWindowBars`, centred on the playhead, or
the whole loop when the loop is ≤ 16 bars). `overdubSourceSpanCacheReady()` gates on
`privatePredecessorSourcePending`, loop-length match, **full loop** and the current `playbackRevision`;
`overdubSourceSpanCacheReusable()` additionally pins the published-snapshot revision/identity when a
root is current. The C3.2/C3.5 rule stands: a private-predecessor scan in progress is not a source, and
a partial prefix must never be mixed with a committed suffix.

**The key structural fact for item 1.** The **view** is window-scoped by construction; the **cache** is
whole-loop only because the view is re-cut from it on every wrap. `resolveDisplayNotesInWindow` already
performs the same per-pass local pairing and `NoteId` assignment over a chunk-range window
(`CommittedEventRange::inWindow`), which DEC-181 introduced for the display path. So the view can be
cut from a *windowed derive of the same content* instead of from a whole-loop vector, with the same
candidate domain.

## Goals / Non-Goals

**Goals**

- One declared, enforced budget for the **aggregate** deferred idle-owner turn while any track is
  timing-critical, expressed as one admission predicate at the owner boundary.
- A named, observable, lossless deferral for every child that cannot run under that budget.
- Item 1: no whole-pass display-note resolve executes on the realtime lane from
  `rebuildOverdubSourceSpanCache`; the source view keeps its exact candidate domain while its
  preparation becomes **touched-coverage tiles** prepared by a resumable event/operation grain.
- Attribution, not restructuring, for `persist_save`: a report-only row that names the indivisible step.
- Capture-only work stops being able to make the production lane's acceptance unreadable.

**Non-Goals**

- No new scheduler, work-budget framework, `RuntimeWorkBudget::admit(WorkClass)`, interval reservation,
  or extra `handleMidiInput()` call sites — all explicitly rejected by `ExecutionBounds.md`.
- Not raising, amortising or hiding the `loop_rem` threshold; the 50 ms bar and its report stay as they are.
- Not removing whole-pass resolves from STOPPED or boot paths. This change is about the realtime lane.
- Not changing the rolling playback window builder's grain, its round-robin policy, or DEC-045's
  "no visual-cache reconstruct while overdubbing" gate.
- Not restructuring persistence in the first slice; not touching the stop path (no full validate on a
  record/overdub stop; deferred save stays `requestDeferredSaveState`-driven).
- Not changing `overdubSourceView` *semantics* — only how it is prepared.

## Decisions

### D1 — The bound is a property of the turn, declared once, enforced at one place

A single predicate lives beside the existing deferred-owner policy helpers in
`include/Utils/MidiServiceDrain.h` and is consumed by `runDeferredIdleMaintenance` in `src/main.cpp`.
It answers, for a named child, one of three things: **run**, **defer(named reason)**, or
**not owed**. The registry of "which children are bounded" is compile-time data next to the predicate,
so adding a child without declaring a bound is a visible omission rather than a silent overrun.

Realtime admission composes with, and does not replace, the existing per-owner gates
(`allowVisualCacheReconstructSlice`, `aroundIdleMaintenance`, `serviceCanonicalPrefixTurn`'s
priority-work check, the DEC-045 overdub gate).

*Alternatives rejected.* (a) Per-child budget checks scattered through each preparation: eight places
to keep true, and no place that can report the aggregate. (b) A runtime work-class scheduler: rejected
by the authority document, and it would make the bound a policy engine instead of a property.
(c) Raising the bar for timing-critical turns: hides the breach the oracle is supposed to count.

### D2 — The scheduling budget is not the breach threshold

The 50 ms figure has two different jobs and they must not be conflated:

- **Breach threshold** (`kRuntimeTimingTelemetry::kLoopRemainderOneShotUs`, the declared `idle_maint`
  bar): the alarm. A turn that exceeds it is a breach and fails acceptance. It stays at 50 ms.
- **Owner work budget** (`kOwnerWorkBudgetUs`, new): the amount of *child work* an idle-owner turn may
  admit. It must be strictly smaller than the threshold, because the threshold also has to carry the
  turn's fixed overhead — the owner scans (track walk, `hasPendingSnapshotPublishOnAnySlot`, the
  visual-cache wrap-append scan), the `handleMidiInput()` polls around the turn, the telemetry and
  `loop_rem` emission itself, and the deferred-owner bookkeeping. Giving the children the entire 50 ms
  guarantees that some turn lands at 50 ms + overhead and breaches.

`kOwnerWorkBudgetUs` is derived from the **MIDI clock service interval**, not from half the alarm. An
uninterrupted child that runs longer than one clock interval guarantees a delayed clock edge — that is
the tempo wobble this change exists to remove. The measured interval is **~20.8 ms**
(`DIAG,service_period,20832`; 24 PPQN at ~120 BPM), so an earlier draft's 25 ms was **already over
budget** and is corrected here to **8 ms**.

The rule is `kOwnerWorkBudgetUs + turnOverheadUs < minimum MIDI clock interval`. 8 ms leaves ~12.8 ms of
the measured interval for the turn's own scans, the MIDI polls, telemetry and the breach record. It
remains a **recorded tunable**: the first device oracle measures the residual and the supported tempo
range, and a faster supported tempo shrinks the interval (and this value) with it.

**Do not size coverage tiles or worker grains against this number.** A tile is a
correctness/publication unit; a grain is an execution unit bounded by its own event/operation cursor.

**Reserve before dispatch.** Admission reserves a child's **declared maximum grain** before the child
runs:

```
declaredChildMaxUs <= remainingOwnerWorkBudgetUs
```

A child whose declared maximum does not fit is deferred with a named reason — it is never dispatched
into a budget it can only overshoot. A child may instead receive a *smaller remaining deadline* only if
that child is **resumable at a finer bounded grain** and declares that grain; "run it and hope it stops
early" is not admission.

`loop_rem` remains the **backstop and acceptance alarm**, not the enforcement mechanism: it reports what
actually happened, and the fact that it is the only breach detector is a design defect, not a control.

The existing `idle_maint` span already wraps the whole turn in `main.cpp`, so the aggregate is measured
rather than inferred. Each child is told the remaining budget for this turn; its effective bound is
`min(declaredChildMax, remainingOwnerWorkBudget)`.

*Alternatives rejected.* (a) Using the 50 ms bar as the child budget: leaves no room for the turn's own
cost and makes a breach a matter of luck. (b) Per-child budget checks scattered through each
preparation: eight places to keep true and no place that can report the aggregate. (c) Summing declared
child maxima and refusing to stack them at all: would starve the rotation and reintroduce the "blocked
holder monopolises service" defect the playback lane explicitly fixed.

### D3 — Touched-coverage preparation (the chosen resolution of 4.5)

**Superseded.** An earlier version of this section cut the view from a *window centred on the playhead*.
That shape was measured and rejected as the architecture: it must re-derive on every wrap, its cost is
paid repeatedly, and it cannot bound a coverage transition without serving stale geometry (the 4.5
blocker). The windowed derive built in tasks 4.2–4.4 remains in the tree as the **grains** the new
architecture uses; it stops being the correctness mechanism.

**The domain is what the session touched, not what is near the playhead.**

```
sourceGeneration : the committed content identity the coverage is qualified by
requiredCoverage : the loop-relative phase regions the session's accepted notes touch (monotonic)
preparedCoverage : the regions derived and published for sourceGeneration (per region, atomic)
pendingCoverage  : the regions owed to the deferred owner
```

- **Required coverage is "phases touched so far", not "the note's final span".** The final span is not
  known at acceptance, so the three transitions are separate and each is cheap:
  - **NoteOn** marks the **pressed-phase tile** required;
  - **held advancement** marks each newly crossed tile required as the playhead continues through the
    held note;
  - **NoteOff / stop** seals the complete span and ensures every tile in it is required.
  None of the three performs a source derivation.
- Required coverage never becomes unrequired during the session. "Repeated wraps add nothing" is
  precise: a wrap adds nothing **unless a held note actually continues into an as-yet-untouched phase**.
- The bounded deferred-maintenance worker prepares newly required regions once. Playhead movement and
  loop wrap neither invalidate nor move prepared coverage.
- Coverage is a set of **bounded tiles**, not one extent: notes at bar 2 and bar 27 require those two
  regions, not the 25 bars between them, and elapsed wraps cannot manufacture touched coverage.
- A source-generation change **does not** discard `requiredCoverage`: that is session state describing
  what this overdub touched, and the session is still semantically alive across a commit that moves the
  generation. Instead, `preparedCoverage` and any in-flight preparation are invalidated, and **every
  currently required tile becomes pending again** for the new generation. (If a future change makes a
  generation change always terminate the session, that must be stated explicitly rather than implied.)

**Conservative closure.** Candidate lookup is expanded backward by the **maximum committed source-note
span**, so a committed note that starts before a required region and sustains into it is still a
candidate. This is conservative closure, not an exact domain: a global maximum may over-admit, never
under-admit. A per-pitch/per-region index is explicitly future work.

**Where coverage is actually required (characterized, not assumed).**

| point | what it needs | today |
|---|---|---|
| NoteOn (`TrackCaptureInput.cpp:222-232`) | occupancy at the **pressed phase**; playback ledger is primary, source view is the fallback | synchronous, one tick |
| note held (`ensureOverdubSourceNotesForHold`) | participation for the held pitch as the playhead advances — the 94.9 ms `why=private_hold` path | synchronous |
| NoteOff (`TrackCaptureInput.cpp:1106-1130`) | the **complete note span** → Shorten/Hide | synchronous |
| stop finalization (`TrackCaptureInput.cpp:720-745`) | the same, for notes still held at the stop | synchronous |

So preparation may lag a held note's *remaining* span, but two points need source information on the
MIDI path: the pressed tick (occupancy) and the note's completion. Those become **named prerequisites**,
not inline derives and not a blocked event path:

- `CoveragePrerequisite::OccupancyAtPhase(phase)` — **wired on the NoteOn path**, in this order: the MIDI
  note is accepted immediately; the pressed-phase demand is marked; then ledger authority is consulted.
  If the ledger cannot establish the answer **and** the tile is not prepared, the overlap decision
  becomes a named owed prerequisite. "Fallback refused" is never read as "not occupied".
- `CoveragePrerequisite::HeldParticipationAtPhase(phase)` — **wired on the hold path**, replacing
  `ensureOverdubSourceNotesForHold`'s synchronous derive (the 94.9 ms `why=private_hold` breach): mark
  the currently touched tile, consume only prepared source data, and defer the overlap-side consequence
  **by name** when the tile is not prepared. A fixture SHALL prove that path can no longer derive.
- `CoveragePrerequisite::SpanFinalization(span)` — if the span's tiles are not prepared for the current
  generation, the completion is **deferred by name** and is not computed against partial coverage.

**A tile is not an execution bound.** The tile is the **spatial** unit of correctness and publication
atomicity: it is what is required, what is prepared atomically, and what composition rule applies to.
The **execution** unit is a separate, resumable **event/operation cursor inside a tile**, because a
dense tile can contain far more events than a nominal one and conservative closure can make a small
tile inspect much of the loop. Sizing the worker by "a tile is about 10 ms" would be an empirical sample
presented as a bound. The worker declares its own grain (events/operations per turn) and persists its
cursor across turns; a dense-tile plus long-closure case must still stop at the declared grain.

**Closure composition is by identity, not concatenation.** The same source note can fall in the backward
closure of several adjacent tiles, so per-tile results can be individually correct and duplicate when
combined. Composition SHALL be by stable `NoteId` (the existing effective-content identity rule), never
by concatenating per-tile candidate lists. If `maxCommittedNoteSpan >= loopLengthTicks`, closure
**saturates to the whole loop** rather than producing ambiguous modular ranges.

**Consequences.**

- `kOverdubSourceWindowBars` and the moving playhead window cease to be correctness mechanisms.
  `resolveOverdubSourceWindow` survives only as a *grain selector* for a prepared region.
- The whole-loop span cache becomes an **optional prewarm**. `fullLoop` stops being a readiness
  requirement; per-region readiness is. The assembled-parity gate (D3a) becomes **convergence parity**:
  whole-loop geometry must be reproduced once every phase has been touched and prepared.
- `overdubSourceSpanCacheCoversWindow` is replaced by per-region coverage; a contiguous
  `(coverageStart, coverageLength)` pair is no longer sufficient and was a step, not the destination.
- The wrap-transition hazard disappears: there is no coverage transition to catch up on, because
  coverage only grows where the player played.
- Cost becomes proportional to the touched region (measured: 4 bars ≈ 20 ms device, 2 bars ≈ 10 ms),
  paid once per region per generation, instead of ~83 ms per wrap.
- Scheduling side effect to verify: `canonicalPreparationHasPriorityWork(..., sourceSpansReady)` will
  now usually report ready during a session, so the canonical-prefix owner gets turns it was denied.
  That is a behaviour change needing its own oracle.

### D3a — Whole-loop parity is a convergence gate, not a construction gate

The version of D3a written for the windowed shape asked for "assembled-cache parity before
fullLoop=true". Under touched coverage the gate moves: parity is asserted **when preparation converges**
— every phase touched and prepared for one generation must reproduce the whole-loop derivation exactly
(boundary-crossing notes, loop-wrap notes, repeated/simultaneous same-pitch notes, `NoteId` identity,
start/end geometry, no duplicates, no omissions).

Per-region parity is not sufficient and per-window parity is not sufficient; the convergence case is the
one that proves the tiles compose. Whole-loop prewarm remains an optimisation and cannot be a
prerequisite, so the gate is on the *derived result*, not on a cache being complete.


### D4 — Deferral is a state, not a retry

A deferred child keeps its cursor/request exactly as it is today; nothing about the deferral resets it.
The rotation still advances (the existing rule that a blocked holder must not monopolise service), and
the child is owed again on a later turn. No new queue, no new priority field.

The deferral row is **Tier-A** and bounded: once per child per turn-interval, not per turn. A row that
only appears when the ring has room would be shed exactly where it is needed — the tier classifier
list in `include/Utils/CaptureLineTier.h` is the reason the existing `win_gap`/`reentry` rows exist.

### D5 — Item 1a (same slice): name the persisting step, report-only

`resolvePersistenceSliceBudgetUs` returns 300 µs while capture or transport is active
(`Config::maxPersistenceMicrosActive`); the step loop in `src/StorageManager/DeferredSaveScheduler.cpp`
checks it **between** steps, so one step's own cost is the bound. The 213 ms span emitted no
`PERS,work` row, so today the step is unnamed.

Add one report-only row emitted when a single step exceeds the active budget or the declared bar,
carrying: step name, step duration, the budget in force, and whether the step is indivisible. It is
attribution only — it changes no admission, no budget and no order. It must not be emitted from the
stop path (no full validate on stop), and it must not allocate; the stage names already exist as
string literals in that file.

*Alternative rejected.* Guessing the step from neighbouring `PERS,work` timestamps: the 41.5 ms
`payload_slice` in the adjacent turn is not the 213 ms step, and attributing by timing proximity is the
error this row exists to prevent.

### D6 — Capture-only work is separated, not exempted

`deferredStoredMidiVerification` and the `SEVT` dump are compiled only under `SESSION_CAPTURE`. Under
this change they are subject to D1 like any other child: bounded under the aggregate turn, or deferred
while timing-critical. Their 88.9/230.1 ms contributions leave the production acceptance numbers, and
the acceptance run stays readable on a capture build because the production lane's rows are no longer
interleaved with capture-only whole-loop resolves on the same turn.

*Alternative rejected.* Leaving them as-is and subtracting them by hand from each run: it makes every
acceptance claim depend on post-hoc arithmetic, and it leaves two unbounded children in the inventory
that the rule says must be declared.

### D7 — Bound classes and RAM1

The turn gains an enforced **owner work budget** (`kOwnerWorkBudgetUs`, D2, derived from the fastest
supported clock interval), and each child that moves gains its own declared maximum grain: an
**event/operation cursor** for the source-view worker, bars for the cache slice, steps for persistence.
`loop_rem` stays the 50 ms alarm and is the backstop, never the enforcement.

`ExecutionBounds.md` rows 5/8/9/10/12/19 are updated in the same commits that enforce the change
(rule 3). **Row 14 (`processDeferredSaveState`) is deliberately excluded from that move until section 6
splits the step**: for the first slices it stays classified coarse/empirical with the explicit note
that its nominal 300 µs budget is **not enforced within an indivisible step**. Section 3 only *names*
the offending step. Reclassifying row 14 before the split would be exactly the "declare a bound you do
not enforce" defect this change exists to remove.

Any added state follows the established practice on this tree: file-scope observation state is
`DMAMEM` (RAM2), per-slot state lives in the slot's external working set, and nothing new is charged to
RAM1 — the budget gate refuses new DTCM statics (DEC-174/183/189 all moved bytes to RAM2 for this
reason). The per-slot state is the tile set (required / prepared / pending) qualified by
`sourceGeneration`; its size is a declared tile count, and the envelope must cover the longest supported
loop. It is not a per-window mask: tiles are the coverage unit and carry no execution bound.

## Risks / Trade-offs

- **Wrap-crossing window parity** (D3) → resolve with a native parity fixture *before* the production
  change; widen to two windows if needed. This is the only item that can invalidate the slice.
- **Tile-composition drift** (D3a) → per-tile correctness does not compose automatically; composition is
  by note identity and the convergence fixture is what proves it, so a wrong composition is a red gate
  rather than a silent duplicate candidate.
- **Deferring the view refresh past its coverage** → explicitly forbidden by D3. The failure mode is not
  "stale geometry served"; the bounded refresh runs per window, and if it cannot, the child's grain gets
  finer. This is why slice 1 and the windowed replacement land atomically for this child.
- **Deferring other unbounded children changes what settles late** → display/content may settle one turn
  later. Accepted: this is the trade the owner asked for, and it is measurable as "deferral rows" rather
  than as "MIDI gap".
- **A child mis-declared as bounded re-breaches the bar silently** → the aggregate `loop_rem` row is
  the backstop, and the acceptance oracle counts it. A breach always fails acceptance, so a wrong
  declaration cannot pass unnoticed.
- **Tier-A growth** → every new row costs ring and DTCM-adjacent bytes in the classifier. Each new
  literal is registered and pinned in `test_capture_line_tier`, and the rows are interval-bounded.
- **Persistence diagnostic noise** → emitted only on breach of the active budget or the bar, one row
  per step per interval.
- **Rollback** → the owner admission is one predicate plus its call site; reverting it restores today's
  behaviour without touching the child code. The tile coverage model, the worker grain and the
  prerequisite wiring are gated by the parity fixtures and are revertible together, but **not separately
  from each other** (D3).

## Migration Plan

No data or format migration. Firmware-only:

1. **Contract + bounded replacement (one commit, one image).** Owner admission predicate with the
   reserve-before-dispatch rule, the enforced `kOwnerWorkBudgetUs`, the persist named-step row, `SEVT`
   and the other unbounded children denied while timing-critical, **and** the touched-coverage tiles that
   replace the whole-pass calls for the overdub source view. The view child cannot be deferred without
   that replacement (D3), so these are not separable slices. Content behaviour is unchanged; the timing
   behaviour change is the point.
2. **Convergence parity (may land with or after 1, never before).** Whole-loop geometry is reproduced
   once every phase has been touched and prepared for one generation; the whole-loop cache is an optional
   prewarm and no consumer may depend on it.
3. **Follow-ups by measurement**: persist splitting (which is the only step that may reclassify row 14),
   then the remaining capture-only children and inventory rows, each as its own slice with its own
   oracle.

Device acceptance is one run of the same overdub shape, read against the proposal's numbers **including
the maximum MIDI-service and clock-dispatch gap**. "Every turn under 50 ms" alone is not evidence of
realtime behaviour: a turn can be short and the clock can still be late, so the oracle carries both the
breach count and the service-gap maxima.


## Open Questions

- The exact indivisible persistence step. Deferrable: D5 exists to name it, and the slice boundary is
  "measure, then split", so the answer changes tasks 3+ ordering but not the approach or the specs.
- Whether `appendOverdubPassWrapPairedNotes` can be reused for a windowed result unchanged, or whether
  the pad makes it unnecessary. Deferrable: both are inside item 1 and the parity fixture decides.
- The declared tile count and the worker grain (events/operations per turn). Deferrable to 4.1b, which
  sizes them against the longest supported loop and a worst-case dense tile — **not** against the owner
  budget. Above the declared tile count the behaviour is a named fallback, not a different contract.
