## Why

OpenSpec change `resumable-midi-boundary-playback-maintenance` has landed its whole
ownership layer for continuous queued capture (task 2.3: FIFO/queue-head binding,
sealed raw-range ownership, job-owned pending changes/preview/undo baseline,
session-exit receipt ring, interval-qualified prepared companion history, and the
recorded causal predecessor with its superseded rule). Its three public reentry
reproducers stay red **on purpose**: three admission guards still refuse a successor
interval while a predecessor finalizes, and removing them was explicitly excluded
from that change so it could be reviewed — and device-verified — on its own.

This change removes those guards, innermost first, so a player can punch out A and
immediately record B without waiting for A's finalization, while every ownership
guarantee the previous change established keeps holding.

## What Changes

- Replace the blanket "a pending deferred stop refuses a new interval" refusals with
  precise, ownership-based predicates, layer by layer: **Track** (`Track::startOverdubbing`),
  then **TrackManager** (cross-track maintenance exclusion and slot-change finalize
  path), then **Button A** (`MidiButtonActions::handleToggleRecord`).
- Declare the successor capacity explicitly: **one live capture interval plus one
  admitted deferred stop per track** (the two prewarmed `CaptureAdmissionStaging`
  roles plus the single `captureStopOwner`). A stop that exceeds capacity SHALL be
  denied explicitly with its own outcome (`queue_full` / `storage_denied`) and SHALL
  NOT be queued silently or dropped after acceptance.
- Keep a successor's ingress bound to the **live** staging role and the live
  interval's identity/routing/preview for its whole life; a predecessor's finalization
  resolves only against its own receipts (sealed raw range, pending changes, preview,
  undo baseline, recorded causal predecessor).
- Guarantee that old-job completion cannot clear or retarget the live successor:
  pending notes, preview, capture state, storage and track state stay the successor's.
- Keep `hasPendingDeferredOverdubStop()` a background query, never a live-ingress
  gate; add compact transition/aggregate telemetry for admission, denial, reuse and
  commit decisions.
- Non-goals (this change deliberately does **not**): admit a second simultaneous
  frozen stop job (needs a third staging role), drain a multi-job FIFO, add a second
  worker graph, add cross-track reentry, clone `Loop`/`Track`, queue commands, or
  sort/gather/detach unbounded storage at the action edge.
- **No breaking changes.** The removed refusals are internal admission guards; every
  public control surface keeps its existing meaning, and capacity-exhausted presses
  remain explicit denials.

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
- **Tests:** `test/test_continuous_capture/test_continuous_capture.cpp` — the three
  public reproducers (`test_button_a_three_intervals_accept_notes_with_service_paused`,
  `test_manager_reentry_with_a_held_same_pitch_note`,
  `test_track_reentry_does_not_depend_on_manager_guard`) must pass, plus new capacity
  and isolation coverage.
- **Hardware:** observable over serial (record/overdub-stop/reentry), so the change
  needs a device acceptance run before the parent change's task 2.3 checkbox closes.
- **Docs:** `docs/Runtime/CURRENT_WORK.md`, `docs/Runtime/PROJECT_STATE.md`,
  `docs/DELIVERABLE_TRACKING.md`, and the parent change's `tasks.md` /
  `CONTINUOUS-CAPTURE-OWNERSHIP.md`.
- **Dependencies:** requires `resumable-midi-boundary-playback-maintenance` (its
  ownership layer) to be complete; this change closes that change's three remaining
  public acceptance cases.
