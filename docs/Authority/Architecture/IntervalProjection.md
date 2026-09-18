# Runtime architecture — interval projection

**Parent:** [RuntimeArchitecture.md](RuntimeArchitecture.md)

**Naming:** [NAMING.md](../NAMING.md) — Playback window, materialize, merge.

---

## Question

> Which part of the timeline is relevant?

Interval projection is **orthogonal** to derived representations. It operates on canonical or already-projected spans — it does not own storage and does not replace materialization.

---

## Owner

**`IntervalProjection`** (`include/Utils/IntervalProjection.h`, `src/Utils/IntervalProjection.cpp`) — pure math + consumer selection policy. **No storage mutation.**

OpenSpec: `openspec/changes/unified-interval-projection/` (archive to `openspec/specs/unified-interval-projection/` after Phase 6).

---

## Primitives

| Type | Role |
|------|------|
| `TickInterval` | `{ start, end }` — inclusive window; **not** a note |
| `ProjectionContext` | `loopLength`, `window`, `ProjectionType`, extensions (`loopStartTick`, `projectionCycleStartTick`, `selectedTick`, …) |
| `CanonicalNoteSpan` | Linear storage span input |
| `ProjectedNoteInterval` | Output interval in working coordinates |

**Stage 1:** `generateEquivalentIntervals` — k-shift / wrap candidates for the requested window.
**Stage 2:** `selectProjectedInterval` / `selectProjectedIntervalsForDisplay` — consumer policy.

The requested window bounds which projected intervals are selected, but it does
not by itself establish a fixed execution bound for Stage 1. The calculated
`k` range can grow when a canonical span crosses multiple loop lengths.
Timing-critical callers must select a bounded span range before projection and
apply the structural or empirical bound policy from
[ExecutionBounds.md](ExecutionBounds.md) to candidate generation. A local
timer checked only between candidates does not bound one candidate-generation
step.

---

## Intervals are consumer-agnostic

Prefer one interval value in APIs:

```text
TickInterval { start: bar8Tick, end: bar24Tick }
```

| Consumer | Use |
|----------|-----|
| Playback | Phase gate via `playbackEventPhase` (scalar hot path) |
| Display | Filter post-`projectDisplayNotes` list |
| Edit | `projectEditIntervalsForAnalysis` |
| LEDs | Bar index range query |

Do not fork parallel types (`PlaybackInterval` vs `DisplayInterval`) unless a consumer needs extra metadata — the interval is the same tick range.

---

## Relationship to representations

```
Selected `LoopSnapshot` canonical note/event content
        → consumer-owned canonical span selection/build
        → IntervalProjection (wrap/select)
        → Derived display representation (DisplayNote list)
        → DisplayWindowUtils (viewport interval filter)
```

Display is **two steps**: representation (head/tail, open tails) then interval filter (viewport). Do not merge into one “display cache window.”

---

## Readiness deadline and window size

Interval projection owns *which part of the timeline is relevant*. It now also carries **when** a projected
interval must be ready, because playback may only consume immutable state.

**Readiness contract.** After a recording, overdub or edit changes content at a tick, an updated immutable
state must be ready by the time the playhead moves over that tick again. A **missed deadline keeps the previous
version authoritative for that pass** — never torn, never held silent, never deferred in a way that disturbs
timing. Currency is decided by coverage/identity, not by a timestamp.

**Window size is a tunable, not a constant.** Bar-based windows are today's unit (a 2-bar merged playback
window is 1,536 ticks). Finer queued slot switching — the jamming target — needs shorter windows. At
`INTERNAL_PPQN = 192` and `QUARTERS_PER_BAR = 4` (`TICKS_PER_BAR = 768`, 192 ticks per beat):

| Granularity | Ticks | At 120 BPM |
|---|---|---|
| 1 bar (today) | 768 | 2,000 ms |
| 1/4 beat (16th note) | 48 | 125 ms |
| 1/8 beat (32nd note) | 24 | 62 ms |
| 1/16 beat | 12 | 31 ms |
| 1/32 beat | 6 | 16 ms |

Window size trades three coupled quantities: **resident bytes** (how many slots can sound), **achievable switch
granularity**, and **publication load** (smaller windows mean more boundary crossings and more frequent work,
tightening the deadline exactly as the work grows more frequent). It must therefore be measured, not assumed —
see the window-size sweep in `openspec/changes/runtime-working-set-ownership/` (S2.6) and the standby sizing
that shares its arithmetic (S2.7).

## Hot-path rules (D24)

| API | Heap |
|-----|------|
| `playbackEventPhase` | **No** allocation |
| `generateEquivalentIntervals` | No timing guarantee from window selection alone; use `ExternalMemoryFirstAllocator` temps outside the hot path |
| Sort comparator | **Never** call allocating projection inside `std::sort` |

---

## Related

- [DerivedViews.md](DerivedViews.md) — what gets projected
- [Guides/NOTE_WRAPPING_LOGIC.md](../../Guides/NOTE_WRAPPING_LOGIC.md) — display-only pairing (legacy context)
- Design decisions D1–D25: `openspec/changes/unified-interval-projection/design.md`
