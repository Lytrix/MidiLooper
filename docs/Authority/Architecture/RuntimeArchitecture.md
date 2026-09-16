# Runtime architecture

**Authority:** structural model for how timeline data flows from storage to consumers. Subordinate to [ARCHITECTURE_RULES.md](../ARCHITECTURE_RULES.md). Behavior detail lives in [Guides](../../Guides/) and OpenSpec; this document defines **layers, ownership, and invariants**. **Decision:** [DEC-016](../../DECISION_LOG.md#dec-016-runtime-architecture-four-layer-model).

**Entry point for agents:** read this file first for display, playback, LED, or NOTE_EDIT read paths — then the layer-specific child doc.

**Naming:** [NAMING.md](../NAMING.md) — architectural vocabulary and concept boundaries.

**Execution bounds** (interval path, not data flow): [ExecutionBounds.md](ExecutionBounds.md).

---

## Four layers

Each layer answers one question. Do not collapse them.

```
Authoritative Content    →  What is authoritative?
Derived Representations  →  How is it represented for runtime?
Interval Projection      →  Which tick range is relevant?
Runtime Request          →  Representation × interval → consumer input
```

```mermaid
flowchart TB
  CS[Authoritative Content / Storage]
  DR[Derived Representations]
  IP[Interval Projection]
  RR[Runtime Request]
  C[Consumer]

  CS --> DR
  DR --> RR
  IP --> RR
  RR --> C
```

| Layer | Question | Child doc |
|-------|----------|-----------|
| Authoritative content / storage | What is the authoritative timeline? | [Storage.md](Storage.md); effective-state snapshot in [Snapshots.md](Snapshots.md) |
| Derived representations | How should data be shaped for consumers? | [DerivedViews.md](DerivedViews.md) |
| Interval projection | Which part of the timeline matters? | [IntervalProjection.md](IntervalProjection.md) |
| Consumers | Who reads the result? | [Playback.md](Playback.md), [Display.md](Display.md); analyze and LEDs in [DerivedViews.md](DerivedViews.md) § Consumers |

**Invariant:** changing the active **interval** must not require rebuilding the full **representation** unless the selected `LoopSnapshot` source revision changed.

**Invariant:** changing **representation** must not change which **interval** projection selects — only what exists inside it.

---

## Runtime request (common language)

Every consumer is the same pattern:

```
Derived Representation  +  TickInterval  →  Consumer result
```

| Consumer | Representation (today) | Interval (today) |
|----------|------------------------|------------------|
| Play | `mergedEvents` + `playbackOrder` — not `visualCache` | 2-bar gather on long loops; full gather on short; phase via `projectionCycleStartTick` |
| Display | `visualCache.notes` / `capturePreview.notes` | Detailed 16-bar `TickInterval` + optional overview |
| Analyze | Prepared `LoopContentResolution` when the stamp matches; NOTE_EDIT still rematerializes today | Neighborhood around `currentTick` (overdub) or `selectedTick` (NOTE_EDIT target) |
| LEDs | `visualCache.notes` + `capture.store` (presence); tick row is `currentTick` only; track/loop select is slot state | Current bar (16 sixteenths) and bars 0–7 |

Intervals are **consumer-agnostic** — one `TickInterval` (e.g. bars 8–24) can feed playback, display, or LED queries; the consumer interprets the projected slice.

In the snapshot target, the selected complete `LoopSnapshot` supplies
authoritative content to the existing representation owner. The snapshot does
not contain consumer windows or projected intervals; each consumer selects a
bounded source span before using `IntervalProjection` where coordinate
projection is required.

The approved Phase 0 logical snapshot indexes are a span index of pitch →
canonical 768-tick bar → membership lists of `NoteRef`s for complete notes,
plus an event index of bar → membership lists of `EventRef`s. The event index
is an indexed view of existing event-stream order, not a second ordering
authority. A snapshot is self-contained: its content is determined by its own
reference root and does not come from a parent snapshot.

See [DerivedViews.md](DerivedViews.md) § Runtime requests.

---

## Dependency graph (data pipeline)

Representations depend on the selected `LoopSnapshot`; compatibility
representations may temporarily depend on storage records. Consumers depend on
representations — not on each other’s rebuild paths.

```
LoopSnapshot (target immutable authoritative content)
        │
        ▼
Derived Event Representations  (materialized MIDI / session store / analyze indexes)
        │
        ├──────────────────┬──────────────────┬──────────────────┐
        ▼                  ▼                  ▼                  ▼
   Play window        Display list       Analyze indexes    LED pads
   mergedEvents       visualCache        LoopContentResolution
        │                  │                  │                  │
        ▼                  ▼                  ▼                  ▼
   playMidiEvents     DisplayManager     select / overlap   MidiLedManager
                             │
                             └─ 16-step + bar pads read visualCache.notes + capture.store
```

**Rule:** consumers **request** a representation at an interval; they do **not** call peer rebuild APIs (`ensureVisualCacheBuilt` from `MidiLedManager`, etc.).

Live `Capture` and `NoteEditSession.store` remain transient overlays until
their existing commit boundaries publish settled snapshot content. They are
not alternate committed-state authorities.

During migration, `LoopPasses` and materialized data may remain parity or
fallback sources for a caller that has not passed its snapshot migration gate.
They are not a second normal effective-state authority.

---

## Revisions (dependency chain)

Revisions are a **chain**, not independent flags. A consumer checks whether its inputs are stale relative to the chain.

```
Snapshot source revision (publication / selection)
        │
        ▼
Event representation revision
        │
        ├─ compatibility inputs: passes / capture / session store mutation
        ├─────────────┬─────────────┐
        ▼             ▼             ▼
Playback repr.   Display repr.  Analyze indexes
```

**Brownfield identifiers** (until rename migration):

| Concept | Field / API today |
|---------|-------------------|
| Snapshot source identity (target) | Selected `LoopSnapshot` publication or selection |
| Storage dirty | pass publish, capture seal, `NoteEditSession.store` write |
| Display representation | `Loop::visualCache.revision`, `capturePreview.revision` |
| Playback representation | `LoopPlaybackRuntime` rebuild / `sessionPreviewRevision_` during NOTE_EDIT |
| Invalidate hook | `Track::invalidateCaches()` |

Target behavior: validate snapshot and representation revisions → request
rebuild from the owning module → consumer reads when fresh enough for policy
(immediate vs idle-deferred). A stale representation must not trigger a
content-sized full-loop rebuild on a timing-critical path.

---

## Scheduling (responsibilities, not a mandated class)

Coordination remains with the existing domain owner or `main.cpp` idle
maintenance. A new scheduler or parallel semantic owner is not authorized by
the snapshot change.

Each derived representation **must** document:

| Field | Meaning |
|-------|---------|
| **Owner** | Who builds and stores it |
| **Dependencies** | Which storage / upstream representations |
| **Revision** | How consumers detect staleness |
| **Invalidation** | What events bump revision |
| **Build policy** | Immediate, idle-deferred, incremental (bar-dirty), hot-path forbidden |
| **Consumers** | Who may read it |

See [DerivedViews.md](DerivedViews.md) § Owner table.

---

## Large-work boundary

Snapshot completeness, deferred placement, and an existing commit boundary do
not establish an execution bound. Construction, representation rebuild,
serialization, and compatibility migration that execute from
`handleMidiInput()`, clock dispatch, or a track-tick path must use a declared
bounded unit or an empirically bounded unit with an explicit overrun/failure
policy under [ExecutionBounds.md](ExecutionBounds.md).

The active snapshot remains selected until a complete replacement is ready.
No partial snapshot or representation is published, and large-loop persistence
remains owned by `StorageManager` through its deferred units.

---

## Terminology: “cache” vs “representation”

Reserve **cache** for structures whose **primary** purpose is avoiding recomputation of an identical result (e.g. `passesMaterializedStore_` COW flat).

Prefer **derived representation** for consumer-facing shapes:

| Legacy name | Architecture term |
|-------------|-------------------|
| `visualCache.notes` | Derived display representation |
| `primaryWindow.mergedEvents` | Derived playback representation |
| `sessionMidiEvents()` | Derived event representation (NOTE_EDIT overlay) |

Code identifiers may still say `VisualCache` until a rename migration; docs use representation language.

---

## Related authority

| Topic | Document |
|-------|----------|
| Storage / passes / undo | [Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md](../../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) |
| Memory tiers | [Guides/INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md](../../Guides/INTERNAL_HEAP_AND_EXTERNAL_MEMORY.md) |
| UIP engine | [IntervalProjection.md](IntervalProjection.md), `openspec/changes/unified-interval-projection/design.md` |
| End-to-end timeline | [plans/record_overdub_memory_display_timeline_enhancement.md](../../Plans/record_overdub_memory_display_timeline_enhancement.md) |
| Module ownership | [ARCHITECTURE_RULES.md](../ARCHITECTURE_RULES.md) |

## Historical / investigation (not architecture)

Regression bisects, timing tables, and fix validation belong in **plans** — e.g. [overdub_start_64bar_playing_window_regression_bugfix.md](../../Plans/overdub_start_64bar_playing_window_regression_bugfix.md). Do not add investigation detail to this folder.
