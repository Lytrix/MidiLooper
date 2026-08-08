# Runtime architecture — derived representations

**Parent:** [RuntimeArchitecture.md](RuntimeArchitecture.md)

**Naming:** “Derived view” in diagrams means **derived representation** in prose and new docs. Avoid new `*View` type names in code ([NAMING.md](../NAMING.md)).

---

## Question

> How should the authoritative timeline be represented for runtime consumers?

A derived representation is **not** owned by a consumer. It has an owner, revision, invalidation rules, and build policy.

It is **not** the same as a window or interval — one representation can be projected onto many intervals.

---

## Pipeline

```
Capture Storage
        │
        ▼
Derived Event Representation
        │
        ├─────────────────┐
        ▼                 ▼
Playback Representation   Display Representation
        │                 │
        ▼                 ▼
   (optional)          LED query
```

---

## Owner table (brownfield)

| Representation | Owner | Storage (today) | Revision | Build policy | Consumers |
|----------------|-------|-----------------|----------|--------------|-----------|
| **Event (loop)** | `Loop` | `passesMaterializedStore_` / `midiEvents()` | Materialize dirty | On demand; not on MIDI-clock tick | Playback seed, display reconstruct input |
| **Event (NOTE_EDIT)** | `EditManager` | `NoteEditSession.store` | `sessionPreviewRevision_` | On geometry commit / store write | `sessionMidiEvents()`, overlap analyze |
| **Display** | `Loop` | `visualCache.notes`, `capturePreview.notes` | `revision` fields | **Target:** idle-deferred when dirty; not every PLAYING frame | `DisplayManager` |
| **Playback order** | `Track` / `LoopPlaybackRuntime` | `mergedEvents`, `playbackOrder` | Runtime rebuild flag | Prewarm off hot path; rebuild on transport / invalidation | `playMidiEvents` |
| **LED bar hint** | `MidiLedManager` | Reads display or lightweight scan | Inherits display/event revision | **Target:** no full display rebuild on bar tick | Bar LEDs |

**Exception:** `NoteEditSession.store` is a live **overlay** on passes during edit — not a pure derivative of `materialize()` alone. Tier-2 playback audition reads session store while transport plays.

---

## Runtime requests

Combine **representation + `TickInterval`**:

```
Runtime Request = Derived Representation × Interval → Consumer result
```

| Consumer | Representation | Interval example |
|----------|----------------|------------------|
| Playback | Event representation | Full loop phase window at `projectionCycleStartTick` |
| Display | Display representation | Detailed 16-bar `TickInterval` on piano roll |
| LED | Event or bar-presence summary | Single bar tick range |
| NOTE_EDIT analyze | Event + Edit projection context | Analysis window (v1: `[0, loopLength)`) |

Same interval, different consumers:

```
TickInterval bars 8–24
    → Playback: send MIDI events in projected phase range
    → Display: filter DisplayNotes intersecting window
    → LEDs: note-present query for bar indices in range
```

---

## Invalidation rules

1. **Storage mutation** → invalidate event representation → cascade to display + playback representations.
2. **Consumer** checks revision; if stale, **asks owner** to rebuild (or accepts deferred staleness per policy).
3. **Consumers must not** call `ensureVisualCacheBuilt()` / `materializeToEventVector()` on hot paths (PLAYING, per-CC fader) — owner schedules on idle or explicit refresh.

---

## Incremental build (direction)

`VisualCache.dirtyBars` is the seed for **bar-granular** display rebuild. Target: rebuild only dirty bars into the display representation, then apply interval filter — not full-loop reconstruct per window move.

---

## Related

- [IntervalProjection.md](IntervalProjection.md) — interval math after representation exists
- [Display.md](Display.md), [Playback.md](Playback.md) — consumer contracts
- [INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md](../../Guides/INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md) — allocator tier for cold representations
