## Why

The DMA-display baseline proves that the OLED transfer is no longer the primary
post-overdub MIDI-boundary cost: after the second overdub stop,
`ODUB,stop,display` is 4.775 ms, while the following PLAYING timing window
reaches a 47.768 ms MIDI input gap. That window attributes 10.325 ms to
`track_order` and 11.917 ms to `load_frame`, and records NoteOn, NoteOff, and
clock deadline lateness. Both owners currently execute an indivisible turn, so
an intervening MIDI poll cannot bound their worst case.

This change applies the runtime-scheduling principle that timing-critical work
must either be structurally bounded, resumable, or excluded. It is the next
targeted step toward closing immutable-loop-snapshot-passes 4b.3, after the
DMA-display work; it does not claim that display DMA alone closes the MIDI gate.

## What Changes

- Introduce immutable, prewarmed playback views that bind merged events,
  playback order, source identity, and cursor anchor together: current, ready
  successor, and separate successor-after-next build storage. Reserve two
  fixed bridge buffers and capture-source admission staging alongside them.
- Keep `Loop` canonical pass commit immediate only after the per-slot runtime
  has frozen a generation-matched audible-admission candidate for all
  current-cycle affected instances. Capture admission occurs before input enters
  `capture.store`; its seal/commit path transfers the frozen candidate without
  further semantic work. Bridge suppression is cycle-qualified and ledger-safe,
  so an event may repeat after wrap and an already emitted NoteOn always retains
  a closing path. An under-resourced active input is rejected before capture;
  an accepted commit cannot defer its newly due audible output.
- Make active overdub stop a bounded deferred admission: after the stop command
  the runtime enters `OVERDUB_STOPPING`, finishes reserved staging in MIDI-
  separated grains, then atomically commits only to a command-epoch-validated
  destination. A later transport Stop selects STOPPED and invalidates any
  pending return to PLAYING.
- Split each owner into explicit, bounded grains that may yield after MIDI has
  been serviced and resume in a later eligible main-loop turn.
- Keep the display refreshing during PLAYING: distinguish permission to load
  from actual pending work, and let admitted due display frames take priority
  between load slices while retaining load progress and one-child turns.
- Preserve atomic semantic boundaries: an active view is never mutated by a
  successor build, and its promotion preserves exactly-once audible MIDI output
  across the switch. Loop publication, slot-load Commit, and display-frame
  validity are never partially published.
- Add owner-local eligibility at the existing `handleMidiInput()` boundaries
  for these two owners. It admits at most the selected bounded grain when
  transport is timing-critical and keeps denied work pending; it does not add
  an interval-reservation framework or scheduling class.
- Add owner-level progress, yield, and per-grain timing telemetry, plus native
  fixtures and a capture-serial HITL gate that separately reports post-overdub
  PLAYING, RECORDING, OVERDUBBING, `OVERDUB_STOPPING`, and STOPPED windows.
- Require a verified per-owner long-loop coverage-service contract. If it
  cannot prove successor preparation before coverage expires, active windowed
  maintenance is excluded in favor of a full-loop prewarm or STOPPED-only work.
  A source-change contract separately proves bridge coverage through the
  current/ready horizon while the build view prepares the new-source successor.

## Capabilities

### New Capabilities

- `midi-boundary-runtime-maintenance`: bounded, resumable post-MIDI-boundary
  maintenance for playback ordering and load/display work while transport is
  active.

### Modified Capabilities

- `playback-runtime-prewarm`: replace the per-loop mutable `playbackOrder`
  prewarm obligation with fixed per-slot published/pending playback-view
  capacity, including long-loop coverage and the active source-change bridge.

## Impact

- Affected firmware: `TrackManager` playback tick path, `Track` / playback
  runtime view and audible-source admission maintenance, `Loop` playback-order
  storage, `main.cpp`
  deferred-frame orchestration,
  `StorageManager`/`LoadLoopJob`, `DisplayManager`, `MidiServiceDrain`, and
  `RuntimeTimingTelemetry`.
- The derived playback-view ownership transfer from direct `Loop` order
  storage to per-slot runtime storage is explicitly gated: firmware work
  begins only after the required architecture reassessment, migration plan,
  compatibility decision, and recorded approval.
- Existing loop playback, slot switching, display content, save semantics, and
  MIDI protocol behavior remain compatible for every accepted capture-pass
  commit. A continuation may defer a derived representation or display frame,
  but it may not defer newly due audible MIDI content. An active overdub input
  whose fixed audible-admission preflight cannot meet capacity or readiness is
  rejected before capture and reported as an admission failure. Overdub stop is
  deliberately deferred for its declared bounded admission duration; the UI and
  telemetry report `OVERDUB_STOPPING` until the canonical/audible commit occurs.
  `OVERDUB_STOPPING` remains active transport for MIDI safety, persistence
  restrictions, telemetry scoring, and overflow rejection.
- This follows the runtime architecture's bounded/resumable rule without
  expanding the load-only `DeferredJobScheduler`. It is within the active
  persistence/overlay/display delivery surface described in
  [`docs/DELIVERABLE_TRACKING.md`](../../../docs/DELIVERABLE_TRACKING.md).

## Open Product Decisions

- **TBD:** the initial grain definitions and measured worst-case cost for
  playback ordering and each load-frame child. Each must be demonstrably
  proportional to its declared input quantum before it is eligible while
  RECORDING, PLAYING, OVERDUBBING, or `OVERDUB_STOPPING`.
- **TBD:** the MIDI-boundary eligibility budget and safety margin. The proposal
  will reuse the existing per-owner reserve vocabulary where applicable, but
  will not treat the observed 2.596 ms service period as a policy bound.
- **TBD:** the maximum supported active-overdub source-admission size. It must
  fit the prewarmed bridge/staging capacity before capture commit; overflow is a
  reported preflight rejection, not a delayed audible revision.
- **TBD:** the maximum `OVERDUB_STOPPING` duration and stop-finalization grain
  count. It must be guaranteed by reservations for each accepted capture event,
  not inferred from observed service cadence.
- Display-frame freshness may remain coalesced while its successor is pending;
  audible MIDI freshness is not an open decision and must match the existing
  playback semantics at every due event boundary.

## Non-goals

- A general interval-reservation framework, a second top-level scheduler, or
  changing MIDI dispatch order.
- Altering `Loop` ownership of canonical content, persistence format, OLED
  visuals, SPI frequency, or DMA transport behavior. Active capture admission
  gains the explicit preflight rejection and bounded deferred-stop behavior
  described above.
- Treating STOPPED throughput as evidence for active-transport MIDI safety.


## First-record continuity clarification (2026-09-15)

The approved implementation direction includes preparing one-bar playback windows during first recording. Bar 1 is established when recording starts and stays ready as accepted input arrives, independently of rolling later-bar preparation. At the existing final-bar return boundary, playback selects that prepared source without delaying launch, rewinding transport, or waiting for a canonical snapshot. The proposed deferred first-record launch was explicitly rejected. Capacity, final-length binding, held-note closure, and boundary-tail readiness must be proven as part of the existing admission and parity gates.


## Shared playback implementation requirement

Recording preparation reuses the existing playback window storage, fill/order,
coverage lifecycle, bridge and MIDI emission path. Extend those shared owners
where needed; do not implement an independent recording window engine. The
recording source and preparation schedule differ, while ordering and playback
semantics remain shared. The prior recording-only prototype is superseded.
