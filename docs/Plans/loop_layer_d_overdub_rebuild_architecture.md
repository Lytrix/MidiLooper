# Layer D — overdub source-view delay (large loops)

**Status:** Active — OpenSpec D1/D2 (`loop-effective-event-source`)  
**Date:** 2026-08-14  
**Decision:** [DEC-036](../DECISION_LOG.md#dec-036-runtime-effective-event-source-for-overdub)  
**OpenSpec:** [`openspec/changes/loop-effective-event-source/`](../../openspec/changes/loop-effective-event-source/)  
**Parent:** [loop_layer_history_persistence_architecture.md](loop_layer_history_persistence_architecture.md) (DEC-035 Layers C–D)  
**Supersedes symptom owner:** `LoopUndoHistory` / `UndoStacks` — **closed** Layer A ([`030147`](../captures/session_20260814_030147.log), [`032227`](../captures/session_20260814_032227.log))

---

## Architectural signal (D0)

The overdub operation is cheap; constructing a representation overdub happens to need is expensive.

```text
begin_capture
  7 µs   state transition
  ~6.78 s establishOverdubSourceView
          ├─ gatherCommittedEvents      (full loop)
          └─ reconstructDisplayNotes    (display — wrong layer)
```

This is a **source-view acquisition** problem, not an overdub FSM problem.

Extended north star (DEC-035):

> Persistence knows content. Replay reconstructs current Loop state. Editing derives undo/redo. Playback knows ranges. **Overdub entry must not require display reconstruction.**

---

## Three derived views (decoupled)

```text
                    ┌── Display notes ──→ async idle rebuild
                    │
Effective content ──┼── Playback range
                    │
                    └── Overdub source range
```

Forbidden coupling:

```text
Effective content → DisplayNote reconstruct → overdub source
```

---

## Phase map

```text
D1  Incrementally maintained runtime effective event source
      ↓  (stops runtime overdub cost tracking pass/event count)
D2  Range-driven overdub consumption
      ↓
D3  Persisted checkpoint + tail          (Layer C — load cost)
      ↓
D4  Range-first loading                 (Layer D — publication gate)
```

| Phase | Stops cost tracking | Build on overdub press? |
|-------|---------------------|-------------------------|
| D1 | Runtime merge at overdub open | **Forbidden** |
| D2 | Full-loop note reconstruct at overdub open | **Forbidden** |
| D3 | Load vs historical layer count | N/A (boot/load) |
| D4 | Full-loop hydration before play window | N/A (lazy load) |

---

## D0 PASS — [`035414`](../captures/session_20260814_035414.log)

68 bars, 3385 events, ~1900 display notes, `undo_entries` 114–117.

| Event | Time | Owner |
|-------|------|-------|
| Overdub #1 open | **6.78 s** | `ODUB,begin_capture,6779396` µs |
| Undo visual rebuild | **6.79 s** | `VCACHE,stale` → `VCACHE,full` |
| Post-stop persist | ~2 s | `LoopPersist` ~150 slices — **separate slice** |
| In-overdub MIDI | 366–388 ms | display/idle — **separate slice** |

Evidence sufficient — no further capture cycle required before D1.

---

## D1 — Incrementally maintained runtime effective event source

**Intent:** Stop historical pass merging at overdub entry. **Not** “cache flat store on button press.”

Update model:

```text
pass commit / undo toggle / edit apply / load
        ↓
apply delta to EffectiveEventStore
        ↓
store current before overdub request
```

API designed for D2:

```text
effectiveEvents().range(window)   // not .all() as the only contract
```

Reuse: evolve `passesMaterializedStore_` from lazy to eager incremental.

### D1 acceptance criteria (non-negotiable)

1. No full-loop `gatherCommittedEvents()` in `beginOverdubSession()`.
2. No full-loop `reconstructDisplayNotes()` in `beginOverdubSession()`.
3. Effective source available **before** overdub request.
4. Updates on content mutation, not overdub press.
5. Range-capable API even if v1 holds full internal flat store updated incrementally.
6. Display reconstruction not prerequisite for overdub entry.
7. `ODUB,begin_capture` bounded independently of pass count, event count, display note count (< 50 ms at `035414` scale).
8. RC-K3 / overlap behavior unchanged.

---

## D2 — Range-driven overdub consumption

```text
beginOverdubSession()
    +-- effective store (already current)
    +-- establish OverdubSourceWindow only
    +-- overlap state from window
    +-- MIDI capture active immediately
    +-- display async (slice_clean)
```

---

## Separate slices (do not contaminate D1)

| Slice | Owner |
|-------|-------|
| Post-stop `PlaybackFullMaterialize=50` | `gatherCommittedEvents` legacy branch when edit rows active |
| `LoopPersist` ~2 s bursts | persistence scheduling |
| In-overdub 300–400 ms `midi_input` | display idle / resolve |

---

## Parked

- Overlay loop picker (`set-revision-persistence` §4.8–4.10) — stash on `feature/set-revision-loop-picker`

---

## References

- [`realtime_incremental_work_capture_overdub_architecture.md`](realtime_incremental_work_capture_overdub_architecture.md)
- [`runtime_scheduling_admission_model_architecture.md`](runtime_scheduling_admission_model_architecture.md)
- Archived Layer A: `openspec/changes/archive/2026-08-14-loop-content-history-persistence/`
