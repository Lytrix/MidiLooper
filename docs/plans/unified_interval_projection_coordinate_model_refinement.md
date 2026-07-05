# Unified interval projection — coordinate model refinement

**Kind:** refinement (OpenSpec `unified-interval-projection`)  
**Status:** Integrated into `openspec/changes/unified-interval-projection/` design + spec (2026-07-05)

## Summary

Nine conceptual refinements from architecture review. **No change to overall architecture** — clarifies projection responsibilities and coordinate vocabulary.

| # | Refinement | OpenSpec anchor |
|---|------------|-----------------|
| 1 | Stage 1 math vs Stage 2 consumer policy | D2, spec two-stage pipeline |
| 2 | `ProjectionContext` = coordinate space (core + extensions) | D3, spec ProjectionContext |
| 3 | Prefer `window.start` / `window.end` over start+length | D3b, `TickInterval` |
| 4 | Shared `TickInterval` primitive | D3b, spec TickInterval requirement |
| 5 | `loopLength` stays a duration (period), not an interval | D3b, spec |
| 6 | k bounds from active `window`, not fixed constants | Algorithm section, spec k-bounds requirement |
| 7 | Display: projection then rendering (head/tail) | D4, Display projection vs rendering section |
| 8 | Projection preserves `noteId` (identity invariant) | D8, spec identity requirement |
| 9 | Timeline = new Stage 2 policy only (future) | D4 Timeline row |

## Key types (Phase 1)

```cpp
struct TickInterval { int32_t start; int32_t end; int32_t length() const; };

struct ProjectionContext {
  uint32_t loopLength;
  TickInterval window;
  ProjectionType type;
  // consumer extensions: originTick, projectionCycleStartTick, loopStartTick,
  // selectedTick, queuedStartTick, …
};
```

## Display pipeline

```
generateEquivalentIntervals → selectProjectedIntervalsForDisplay → render head/tail → DisplayNote
```

Playback/Edit stop after Stage 2 single-interval selection.

## Not in scope

- Canonical storage shape changes
- Firmware implementation (Phase 1+)
