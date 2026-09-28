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
- **Borrow vs. early completion at retirement** (UNKNOWN). Preferred path: complete or
  abandon the in-flight fill before the first `retireJobCaptureSourceGrain` grain
  (retirement is already a multi-grain phase), and additionally assert the range generation
  in the fill so a late read denies. A borrower count is the fallback if a fixture shows a
  deadline that cannot complete first. Confidence: medium.
- **Record admission predicate** (UNKNOWN). The mode-neutral
  `hasPendingDeferredOverdubStop()` ring-count predicate already covers both modes; keep it
  and let S4f.17 name the common owner. Confidence: high.

## Non-goals

No fabricated committed pass, no global Stop, no full-take build on the MIDI edge, no
snapshot wait as a recovery strategy, no new undo grouping, no geometry policy change. S4f.17
(production Record admission), S4f.20's remaining residuals and S4f.21 stay open; this packet
does not close S4f.19.
