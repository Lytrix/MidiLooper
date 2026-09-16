## Context

See `proposal.md` — Why. The ownership layer this builds on is documented in
`openspec/changes/resumable-midi-boundary-playback-maintenance/CONTINUOUS-CAPTURE-OWNERSHIP.md`;
its per-slice checkpoints and recorded decisions (interval-qualified prepared
companion history; recorded causal predecessor + approved superseded rule) are the
constraints below.

Current state that shapes the approach:

- **Three guard layers refuse a successor**, each with its own reproducer in
  `test/test_continuous_capture/test_continuous_capture.cpp`:
  1. `Track::startOverdubbing` (`src/Track/TrackOverdubLifecycle.cpp`) refuses while
     `runtime->captureStopOwner != nullptr`, or without a session-exit handoff slot,
     an active overdub-transfer admission, or admitted canonical output —
     `test_track_reentry_does_not_depend_on_manager_guard`.
  2. `TrackManager` treats a pending deferred stop as "still capturing" for
     cross-track maintenance and slot switching
     (`TrackManager::anyTrackRecordingOrOverdubbing`,
     `TrackManager::setActiveLoopIndex`) —
     `test_manager_reentry_with_a_held_same_pitch_note`.
  3. `MidiButtonActions::handleToggleRecord` returns early with
     `"MIDI Button A: Overdub finalization continues in playback"` while
     `track.hasPendingDeferredOverdubStop()` —
     `test_button_a_three_intervals_accept_notes_with_service_paused`.
- **The real capacity is two staging roles, not four.** `RuntimePlaybackViewSet`
  holds `captureStagings[2]` with `liveStagingIndex`/`frozenStagingIndex`; punch-out
  freezes the live role and exchanges the indexes. The stop ring holds four
  descriptors, and `runtime->captureStopOwner` admits one job at a time. A frozen
  role is held until its job retires, because it owns the admission rows
  (reserved closures) the deferred worker consumes.
- **The worker is already FIFO-head bound and job-qualified**: it binds
  `captureStopJobHead()` once per grain, every receipt is job/interval qualified,
  and a job resolves from its own sealed raw range, pending changes, preview, undo
  baseline and recorded `causalBase`.
- **The superseded rule is landed**: when an interval's recorded causal predecessor
  is no longer the published root, it commits its accepted capture pass without
  publishing a successor window (`TrackPreparedStopCommit::commitSupersededCausalBase`)
  and restores its unpublished horizon.
- `RuntimeCanonicalPassInputs` binds its owner identity **by address** to the live
  Loop scalars and the canonical ordering phases need one coherent live epoch, so
  resolution stays live; the recorded identity is the supersession reference.
- The cleanup path derives state authority from interval identity
  (`job.interval == captureInterval_` in `TrackOverdubLifecycle.cpp`).
- **Reproducer finding (resolved during review).**
  `test_button_a_three_intervals_accept_notes_with_service_paused` presses Button A
  three times with **no worker turns at all**: start A at 96 / stop at 144, start B
  at 192 / stop at 240, start C at 288 / stop at 336, asserting after each stop that
  a deferred stop is pending and after the loop that C "must capture independently
  while A and B remain queued". That demands **two simultaneous frozen jobs plus a
  live third interval**, i.e. ≥3 admission roles and N-job queuing. It therefore
  cannot go green under the capacity this change is allowed to declare, and it is
  the one acceptance item that must be re-scoped or moved to a follow-up capacity
  change (see Open Questions).

## Goals / Non-Goals

**Goals:**
- Remove the three refusals innermost-first, each step backed by an inner invariant
  and flipping one public reproducer green.
- Make the declared capacity explicit, observable, and the only reason a supported
  reentry is denied.
- Preserve every ownership guarantee of the parent change while both intervals are
  live, including held same-pitch notes and transport Stop.

**Non-Goals:**
- N-job queuing, a second simultaneous frozen job, or more than two staging roles
  per track (this is what the third-interval reproducer would need).
- Cross-track reentry, command queueing, `Loop`/`Track` cloning, a second worker
  graph, or unbounded storage work at the action edge.
- Changing the recorded-causal-base or superseded semantics landed in the parent
  change.

## Decisions

**D1 — Un-guard order is Track → TrackManager → Button A (innermost first).**
Each outer guard exists because the inner layer used to be unsafe. Removing the
innermost first means the manager and Button A changes are backed by a Track that
already owns a distinct live interval, and each step flips one reproducer without
unguarding two layers at once. Alternative considered: remove all three together —
rejected because a failure would not identify which invariant is missing.

**D2 — Capacity: one admitted deferred predecessor plus one live successor per
track.**
Starting the successor while the predecessor finalizes is permitted. **Stopping that
successor requires a free frozen/admission role**: if the predecessor has not
retired, a successor stop is explicitly denied without consuming, freezing,
clearing or corrupting the live successor (it keeps recording, keeps its rows, and
keeps its reservations). This is what the two staging roles and the single
`captureStopOwner` support, and it implies the supported envelope:

```text
A recording
A stop            -> A deferred (frozen role = A)
B start           -> admitted (B writes the live role)
B capture         -> accepted
B stop            -> DENIED while A is pending (no frozen role available)
A retires         -> frozen role freed
B stop            -> admitted
```

A further interval cannot start while B still captures, because one track has one
live capture interval. Capacity failure is explicit (`queue_full` from the ring tail
or `storage_denied` from staging/admission, surfaced as the successor-stop denial)
and non-destructive. Alternative considered: reserve a third and fourth staging role
to admit C and D before A drains — rejected for this change, because it is new
ownership work (per-role frozen generations, an N-job drain order, a PSRAM
admission measurement) and the parent plan already sanctions capacity-limited
denial. **The three-interval reproducer depends on that deferred alternative** (see
Open Questions).

**D3 — Admission predicates ask the semantic question; deferred work is not itself a
live-ingress exclusion.**
A pending deferred stop is **not by itself** a reason to reject ingress into an
independently owned live successor. It remains valid input to capacity, retirement,
publication and maintenance decisions about deferred jobs — most importantly whether
the *live successor can itself stop*. Call sites are classified by the question they
actually ask, not by demoting the predicate globally:
- "may ingress enter the live interval?" → never excludes on pending-deferred alone;
- "can this interval freeze and become a deferred job?" → capacity question, answered
  by the frozen-role/`captureStopOwner` availability and reported explicitly;
- "is cross-track maintenance excluded?" → about live capture activity and deferred
  jobs separately (see D4);
- "may playback/persistence proceed?" → unchanged, still excludes on pending deferred
  work.
The blanket wording "never a live-ingress gate" from the first draft is replaced by
this rule.

**D4 — Live-role isolation; do not globally redefine "recording/overdubbing".**
Successor ingress writes only the live staging role, and the successor owns its
routing, note identity, reservations and preview for its whole life; predecessor
cleanup resets only its own retired role. Because cross-track reentry is a non-goal,
`TrackManager::anyTrackRecordingOrOverdubbing()` SHALL NOT be globally weakened.
Either split it into a live-capture-activity predicate and a deferred-maintenance
predicate (`hasLiveCaptureActivity()` / `hasDeferredCaptureMaintenance()`) with
each caller choosing the property it needs, or change only the specific decision
sites whose semantics require live-capture status. Alternative considered:
redefining the global predicate so a pending stop stops counting as capturing —
rejected because it would silently change unrelated cross-track behavior.

**D5 — Job isolation: a predecessor resolves exclusively from job-qualified
receipts.**
The deferred worker reads only the job's own sealed raw range, pending changes,
preview, undo baseline, recorded `causalBase` and interval identity; it never reads
live successor state, and its publication resolves against its recorded predecessor
under the approved superseded rule.

**D6 — State-mutation authority: a retiring job mutates live capture state only if
its interval still identifies the live interval.**
Cleanup may release its own resources, commit its own result and clear its own
receipts unconditionally. It may mutate live `Track`/`Loop` capture state
(pending notes, preview, capture extent, reservations, track state) only while
`job.interval == captureInterval_` and `job.slot == activeLoopIndex`; otherwise
cleanup is job-local:

```text
A completes, no successor:   ordinary terminal state transition (as today)
A completes while B is live: release A resources, commit A result, clear A receipts;
                             do NOT change B's state, pending notes, preview,
                             capture extent or reservations
```

This is the invariant that makes removing the outer guards defensible, and it must
hold for every terminal path (PLAYING commit, STOPPED recovery, superseded route,
fault fallback).

**D7 — Telemetry is transition/aggregate only.**
Admission, denial (with its reason) and retirement stage lines carrying interval
identity and queue depth, reusing `SC_ODUB_STOP_STAGE`; no per-event logging on the
capture path.

## Risks / Trade-offs

- **The third-interval reproducer cannot pass under D2.** Its C step needs two frozen
  jobs plus a live interval. Until the capacity change exists, that reproducer must
  be re-scoped to the supported envelope (or split), and its three-deep expectation
  moved to the follow-up. This is the review's blocking item.
- **Reentry during the canonical copy / undo collection.** The predecessor may be
  mid-grain when successor input arrives; admission must not perturb the borrowed
  frozen role or the job's raw range. Mitigation: the adversarial mid-grain timing
  coverage required by task 5.5.
- **Transport Stop with both intervals live.** Stop must retarget the pending job and
  preserve the successor's accepted input without publishing a stale window.
  Mitigation: explicit ordering coverage (task 5.3) and device acceptance.
- **Held same-pitch identity.** B's NoteOn of A's held pitch must not be paired with
  A's closure. Mitigation: the manager reproducer plus a note-identity assertion.
- **Successor-stop denial can look like unresponsiveness.** The denial must be
  distinguishable from latency in telemetry, and the successor must demonstrably keep
  recording.
- **No new allocations** is expected (roles, ring and receipts are prewarmed), so
  RAM1/PSRAM admissions should be unchanged; this must still be confirmed on the
  device, since static build success does not prove external-pool or heap headroom.

## Open Questions

1. **The three-interval reproducer (blocking implementation).** Confirm one of:
   - **(a) Re-scope it to the supported envelope (recommended):** assert A deferred,
     B start admitted, B capture exact, **B stop explicitly denied while A pends (B
     keeps recording)**, and the denied C press named as the same live interval's
     stop rather than a silent start; the three-deep "A and B remain queued" case
     becomes an acceptance case of the follow-up capacity change. This keeps the
     parent change's public reentry evidence honest at two deferred intervals'
     worth of nothing, i.e. one deferred + one live.
   - **(b) Expand this change to N-job queuing:** add admission roles for a second
     frozen job, multi-job FIFO drain order, and their PSRAM admission measurement.
     This contradicts the agreed non-goal and would make this a much larger change.
