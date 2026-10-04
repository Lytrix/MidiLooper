# Runtime architecture — playback consumer

**Parent:** [RuntimeArchitecture.md](RuntimeArchitecture.md)

**Naming:** [NAMING.md](../NAMING.md) — Playback vs Playing, interval projection vocabulary.

---

## Runtime request

```
Derived Event Representation  +  Playback interval  →  MIDI out
```

| Input | Today |
|-------|-------|
| Representation | `LoopPlaybackRuntime` (`mergedEvents`, `playbackOrder`); NOTE_EDIT: `editManager.sessionMidiEvents()` when edit active |
| Interval | Rolling cycle via `projectionCycleStartTick` + `playbackEventPhase` per event |

`currentTick` is the clock. `Track::playMidiEvents` does **not** read `visualCache`. It phases `currentTick` and sends from `mergedEvents` (raw MIDI). Long loops gather **2 bars** around the playhead; short loops gather the loop. Live overdub adds `loop.capture.store`. That is the play consumer in [DerivedViews.md](DerivedViews.md) § Consumers.

---

## Owner

**`Track`** owns playback runtime per slot. **`TrackManager`** orchestrates transport-wide `updateAllTracks`.

Playback **does not** own storage or display representations.

---

## Build policy

- **No** `LoopPlaybackRuntime` allocation on first tick after transport start (prewarm when heap allows).
- Rebuild playback order off hot path when `invalidateCaches()` or pass revision changes.
- NOTE_EDIT: `sessionPreviewRevision_` gates preview refresh; this-session settled overlay rows audition via splice onto the playback window around `currentTick`, not per-fader MIDI note-on. Today’s firmware still full-replaces from `sessionMidiEvents()` (DEC-037 Editor amendment 2026-08-16).

---

## Demoted-target launch during migration

`TrackManager::servicePendingSlotProvision` reserves cold rolling-only storage and
records an unpositioned immutable launch request for an inactive demoted target.
`Track::stepPlaybackViewMaintenance` owns bounded source count/fill/order/retirement
and ready publication. Qualification fills the unpublished build role in one
bounded source pass, then pauses before ordering/admission; no ready/current view
is modified. Admitted work excludes that completed fill and preserves its original
positional promise. The actual switch boundary revalidates current revision,
operation generation and phase-zero coverage, then binds the complete ready view.
A successful empty interval is valid no-new-events coverage. A pass-backed target
without a current root requests the existing on-demand committed-pass republish,
once while no source job is pending; the Snapshot owner constructs it off-clock.
This does not enable bulk loaded-slot hydration. The launch waits for that source.
The initial window's revision stays fixed during its traversal; expiry is sticky,
so later wraps do not revive it. Capture admission upgrades the same reservation
without resetting prepared content. Blocked targets do not hide later provisioning.

The mutable fallback remains beyond initial coverage and on the named legacy dense
overflow route (`launch_legacy_dense`); accepted events are not truncated to fit.
Task 3.7 — fallback removal and task 3.10 — continuous service qualification remain
gated. Native launch correctness is verified, but device launch/timing and sustained
refill are not qualified. Off-clock legacy gathering is not a timing guarantee.

## Gated current-root rolling handoff

The off-clock `serviceRollingPlaybackWindow` prologue selects later two-bar
coverage through the existing request/maintenance owner. At the exact absolute
boundary, `resolveRollingPlaybackWindow` validates the frame and complete private
candidate before adoption and existing MIDI emission. Cursor transfer and physical
storage generation are part of the handoff; an empty interval means no new events,
not silence. Stale/late/unsupported transitions retain mutable fallback.
`PLAYBACK_WINDOW_ROLLING_ENABLED` defaults0; explicit native/capture qualification
is required to enable it. Later lookahead plus fallback gaps is not continuous
service. Capacity, service constants and fallback-removal prerequisites are unchanged.
See the window change's `evidence/requester-retry-integration/ROLLING_HANDOFF.md`.

## Invariants

1. Sort keys use precomputed `playbackEventPhase` — not allocating projection in comparator.
2. `Loop.nextEventIndex` remains event scan index; projection cycle is separate rolling origin.
3. Queued start (`queuedStartTick`) commits at grid — updates `projectionCycleStartTick` at commit (UIP Phase 4).
4. **Playback is read-only** — emit MIDI and advance indices only; never append to capture, synthesize note-offs into storage, or modify `pendingNotes`. See [LOOP_MIDI_STORAGE_AND_VALIDATION.md](../../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) capture ownership invariants.

---

## Related

- [Storage.md](Storage.md) — authoritative passes
- [Snapshots.md](Snapshots.md) — `LoopSnapshot` source, construction, and publication
- [DerivedViews.md](DerivedViews.md) — event representation
- [IntervalProjection.md](IntervalProjection.md) — phase / wrap
- [Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md](../../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) — merge / materialize
- [playback_gather_lcr_consume_enhancement.md](../../Plans/playback_gather_lcr_consume_enhancement.md) — Stage 1 lateness hooks; LCR consume still queued; same `lateness_us` is the scheduling musical gate (not remaining OpenSpec 6.3)
