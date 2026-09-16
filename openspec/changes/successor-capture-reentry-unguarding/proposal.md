## Why

OpenSpec change `resumable-midi-boundary-playback-maintenance` has landed its whole
ownership layer for continuous queued capture (task 2.3: FIFO/queue-head binding,
sealed raw-range ownership, job-owned pending changes/preview/undo baseline,
session-exit receipt ring, interval-qualified prepared companion history, and the
recorded causal predecessor with its superseded rule). Its public reentry
reproducers stay red **on purpose**: three admission guards still refuse a successor
interval while a predecessor finalizes, and removing them was explicitly excluded
from that change so it could be reviewed — and device-verified — on its own.

This change removes those guards, innermost first, so a player can punch out A and
immediately record B without waiting for A's finalization, while every ownership
guarantee the previous change established keeps holding. Reviewing the reproducers
also established that one of them asserts more than the hardware ownership model can
support in this change (two simultaneously frozen jobs); that case is re-scoped here
and its deeper capacity is a follow-up, not a silent assumption.

## What Changes

- Replace the blanket "a pending deferred stop refuses a new interval" refusals with
  precise, ownership-based predicates, layer by layer: **Track** (`Track::startOverdubbing`),
  then **TrackManager** (cross-track maintenance exclusion and slot-change finalize
  path), then **Button A** (`MidiButtonActions::handleToggleRecord`).
- Declare the successor capacity explicitly: **one admitted deferred predecessor plus
  one live successor interval per track** (the two prewarmed
  `CaptureAdmissionStaging` roles plus the single `captureStopOwner`). Starting the
  successor while the predecessor finalizes is admitted; **stopping the successor
  while the predecessor is still pending is explicitly denied** (no frozen/admission
  role is free), keeps the successor recording with its rows and reservations
  intact, and is reported with its own outcome (`queue_full` / `storage_denied`)
  rather than queued silently or dropped after acceptance.
- Keep a successor's ingress bound to the **live** staging role and the live
  interval's identity/routing/preview for its whole life; a predecessor's finalization
  resolves only against its own receipts (sealed raw range, pending changes, preview,
  undo baseline, recorded causal predecessor).
- Guarantee that old-job completion cannot clear or retarget the live successor:
  pending notes, preview, capture state, storage and track state stay the successor's.
- Treat a pending deferred stop as **not by itself** a reason to reject ingress into
  an independently owned live successor, while it stays authoritative input to
  capacity, retirement, publication and maintenance decisions about deferred jobs
  (including whether the live successor can itself stop). Call sites are classified
  by the question they ask; the predicate is not demoted globally, and
  `TrackManager::anyTrackRecordingOrOverdubbing()` is not globally weakened.
- Guarantee that a retiring job mutates live capture state **only** while its interval
  still identifies the live interval; otherwise its cleanup is job-local (no state
  transition, pending-note, preview, extent or reservation change on the successor's
  behalf).
- Add compact transition/aggregate telemetry for admission, denial, reuse and commit
  decisions (no per-event capture logging).
- Non-goals (this change deliberately does **not**): admit a second simultaneous
  frozen stop job or drain a multi-job FIFO (both need additional admission roles;
  this is what the deferred third-interval case requires), globally redefine what
  "recording/overdubbing" means for cross-track maintenance, add a second worker
  graph, add cross-track reentry, clone `Loop`/`Track`, queue commands, or
  sort/gather/detach unbounded storage at the action edge.
- **No breaking changes.** The removed refusals are internal admission guards; every
  public control surface keeps its existing meaning, and capacity-exhausted presses
  remain explicit denials.

## Open decisions (must be resolved before implementation)

- **The three-interval reproducer vs. the declared capacity.** Recommended: re-scope
  it to the supported envelope (A deferred, B start admitted, B capture exact, B stop
  explicitly denied and still recording, the denied C press identified as the same
  live interval's stop) and move the three-deep "A and B remain queued" expectation
  to a follow-up capacity change. Expanding this change to N-job queuing instead
  contradicts the agreed non-goals. See `design.md` Open Question 1.

## Capabilities

### New Capabilities
- `continuous-capture-successor-admission`: a successor capture interval is admitted,
  routed and preserved while a predecessor's deferred finalization is still pending,
  with an explicitly declared capacity and explicit denial beyond it.

### Modified Capabilities
- None. The behavior being extended belongs to the in-flight
  `resumable-midi-boundary-playback-maintenance` deltas
  (`midi-boundary-runtime-maintenance`, `deferred-job-scheduler`,
  `playback-runtime-prewarm`), which are not yet archived into `openspec/specs/`;
  this change adds its own capability instead of editing an unarchived delta.

## Impact

- **Code:** `src/Track/TrackOverdubLifecycle.cpp` (admission and the deferred worker),
  `src/Track/TrackCaptureInput.cpp` / `TrackPlaybackHotPath.cpp` (live-ingress
  predicates), `src/TrackManager.cpp` +
  `src/TrackManager/TrackManagerSlotMatrix.cpp`, `src/MidiButtonActions.cpp`.
- **Tests:** `test/test_continuous_capture/test_continuous_capture.cpp` —
  `test_track_reentry_does_not_depend_on_manager_guard` and
  `test_manager_reentry_with_a_held_same_pitch_note` must pass, plus new capacity,
  isolation, transport-ordering and mid-grain timing coverage. The third reproducer,
  `test_button_a_three_intervals_accept_notes_with_service_paused`, presses Button A
  three times with **no worker turns at all** and asserts that C captures "while A and
  B remain queued" — i.e. two simultaneous frozen jobs plus a live third interval,
  more than the declared capacity supports. It must be re-scoped to the supported
  envelope (or moved to a follow-up capacity change) before implementation; see
  `design.md` Open Question 1.
- **Hardware:** observable over serial (record/overdub-stop/reentry), so the change
  needs a device acceptance run before the parent change's task 2.3 checkbox closes.
- **Docs:** `docs/Runtime/CURRENT_WORK.md`, `docs/Runtime/PROJECT_STATE.md`,
  `docs/DELIVERABLE_TRACKING.md`, and the parent change's `tasks.md` /
  `CONTINUOUS-CAPTURE-OWNERSHIP.md`.
- **Dependencies:** requires `resumable-midi-boundary-playback-maintenance` (its
  ownership layer) to be complete; this change closes that change's three remaining
  public acceptance cases.
