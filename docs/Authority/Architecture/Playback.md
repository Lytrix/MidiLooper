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

---

## Owner

**`Track`** owns playback runtime per slot. **`TrackManager`** orchestrates transport-wide `updateAllTracks`.

Playback **does not** own storage or display representations.

---

## Build policy

- **No** `LoopPlaybackRuntime` allocation on first tick after transport start (prewarm when heap allows).
- Rebuild playback order off hot path when `invalidateCaches()` or pass revision changes.
- NOTE_EDIT: `sessionPreviewRevision_` gates preview refresh; edited pairs audition via session store, not per-fader MIDI note-on.

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
