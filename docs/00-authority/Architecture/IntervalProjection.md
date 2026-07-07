# Runtime architecture — interval projection

**Parent:** [RuntimeArchitecture.md](RuntimeArchitecture.md)

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

**Stage 1:** `generateEquivalentIntervals` — k-shift / wrap candidates (bounded by window).  
**Stage 2:** `selectProjectedInterval` / `selectProjectedIntervalsForDisplay` — consumer policy.

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
Canonical storage (linear pairs)
        → buildCanonicalSpansFromMidi
        → IntervalProjection (wrap/select)
        → Derived display representation (DisplayNote list)
        → DisplayWindowUtils (viewport interval filter)
```

Display is **two steps**: representation (head/tail, open tails) then interval filter (viewport). Do not merge into one “display cache window.”

---

## Hot-path rules (D24)

| API | Heap |
|-----|------|
| `playbackEventPhase` | **No** allocation |
| `generateEquivalentIntervals` | Allowed off hot path; use `ExternalMemoryFirstAllocator` temps |
| Sort comparator | **Never** call allocating projection inside `std::sort` |

---

## Related

- [DerivedViews.md](DerivedViews.md) — what gets projected
- [Guides/NOTE_WRAPPING_LOGIC.md](../../Guides/NOTE_WRAPPING_LOGIC.md) — display-only pairing (legacy context)
- Design decisions D1–D25: `openspec/changes/unified-interval-projection/design.md`
