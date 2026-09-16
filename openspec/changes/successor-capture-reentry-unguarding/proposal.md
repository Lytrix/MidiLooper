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
- Declare the successor capacity explicitly and split it by direction:
  **START capacity requires the live role to be free; STOP capacity requires the
  deferred/frozen role to be free.** A pending predecessor therefore must not block
  a successor **start**, and must block a successor **stop** while it owns the sole
  deferred role. The successor whose stop is denied remains live and completely
  intact until a later stop request succeeds. Increment B supports **depth 2 in
  capture lifecycle, not depth 2 in deferred-stop queueing**: one finalizing
  predecessor plus one independently recording successor; it does not permit two
  simultaneously deferred capture intervals. The supported sequence is:

  ```text
  A recording
  -> stop A                      A owns the frozen/deferred role
  B start                        admitted into the live role
  B recording                    B events accepted exactly
  B stop while A owns frozen     denied: queue/capacity full
                                 B remains the live recording interval; no events,
                                 notes, reservations or stop descriptor consumed
  A retires                      frozen role becomes available
  B's subsequent stop request    admitted; B transfers into the frozen role exactly
                                 once; the live role becomes available again
  C start                        now legal
  ```

  A denied stop reports its own explicit outcome (`queue_full` /
  `storage_denied`) rather than being queued silently or dropped after acceptance.
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

- **The three-interval reproducer vs. the declared capacity — RESOLVED (re-scope).**
  `test_button_a_three_intervals_accept_notes_with_service_paused` is renamed to
  state its actual contract,
  `test_button_a_successor_stop_denied_until_deferred_role_available`, and asserts
  the supported sequence above: A deferred, B start admitted, B capture exact, B stop
  **denied and non-destructive**, then B's subsequent stop admitted once A retires,
  after which C may start. The original three-deep "A and B remain queued" scenario
  is kept as a documented **follow-up capacity** test/spec (N-job deferred queueing),
  not as a skipped red case in the current suite. See `design.md` D2.

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
