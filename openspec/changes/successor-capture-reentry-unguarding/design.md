## Context

See `proposal.md` — Why. The ownership layer this builds on is documented in
`openspec/changes/resumable-midi-boundary-playback-maintenance/CONTINUOUS-CAPTURE-OWNERSHIP.md`;
its per-slice checkpoints and the recorded decisions (interval-qualified prepared
companion history; recorded causal predecessor + approved superseded rule) are the
constraints below.

Current state that shapes the approach:

- **Three guard layers refuse a successor**, and each has its own reproducer in
  `test/test_continuous_capture/test_continuous_capture.cpp`:
  1. `Track::startOverdubbing` (`src/Track/TrackOverdubLifecycle.cpp`) refuses while
     `runtime->captureStopOwner != nullptr`, or without a session-exit handoff slot,
     an active overdub-transfer admission, or admitted canonical output —
     `test_track_reentry_does_not_depend_on_manager_guard`.
  2. `TrackManager` treats a pending deferred stop as "still capturing" for
     cross-track maintenance and slot switching (`TrackManager::anyTrackRecordingOrOverdubbing`,
     `TrackManager::setActiveLoopIndex`) — `test_manager_reentry_with_a_held_same_pitch_note`.
  3. `MidiButtonActions::handleToggleRecord` returns early with
     `"MIDI Button A: Overdub finalization continues in playback"` while
     `track.hasPendingDeferredOverdubStop()` — `test_button_a_three_intervals_accept_notes_with_service_paused`.
- **Capacity is two staging roles, not four.** `RuntimePlaybackViewSet` holds
  `captureStagings[2]` with `liveStagingIndex`/`frozenStagingIndex`; punch-out freezes
  the live role and exchanges the indexes, so exactly **one live interval plus one
  admitted deferred job** fits per track. The stop ring holds four descriptors, and
  `runtime->captureStopOwner` admits one job at a time.
- **The worker is already FIFO-head bound and job-qualified**: it binds
  `captureStopJobHead()` once per grain, every receipt is job/interval qualified, and
  a job's resolution uses its own sealed raw range, pending changes, preview, undo
  baseline and recorded `causalBase`.
- **The superseded rule is landed**: when an interval's recorded causal predecessor
  is no longer the published root, it commits its accepted capture pass without
  publishing a successor window (`TrackPreparedStopCommit::commitSupersededCausalBase`)
  and restores its unpublished horizon.
- `RuntimeCanonicalPassInputs` binds its owner identity **by address** to the live
  Loop scalars, and the canonical ordering phases need one coherent live epoch, so
  resolution stays live; the recorded identity is the supersession reference.
- `hasPendingDeferredOverdubStop()` is currently used as a live-ingress gate in
  several places (`TrackCaptureInput.cpp`, `TrackPlaybackWindowBuild.cpp`,
  `TrackStateFacade.cpp`), which the plan requires to become a background query.

## Goals / Non-Goals

**Goals:**
- Remove the three refusals innermost-first, each step backed by an inner invariant
  and flipping exactly one public reproducer green.
- Make the declared capacity explicit, observable, and the only reason a reentry is
  denied.
- Preserve every ownership guarantee of the parent change while both intervals are
  live, including held same-pitch notes and transport Stop.

**Non-Goals:**
- A second simultaneous frozen job, a multi-job FIFO drain, or more than two staging
  roles per track.
- Cross-track reentry, command queueing, `Loop`/`Track` cloning, a second worker
  graph, or unbounded storage work at the action edge.
- Changing the recorded-causal-base or superseded semantics landed in the parent
  change.

## Decisions

**D1 — Un-guard order is Track → TrackManager → Button A (innermost first).**
Each outer guard exists because the inner layer used to be unsafe. Removing the
innermost first means the manager and Button A changes are backed by a Track that
already owns a distinct live interval, and each step can flip exactly one reproducer
without unguarding two layers at once. Alternative considered: remove all three
together — rejected because a failure would not identify which invariant is missing.

**D2 — Declared capacity: one live interval plus one admitted deferred stop per
track; further stops denied explicitly.**
Backed by the two staging roles and the single `captureStopOwner`, and consistent
with the implementation plan's acceptance case 2 ("when the declared capacity
permits ... rejected explicitly"). The denial reuses the existing explicit outcomes
(`queue_full` from the ring tail, `storage_denied` from staging/admission) and adds a
stage line naming the reason. Alternative considered: reserve a third and fourth
staging role to admit C and D before A drains — deferred, because that is new
ownership work (per-role frozen generations, an N-job drain order, and a PSRAM
admission measurement) and it is not needed for the reentry reproducers. This
capacity statement is the one assumption to confirm at artifact review.

**D3 — Replace blanket refusal predicates, do not relax ownership checks.**
`Track::startOverdubbing` keeps requiring an active overdub-transfer admission, an
admitted canonical output and a session-exit handoff slot, because those are
storage/ownership preconditions. What changes is that they may be satisfied while a
*predecessor* job is pending, and that the pending job no longer blocks the new
interval's own admission. The manager and Button A layers stop treating
`hasPendingDeferredOverdubStop()` as "this track is still capturing" for actions that
belong to the *live* interval; the predicate stays a background query where its real
meaning is "a deferred stop exists".

**D4 — Successor ingress writes only the live staging role for its whole life.**
Already true after the dual-role split; this change must not reintroduce sharing. The
successor's routing, note identity, reservations and preview bind to the live role
and the live interval, and the predecessor's cleanup resets only its own retired
role. Verification: the existing
`test_stop_job_and_live_ingress_own_distinct_staging` plus new reentry coverage.

**D5 — A predecessor's completion is scoped to its own interval.**
Completing A must not set the track's state, clear pending notes, or touch the live
role on B's behalf. Where the current code derives state from "the selected
interval" (`job.interval == captureInterval_` in the cleanup path), the reentry flow
must prove the successor case explicitly rather than relying on the outer guards.

**D6 — Telemetry is transition/aggregate only.**
Admission/denial stage lines with interval identity and queue depth, reusing
`SC_ODUB_STOP_STAGE`; no per-event logging on the capture path.

## Risks / Trade-offs

- **Reentry during the canonical copy / undo collection.** The predecessor may be
  mid-grain when the successor's input arrives; the successor's admission must not
  perturb the borrowed frozen role or the job's raw range. Mitigation: per-step
  reproducers plus the existing staging-isolation and raw-range-parity tests.
- **Transport Stop with both intervals live.** Stop must retarget the pending job
  and preserve the successor's accepted input without publishing a stale window.
  Mitigation: explicit native coverage plus the parent change's STOPPED-recovery
  path; device acceptance is required because this is observable.
- **Held same-pitch identity.** B's NoteOn of the same pitch as A's held note must
  not be paired with A's closure. Mitigation: the manager reproducer plus a
  note-identity assertion; the final physical NoteOff must not terminate the wrong
  note.
- **Capacity denials feel like unresponsiveness.** A press beyond capacity is a
  deliberate, visible denial; telemetry must make it distinguishable from latency so
  a device run can tell them apart.
- **No new allocations** is expected (the roles, ring and receipts are prewarmed), so
  RAM1/PSRAM admissions should be unchanged; this must still be confirmed on the
  device, since static build success does not prove external-pool or heap headroom.
