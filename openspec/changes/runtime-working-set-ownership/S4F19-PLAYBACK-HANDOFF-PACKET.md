# S4f.19 — first-bar and onward playback handoff (execution packet)

Status: **G0 complete, 2026-09-28; no firmware behavior changed by this packet.**
Baseline `c125a32f` (closure-plan Step 1 + review follow-ups). Authority:
[shared finalization contract](SHARED-CAPTURE-FINALIZATION.md) § "First-bar and onward
playback contract" and § "Bounds and resources";
[S4F-CLOSURE-PLAN.md](S4F-CLOSURE-PLAN.md) §2; DEC-093 — shared capture finalization,
DEC-043 — execution bounds, DEC-064 — exclusive raw capture ranges.

## G0 result — PASS, with named prerequisites

VERIFIED from the current code (all citations are production paths):

- **The MIDI entry emits nothing for the interval.** `Track::playMidiEvents` /
  `playMidiEventsForSlot` return before `playCommittedLoopMidi` when
  `!loop.hasCommittedPasses() || loop.loopLengthTicks == 0`
  (`src/Track/TrackPlaybackHotPath.cpp:478,495`). `hasCommittedPasses()` requires a
  non-empty committed record/overdub pass (`src/Loop/LoopCapture.cpp:28-40`). A deferred
  Record interval has neither, so the retained first-bar window and the job-owned
  `CaptureRange` are **both unreachable** on the MIDI edge.
- **The adopt branch cannot cover it either.** `adoptFirstRecordingBarReturnWindow` is
  reached only under `loop.isPlaybackSessionExitPublicationGap()`
  (`TrackPlaybackHotPath.cpp:243-255`), which requires a queued session-exit receipt **and**
  a pending snapshot publish (`src/Loop/LoopSnapshotPublish.cpp:1605-1610`) — false while
  the pass is unpublished.
- **Nothing binds the window on the deferred route.** `bindFirstRecordingBar` has exactly
  one caller, the inline stop (`src/Track/TrackCaptureStopCommit.cpp:490`). The deferred
  Record worker never calls it.
- **The frozen-source reader exists but is unwired.** `RecordCoverage` applies the record
  stop projection and window filter (`src/Track/RuntimePlaybackViewFill.cpp:148-155`), and
  `RuntimePlaybackViewSource::fromCaptureRange` (`…:24-36`) reads the sealed range
  generation-qualified. Neither has any production caller; only
  `test/test_playback_prewarm.cpp` uses them.
- **No later-window builder exists for the frozen source.** `serviceBridgeWindow` and the
  pair/merged fallbacks all require committed bridges/snapshots
  (`src/Track/TrackPlaybackRuntime.cpp:1492-1497`,
  `src/Track/TrackPlaybackWindowBuild.cpp:188-296`). Past bar one the adopt path returns
  nullptr (`TrackPlaybackRuntime.cpp:1700-1703`) and nothing else serves the interval.
- **Playback holds no borrow.** `job.rawRange` is borrowed only by the job
  (`src/Track/TrackOverdubLifecycle.cpp:757-758`); `retireJobCaptureSourceGrain` frees a
  chunk per turn and bumps the range generation (`…:356-374`,
  `src/LoopEventStore.cpp:1893-1912`). A fill reading the range would be denied mid-flight,
  and there is no borrower count or refusal to stop that.
- **Record-mode jobs are test-only today.** `beginDeferredOverdubStopForSlot` requires
  `isOverdubbing()` (`TrackOverdubLifecycle.cpp:517`); production Record admission is S4f.17
  (unchecked). The deferred state is reachable only through
  `TrackCaptureStopTestAccess::bindRecordClosure`, so every fixture for this task is a
  controlled-bind production-worker fixture until S4f.17 lands.

G0 PASS: the primitives exist (raw bar-1 window, adopt contract, projected frozen-source
fill, view lifecycle). The missing work is **admission + binding + the onward window
cursor**, not a new representation.

## Hidden prerequisites (the reason this is sliced)

1. The `hasCommittedPasses()` entry gate must gain a job-owned-source branch without
   fabricating a pass.
2. `serviceFirstRecordingBar` reads the **live producer store**, which `sealCaptureRange`
   swaps away; a post-freeze service must use `fromCaptureRange` instead, and it is only
   stepped while `isRecording()`.
3. No window cursor/reanchor selects successive windows from the frozen take.
4. Readiness is unproven: `prepareFirstRecordingBar` succeeds with a zero-row window, and
   the window carries default cursor/ledger receipts. Held-note closure crossing the window
   end is explicitly deferred to a published root or the loop wrap — neither exists for an
   unpublished interval.
5. Retirement invalidates the source; no borrower refusal protects a playback fill.
6. `reset(true)` preserves exactly one view; a frozen-window role must reuse the existing
   three-view lifecycle, and any job field growth re-bases the `CaptureStopJobs` size pin.
7. A successor start is not additionally gated for a same-slot predecessor (S4f.17/S4f.21).

## Slice sequence (each independently verifiable; parents stay open)

1. **S19.1 — MIDI-entry admission for a job-owned interval.** Extend the two
   `hasCommittedPasses()` early returns with a bounded, job-qualified branch that permits
   playback emission for the interval's own slot when the job owns a ready source, and
   never publishes or implies a committed pass. Fixtures: emission denied for a different
   slot, for a superseded interval, and while `!permitsPlaybackEmission()`.
2. **S19.2 — bounded return-window bind in the worker.** A new bounded preparation step
   projects the retained bar-1 window's rows to the job's final length (sub-bar clamp,
   overflow NoteOn remap, overflow non-note drop) and installs it as the bound return
   window for the job's generation, so the launch serves bar one from the job's own
   geometry. **Not at the stop edge**: the stop stays structurally bounded. Fixtures:
   ≥1 bar, sub-bar, remapped NoteOn, dropped row, stale generation, incomplete window.
3. **S19.3 — onward windows from the owned frozen source.** A window cursor builds the next
   `PlaybackCoverage`/`RecordCoverage` window from `job.rawRange` through the existing
   `fromCaptureRange` overload, reusing the `build`/`ready` roles, with the read completing
   inside its declared deadline independently of job completion. Fixtures: traversal beyond
   bar one, wrap while finalizing, long/dense take, multiple sounding tracks, stale source
   replacement.
4. **S19.4 — ownership through successor, publication and retirement.** Either hold a
   playback borrow that makes retirement wait, or complete/abandon the fill before the
   source is released; freeze/rebind in-flight builders to the job-owned source at a
   successor start; retire every receipt at publication. Fixtures: premature-retirement
   mutation fails, successor start preserves the window, publication replaces it exactly
   once.
5. **S19.5 — acceptance evidence.** Exact emitted content and ownership at every boundary;
   mutations for the old-generation selection, premature retirement, and a full-take
   fallback on the MIDI edge; focused + full native + capture-serial (padding ≥ 840).

## Resource accounting to record before S19.1 production edits

Per the contract, before any production edit for this task:

- bytes added to `Track::CaptureStopJob` / `CaptureStopJobs` (re-base the 28936 pin with the
  reason in the `static_assert`) — target **zero**: reuse the existing job receipts;
- the view role used for the frozen window, from the existing `kViewCount = 3`
  (`include/RuntimePlaybackView.h:18-24`) — target **no new role**;
- `RuntimePlaybackViewSet::fixedExternalBytes()` before/after
  (`src/Track/RuntimePlaybackView.cpp:852-874`);
- RAM1 padding floor 840 (`scripts/ram1_budget.json:7`) and the linked ITCM delta;
- simultaneous owners per interval: raw range + retained window + in-flight frozen fill.

## Open decisions and their preferred path

- **Window cursor representation** (UNKNOWN, no existing owner). Preferred path: reuse the
  `RuntimePlaybackViewSet` `build`/`ready` handoff exactly as `serviceBridgeWindow` does,
  with the cursor derived from the playhead phase and the frozen range's projected rows —
  no new stored cursor field until a fixture proves one is required. Confidence: medium.
- **Retirement must be gated on playback continuity, not on the fill finishing** (UNKNOWN).
  Completing or abandoning the *current* fill is insufficient: the **next** playback window
  may still need the frozen source. `retireJobCaptureSourceGrain` must wait until all three
  conditions hold —
  1. a matching committed replacement is ready and handed off;
  2. every builder has detached from the raw range;
  3. playback no longer needs that range for subsequent windows (the onward window has moved
     past what the range still covers, or committed content covers the remaining span).
  Enforce that condition **at retirement itself**, not by draining a fill that may be
  restarted. A borrower counter is optional if the existing ownership protocol can prove the
  three conditions (the range is `sealed`, the job owns it, the committed handoff carries the
  matching identity, and no builder borrows it); generation checks alone are **not**
  sufficient, because they deny a late read rather than preserving continuity through the
  interval. Confidence: medium; the exact predicate is S19.4's contract.
- **Record admission predicate** (UNKNOWN). The mode-neutral
  `hasPendingDeferredOverdubStop()` ring-count predicate already covers both modes; keep it
  and let S4f.17 name the common owner. Confidence: high.

## Non-goals

No fabricated committed pass, no global Stop, no full-take build on the MIDI edge, no
snapshot wait as a recovery strategy, no new undo grouping, no geometry policy change. S4f.17
(production Record admission), S4f.20's remaining residuals and S4f.21 stay open; this packet
does not close S4f.19.

## Landed: S19.2 core — job-qualified deferred return-window bind

`LoopPlaybackRuntime::bindDeferredRecordReturnWindow(range, expectedGeneration,
loopLengthTicks)` binds the retained bar-1 window to a deferred job's frozen source and final
geometry. It reuses `RuntimePlaybackView::bindRawCoverage` + `handoffToRetained` and adds the
job qualification the inline route does not need:

- refuses a range that is not `ready()` or whose generation is not `expectedGeneration`;
- **transforms** the raw bar before binding, not merely clamps its coverage:
  `RuntimePlaybackView::projectRawCoverageToLoopLength` applies the record stop tick policy
  (an overflowing NoteOn is remapped into the loop, every other overflowing row is dropped),
  closes a note left open at the loop end with one synthetic Off at `loopLengthTicks - 1`
  carrying the NoteOn's identity, clamps the coverage to the prepared-bar/final-loop
  intersection, and re-orders the retained rows canonically (phase, NoteOff < NoteOn < other);
- leaves the retained window untouched on refusal and is a no-op after a successful handoff.

`Track::stepDeferredRecordClosure` calls it once per closure grain after the frozen-source
borrow is validated; a refusal never fails the job, so playback simply has no unpublished
source, exactly as before.

`LoopPlaybackRuntime::hasBoundRecordReturnWindowForPendingJob()` is the eligibility predicate
the MIDI entry will use: a **pending** job owning a bindable window. It is false for the inline
route (which keeps the session-exit publication-gap predicate) and false once the job retires,
so committed content owns the slot from then on.

Fixtures (`test/test_playback_prewarm`, 186/186) verify **transformed event content**: a
sub-bar take (raw bar with a NoteOn at 600 remapped to 100, a CC at 700 dropped, two notes
left open closed at 499) binds and serves the exact canonical sequence; a stale generation
leaves the window unbound and unadoptable; an unsealed range refuses; and eligibility requires
both a pending owner and a bound window. Mutations red-prove the fixtures: removing the tick
projection leaves the overflow row and fails the content assertion; removing the closure path
fails the bind. Resource cost: the projection is one FLASH-resident cold function on the
existing `RuntimePlaybackView` role — **no new field, view role, or job byte** — with one
256-byte stack lane bitmap and work bounded by the view role capacity (one bar).

**Not yet wired (S19.1):** the two `hasCommittedPasses()` early returns in
`Track::playMidiEvents`/`playMidiEventsForSlot` and the adopt-branch
`isPlaybackSessionExitPublicationGap()` condition still do not admit a deferred interval. That
edit is deliberately held until a native fixture can observe emission, because the harness has
no MIDI send capture today (`off_path_deps.h` stubs only `silenceTrackMidiOutput`). The
predicate, the projection and the bind are in place; the wiring is one guarded condition plus
that fixture.

**Still open:** onward windows from the frozen source (with the continuity-gated retirement
above) and the ownership-through-successor slice. S4f.19 stays unchecked and is partial
implementation; production Record admission (S4f.17) must not be enabled before the
onward-window and retirement evidence exist.

## Correction and next step (2026-09-28, round 2)

**Fixed — refusal is now atomic.** `projectRawCoverageToLoopLength` mutated rows and could
append synthetic Offs before returning `false` when closure capacity ran out, so a worker retry
saw a partially transformed window. It now preflights the closure count with a non-mutating pass
that applies the same drop rule and lane logic, and returns `false` with the window
byte-identical when the appends cannot fit. Fixture
`test_deferred_record_projection_refusal_leaves_the_window_untouched` fills the view role to
capacity with held notes and compares every row (tick, type, channel, note, retained flag,
phase) plus the coverage before and after a refusal, and again after a repeat refusal. Mutation:
removing the preflight leaves a mutated row and fails the comparison.

**Added — native MIDI send-capture boundary.** `MidiSendCapture` (`include/MidiHandler.h`,
`src/MidiHandler.cpp`, `PIO_UNIT_TEST_NATIVE` only) records every event that reaches
`MidiHandler::sendMidiEvent`, so playback emission can be asserted end to end without a device.

**Added — admission predicate and its fixture.** `Track::hasAudiblePlaybackSource(slot)` is the
admission predicate for both entry points: committed passes, or a pending job's bound bar-1
window. `test_deferred_record_interval_is_audible_only_with_a_bound_window` drives the real
record start (which prepares the window) and the deferred closure grain, and asserts the slot is
**not** audible while the window is raw, becomes audible once the worker binds it, is not audible
for another slot, and stops being audible when the job retires.

**Hot-path prerequisite found and fixed:** `playCommittedLoopMidi`'s stale-runtime branch called
`reset(true)`, which refuses while a pending job owns the window, and then returned — so the
interval could never be served. The reset is now skipped when a pending job owns a bound return
window (the window is exactly the state `reset(true)` preserves), and the revision is synced
without a destructive reset.

**Immediate next step — emission assertions (partially landed).** The capture boundary is in
place and the first checks are green; two remain.

Landed (round 3, `test_shared_capture_finalization`):
- `MidiSendCapture` records a direct `Track::sendMidiEvent` (sanity: the boundary itself works).
- An unpublished interval with a **pending job but a raw (unbound) window emits nothing**.
- The **active entry point emits exactly one NoteOn and one NoteOff** from the bound window
  (`ons == 1`, `offs == 1`, with the emitted trace in the failure message).
- The **layered entry point emits nothing** for this interval: `isActive` is target-based
  (`PlaybackMidiTarget::ActiveSlot`), so `fixedViewEligible` is false there and an unpublished
  interval has no fixed view. That is the layered entry's contract — it serves another track's
  committed bridge — so it must not fabricate content from a window it does not own.
- **Retiring the job ends emission**, and another slot emits nothing.

Hot-path prerequisite fixed: `playCommittedLoopMidi`'s stale-runtime branch called `reset(true)`,
which refuses while a pending job owns the bound window, and then returned — so the interval
could never be served. The reset is now skipped when a pending job owns a bound return window (the
window is exactly the state `reset(true)` preserves) and the revision is synced without a
destructive reset. That change is what made the active-entry assertion pass.

Still owed:
1. **A note crossing the first-bar boundary must close at its actual endpoint.** This is a real
   gap, not just a missing test: the retained window is truncated at one bar, so a NoteOn in bar 1
   whose Off lies in bar 2 is closed by the projection at `loopLengthTicks - 1` rather than at its
   actual Off tick. The actual endpoint lives in the owned frozen source, so this check depends on
   the onward-window slice; the closure policy is only correct for a note left open at the stop.
   Until then the projection's closure is a **known approximation for crossing notes**.
Landed in round 5 — **onward window built from the owned frozen source:**
`LoopPlaybackRuntime::serviceFrozenRecordWindow(range, expectedGeneration, sourceIdentity,
loopLengthTicks, phase, windowTicks, entryQuantum)` builds the window that contains the playhead
from the frozen `CaptureRange` through the existing fill/order owners and the `RecordCoverage`
projection. One bounded grain per call, reusing the `build` role and publishing into `ready` for
the normal handoff. It refuses a stale/unready range or a phase outside the loop, leaving both
roles as they were.

Fixtures (`test/test_playback_prewarm`): the bar containing phase 800 is built as the window
`startPhase 768, length 768` over a 1536-tick loop with **exact projected content** — only the two
bar-2 rows, canonically ordered (the bar-1 rows are outside the window); a stale generation, an
out-of-loop phase and an unsealed range all refuse without creating a ready window. Mutation:
switching the fill from `RecordCoverage` to `AllSource` retains all four rows and fails.

**Remaining for the onward slice:** the crossing-note endpoint uses the same frozen source; and
the continuity-gated retirement plus ownership through successor start still follow.

Landed in round 6 — **onward window served through the production hot path:**
`LoopPlaybackRuntime::adoptFrozenRecordWindow` returns the onward window covering the playhead,
promoting a completed `ready` build through the audible handoff and reusing an already-serving
onward window across turns (excluding the bar-1 return window) so a steady playhead does not
rebuild. `Track::playCommittedLoopMidi` now, when the bar-1 window no longer covers the phase and
a pending Record job owns the frozen range, adopts the onward window or services the next build
grain from `job->rawRange` with `RecordCoverage`.

Fixture `test_deferred_record_interval_serves_beyond_bar_one_from_the_frozen_source`: a two-bar
take with a note in each bar; at phase 90 the bound bar-1 window serves the bar-1 note, and past
bar one the onward window built from the job's frozen source serves the bar-2 note **exactly once**
(one NoteOn and one NoteOff, note 62), with the pass still unpublished. That satisfies the
traversal-beyond-bar-one exit case.

Landed in round 4 — **publication/replacement parity:**
`test_published_record_interval_does_not_replay_the_return_window` drives the whole deferred chain
(closure → staging → normalization → transfer → publication), asserts the pass is committed, then
emits through the active entry point and asserts **exactly one NoteOn and one NoteOff**. The bound
window survives publication and is still the served source because no matching replacement *view*
has been handed off yet — the contract keeps it until one is — and the assertion proves the
already-served window events are **not replayed** and the committed boundary is **not omitted**.
The adopt path's phase rule is also pinned: adopting with a launch phase past the window's events
skips them (the "already heard live" rule), which is why the drive stamps the source before
resetting the cursor to the loop start.

