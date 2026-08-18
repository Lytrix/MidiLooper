# Design — loop effective event source (D1 + D2)

**Change:** `loop-effective-event-source`  
**Decision:** DEC-036  
**Evidence:** [`session_20260814_035414.log`](../../../captures/session_20260814_035414.log)  
**Plan:** [`loop_layer_d_overdub_rebuild_architecture.md`](../../../docs/Plans/loop_layer_d_overdub_rebuild_architecture.md)

---

## North star

Persistence knows content. Replay reconstructs current Loop state. Editing derives undo/redo. Playback knows ranges.

**Overdub entry must not require display reconstruction.**

```text
                    ┌── Display notes ──→ async idle rebuild
                    │
Layered passes ──► EffectiveEventStore
                    │
                    ├── Playback range
                    │
                    └── Overdub source range
```

Not:

```text
Layered passes → merge all → DisplayNote reconstruct → overdub source
```

---

## D0 conclusion (sufficient — no further capture required)

```text
begin_capture
  7 µs   set_state
  ~6.78 s establishOverdubSourceView
          ├─ gatherCommittedEvents      (full loop)
          └─ reconstructDisplayNotes    (full loop)
```

Separate latency classes (do not unify fixes):

| Operation | Current | Target |
|-----------|---------|--------|
| Overdub start | ~6.8–7.8 s | immediate (bounded, event-count independent) |
| MIDI during overdub | 366–388 ms spikes | bounded / time-budgeted (later slice) |
| Post-stop `LoopPersist` | ~2 s / ~150 slices | cooperative background (existing) |
| `PlaybackFullMaterialize` at stop | legacy path | separate slice |

---

## D1 — Incrementally maintained runtime effective event source

### Responsibilities

1. Hold the canonical runtime view of **committed** loop content (effective prefix of layered passes + applied edit rows).
2. Update incrementally when content mutates: capture pass commit, undo/redo pass toggle, edit apply, loop load complete.
3. Remain **valid before** `beginOverdubSession()` — never built on button press.
4. Expose **range query** API designed for D2 (`range(window)`), even if v1 internally holds a full flattened store updated by delta.
5. Serve playback/edit/overdub **without** constructing `DisplayNote` objects.

### Update model (required)

```text
pass commit / undo toggle / edit apply
        ↓
apply delta to EffectiveEventStore
        ↓
store remains current
```

Forbidden:

```text
[user presses overdub]
        ↓
merge all passes → build store
```

### Reuse vs new owner

`Loop::passesMaterializedStore_` is the starting extension point: today it is **lazy** (stale until `materializeEditViewFromPasses`). D1 promotes it to **eager incremental** maintenance on content mutation hooks, with explicit range export.

Do **not** add a parallel full-loop cache that is populated only at overdub entry.

### API sketch (normative intent, names TBD in implementation)

```text
EffectiveEventStore::isCurrent()     // true before overdub request
EffectiveEventStore::range(start, length, outEvents)
EffectiveEventStore::revision()    // bumps on each delta
```

Dependency direction:

```text
Layered passes → EffectiveEventStore → TickRange → Overdub source
```

not `EffectiveEventStore → entire vector → overdub`.

---

## D2 — Range-driven overdub consumption

### Responsibilities

1. `establishOverdubSourceView` establishes only the **OverdubSourceWindow** around playhead ± margin (existing window constants / overlap policy).
2. Note-off / overlap (RC-K3, Gate 0 cap 128) reads from the established window — not from a full-loop `overdubSourceViewNotes_` built via `reconstructDisplayNotes`.
3. `isRangeAvailable(window)` for overdub demand before querying (runtime subset of Layer D invariant; full play-gate reassessment remains D4).

### Overdub entry flow (target)

```text
beginOverdubSession()
    |
    +-- establish source
    |      +-- obtain effective event source (already current)
    |      +-- copy/query current range window only
    |      +-- establish overlap state from window
    |
    +-- enter OVERDUBBING / beginCapture
    |
    +-- MIDI capture active immediately
    |
    +-- display catches up async (slice_clean path)
```

### Forbidden at overdub entry

```text
beginOverdubSession()
  -> gatherCommittedEvents()          // full loop
  -> reconstructDisplayNotes()       // full loop
  -> rebuildVisualCacheFromPasses()   // as prerequisite
```

---

## Display coupling (explicit)

When the user presses overdub:

```text
button → beginOverdubSession → MIDI capture active → display catches up async
```

`markDisplayCachesStale()` may still run, but idle `rebuildVisualCacheIdleSlice` must not block overdub entry. Stale display during capture is acceptable; blocked MIDI is not.

---

## D3 / D4 (out of scope — recorded)

| Phase | Layer | Stops cost tracking |
|-------|-------|---------------------|
| D3 | C Stage 6 | Load / boot reconstruction vs historical pass count |
| D4 | D Stage 7 | `LoadLoopJob` range fill; `lazy-slot-hydration` COMMITTED reassessment |

D1 stops **runtime overdub-open** cost from tracking pass/event count. D3 stops **load** cost from tracking history.

---

## Tests

- Native: effective store equivalence vs `passes.materialize` after commit, undo toggle, edit apply (prefix invariant extended).
- Native: `establishOverdubSourceView` uses range only; fixture asserts no full-loop gather at overdub entry (counter or hook).
- Native: RC-K3 / `test_overdub_source_view` / overlap hold fixtures unchanged in behavior.
- Device: `035414` scenario — `ODUB,begin_capture` < 50 ms at 3385 events; first `MO` within one MIDI window of transition.

---

## Risks

| Risk | Mitigation |
|------|------------|
| D1 becomes “cache full flat store on overdub” | Acceptance criteria forbid build-on-press; tests on mutation hooks |
| DisplayNote still required for overlap | D2 establishes event-level or narrow-note window without full reconstruct |
| Double maintenance (store + passes) | Single owner; passes remain authoritative; store is derived |
