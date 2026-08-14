# Architecture review — loop effective event source

**Change:** `loop-effective-event-source` (DEC-036 D1 + D2)  
**Updated:** 2026-08-14  
**Evidence:** [`session_20260814_035414.log`](../../../captures/session_20260814_035414.log)

**Related:** DEC-035 Layer C–D ([`loop_layer_history_persistence_architecture.md`](../../../docs/Plans/loop_layer_history_persistence_architecture.md)); M6 display ([`runtime-derived-representation-heap`](../runtime-derived-representation-heap/ARCHITECTURE-REVIEW.md)); overdub overlap (DEC-031/032).

**Orthogonal:** Post-stop `PlaybackFullMaterialize` legacy path; `LoopPersist` slice scheduling; Layer C persisted checkpoint; Layer D `LoadLoopJob` publication gate.

---

## North star

Overdub entry must consume an **already-existing** effective event source and must **never** require display reconstruction.

---

## Per-phase gates

### Phase D1 — Incremental effective event source

| Question | Answer |
|----------|--------|
| **Owner module** | `Loop` — `EffectiveEventStore` (evolve `passesMaterializedStore_` + mutation hooks) |
| **Primary invariant** | Effective committed content is current **before** `beginOverdubSession()`; updates on content mutation, not on overdub press |
| **Ownership change?** | **YES** — derived effective store becomes eager incremental owner for committed runtime queries |
| **State transition change?** | **NO** for FSM; **YES** for scheduling — overdub no longer waits on full-loop rebuild |
| **Behavior-preserving?** | **YES** for audible note semantics and overlap resolution |
| **Reuse** | **YES** — extend `passesMaterializedStore_`, `CommittedEventRange`, commit/undo hooks |
| **Phase scope** | D1 only — no persist checkpoint, no load range publication |

#### D1 acceptance criteria (non-negotiable)

1. `beginOverdubSession()` performs **no** full-loop `gatherCommittedEvents()`.
2. `beginOverdubSession()` performs **no** full-loop `reconstructDisplayNotes()`.
3. Effective runtime source is **already available** before overdub is requested.
4. Store updates are a consequence of **content mutation/commit**, not overdub button press.
5. API can serve a **range** (`range(window)`), even if v1 maintains a full internal flat store updated incrementally.
6. Display-note reconstruction is **not** a prerequisite for overdub entry.
7. Measured overdub-open latency is bounded independently of historical pass count, loop event count, and display note count (`ODUB,begin_capture` gate).
8. RC-K3 note-off behavior unchanged (`test_overdub_source_view`, overlap hold fixtures).

#### D1 implementation review

| Check | Pass |
|-------|------|
| Mutation hooks update store on commit / undo / edit / load | [x] |
| Equivalence native fixtures vs `passes.materialize` | [x] |
| No full-loop gather at overdub entry (test/counter) | [x] |
| `pio test -e native` | [x] |
| Device `035414` class: `begin_capture` < 50 ms | [ ] (D2 gate) |

---

### Phase D2 — Range-driven overdub source window

| Question | Answer |
|----------|--------|
| **Owner module** | `Loop::establishOverdubSourceView` + overlap note-off consumers |
| **Primary invariant** | Overdub source is established from `effectiveEvents().range(overdubWindow)` only |
| **Ownership change?** | **NO** — consumes D1 store |
| **State transition change?** | **NO** |
| **Behavior-preserving?** | **YES** — overlap geometry unchanged |
| **Reuse** | **YES** — window margin policy, `OverlapNoteIdSet`, RC-K3 path |
| **Phase scope** | D2 only — replace full `overdubSourceViewNotes_` rebuild |

#### D2 acceptance criteria

1. `establishOverdubSourceView` queries effective store **range** only (playhead ± margin).
2. No `reconstructDisplayNotes` on overdub entry path.
3. Gate 0 (`OverlapNoteIdSet` cap 128) unchanged.
4. Native overlap / note-off fixtures PASS.

#### D2 implementation review

| Check | Pass |
|-------|------|
| Windowed source establishment | [ ] |
| RC-K3 / overlap hold native PASS | [ ] |
| `pio test -e native` | [ ] |
| Device overdub overlap HITL or `035414` manual re-run | [ ] |

---

**Approval:** APPROVE design gate — D1 firmware may proceed after tasks 1.x complete.
