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

## Invariants

1. Sort keys use precomputed `playbackEventPhase` — not allocating projection in comparator.
2. `Loop.nextEventIndex` remains event scan index; projection cycle is separate rolling origin.
3. Queued start (`queuedStartTick`) commits at grid — updates `projectionCycleStartTick` at commit (UIP Phase 4).
4. **Playback is read-only** — emit MIDI and advance indices only; never append to capture, synthesize note-offs into storage, or modify `pendingNotes`. See [LOOP_MIDI_STORAGE_AND_VALIDATION.md](../../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) capture ownership invariants.

---

## Related

- [Storage.md](Storage.md) — authoritative passes
- [DerivedViews.md](DerivedViews.md) — event representation
- [IntervalProjection.md](IntervalProjection.md) — phase / wrap
- [Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md](../../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) — merge / materialize
