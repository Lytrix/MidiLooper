# Layer D — overdub source-view delay (large loops)

**Status:** Visual-cache overdub source device **PASS** — overdub [`045556`](../captures/session_20260814_045556.log) `begin_capture` 2.2 ms; undo [`112909`](../captures/session_20260814_112909.log) 3 ms, no `VCACHE,full`. **Successor:** [`loop_event_sourced_resolution_architecture.md`](loop_event_sourced_resolution_architecture.md) (DEC-037) — do not optimize `materializeToEventVector` again.  
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

**Shipped implementation (withdrawn as the overdub-open solution):** mutation hooks call `rebuildEffectiveEventStore()` → full `passes.materializeToEventVector`. That is not a delta. Criterion 5's "full internal flat store" clause is the failed path — see reassessment below.

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

## Device FAIL — [`042909`](../captures/session_20260814_042909.log)

Same class as `035414`: 68 bars, ~3600 events, ~1800 display notes, `undo_entries` 117.

| Event | Wall clock | Cost | Owner |
|-------|------------|------|-------|
| Boot `load_frame` | `loop_rem,load_frame,6465779` then `7838621` | **6.47 s / 7.84 s** | Load hydration (D3/D4 — **not** this slice) |
| Undo #1 | 24.388 → 38.721 | **14.3 s** | `notifyCommittedContentChanged` full flatten + `rebuildVisualCacheFromPasses` (`VCACHE,full,ev,3679,notes,1788`) |
| Undo #2 | 41.570 → 56.192 | **14.6 s** | same (`VCACHE,full,ev,3625,notes,1761`) |
| Overdub open | 66.705 → 73.817 | **7.1 s** | `beginCapture` → `ensureEffectiveEventStoreCurrent` full rematerialize |

Overdub is still full-loop evaluation. D2's 16-bar window never runs until after `materializeToEventVector` of the whole loop.

### Why the freshness fix could not hold

1. `notifyCommittedContentChanged` rematerializes **all** events on every pass toggle (undo).
2. `TrackUndo` then calls `rebuildVisualCacheFromPasses()` → `gatherCommittedEvents()` (now the full flatten) + `reconstructDisplayNotes` of ~1800 notes.
3. `TrackUndo` then calls `invalidateCaches()` which sets `passesMaterializedStoreStale_ = true`.
4. `startOverdubbing` calls `markDisplayCachesStale()` → `invalidatePlaybackCaches()` → `discardEventsCache()`.
5. `establishOverdubSourceView` → `copyEffectiveCommittedEventsInRange` → `ensureEffectiveEventStoreCurrent` sees stale or empty cache → **full rematerialize again**.

A warm full flatten cannot survive the existing cache-invalidation contract. Keeping one is the wrong derived view.

---

## Reassessment (2026-08-14) — stop full-loop evaluation

**Reason triggered:** User-requested reassessment after device FAIL; D1 shipped an ownership change (eager full derived store) that conflicts with DEC-035 Layer D (`PlaybackWindow ⊆ AvailableData ⊆ BufferedData ⊆ Loop content`) and with existing `invalidateCaches` / `markDisplayCachesStale`. No reusable "keep entire loop flattened" extension point survives those owners.

**Current architecture:** `LoopPasses` is authoritative. `CommittedEventRange::inWindow` already walks chunks for a tick window without flattening. D1 replaced that with a full `SessionMidiEventVec` rebuilt on every mutation.

**Chosen source (user, 2026-08-14):** when `visualCache` is `slice_clean`, **copy `visualCache.notes` into `overdubSourceViewNotes_`**. That list is already the committed display view overlap consumes. Do not flatten events or `reconstructDisplayNotes` at overdub entry.

Proof in [`043822`](../captures/session_20260814_043822.log): `slice_clean` notes=1799 bars 0–67 dirty=0, then overdub `begin_capture` **425 ms** after `VCACHE,stale_all` (notes still 1799, dirty=1). `startOverdubbing` had called `markDisplayCachesStale()`, so the authoritative check refused the usable cache and walked chunks instead.

```text
beginOverdubSession()
    +-- if committedDisplayVisualCacheAuthoritative: copy visualCache.notes
    +-- else CommittedEventRange::inWindow (fallback only)
    +-- do not markDisplayCachesStale on overdub entry
    +-- do not reconstructDisplayNotes at entry
    +-- overlap: overdubSourceViewNotes_ already populated, or hold-window reconstruct
    +-- MIDI capture active immediately
    +-- display: leave clean cache in place
```

Undo/commit: **mark stale only**. Do not rematerialize. Do not `rebuildVisualCacheFromPasses` on the undo stack. Idle `slice_clean` catches display up.

**Out of scope:** boot `load_frame` 6–8 s (D3/D4); post-stop persist.

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
