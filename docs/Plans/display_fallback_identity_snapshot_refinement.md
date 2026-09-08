# Display fallback identity vs current snapshot

**Status:** Stage 1 shipped  
**OpenSpec:** `immutable-loop-snapshot-passes` task `4.4h`  
**Parent:** 4.4g closed
([`analysis_validation_oracle_snapshot_refinement.md`](analysis_validation_oracle_snapshot_refinement.md)).
**Does not start:** hydrate Stage 4a, raise `N`, 4.5, 4.6.

## Debugging boundary

```text
4.4b / 4.4e idle + last-resort committed paint
    ← closed (current-root span copy; Overlay rejected)
rebuildDisplayNotesInWindow
    ← leftover windowed fallback: event reconstruct + Overlay accepted
hydrate Stage 4a / raise N
    ← later
```

## Leftover oracles (code)

| Caller | What it still does | After 4.4b–4.4e |
|---|---|---|
| `rebuildVisualCacheIdleSlice` when current | `appendSnapshotNotesForBarRange`; Overlay discarded | Closed |
| `resolveDisplayNotesCommitted` when current | `resolveDisplayNotes(*snapshot)`; Overlay discarded | Closed |
| `rebuildDisplayNotesInWindow` | `tryGather` events then `reconstructDisplayNotes`; Overlay kept | Leftover. Same owner as windowed paint when `visualCache` does not cover the window. |
| Live capture `includeActiveCapture` | Capture window gather | Keep when capture is active |

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | NO — extend `rebuildDisplayNotesInWindow` / `Loop` snapshot copy |
| State-transition change? | NO |
| Unbounded MIDI-interval owner? | NO — existing paint/cold path |

Reuse: YES. Pass rematerialize stays the no-current-root fallback.

## Invariant

```text
When the published LoopSnapshot is current and capture is idle,
windowed display fallback copies that root's span notes for the
gather window. It does not reconstruct from snapshot events.
Overlay (stale root) is rejected.
```

## Stages

### Stage 1 — windowed fallback uses current-root spans

`Loop::tryCopyCurrentSnapshotDisplayNotesInWindow` copies
`appendSnapshotNotesForBarRange` for the gather bars, then window-filters.
`rebuildDisplayNotesInWindow` uses that copy when the root is current and
capture is not being merged. Overlay gather is rejected. No-current-root
keeps event gather / `resolveDisplayNotes(passes)`. Wrap-held append stays
off the current-root span path.

### Stage 2 — hydrate / raise N (not this slice)

## Out of scope

- Hydrate Stage 4a (`queueLoadedSnapshotPublish` after restore)
- Raising snapshot idle `N`
- Changing occupy live source
- `take_only` / SEVT

## Tests

| Gate | What |
|---|---|
| Native Stage 1 | `test_try_copy_current_snapshot_display_notes_in_window_uses_span_note_ids` PASS. Current-root window keeps span NoteId `7`; drops event-only `64`; stale root returns false. |
| RAM1 | `teensy41-capture-serial` locals **6848** |
| Device | Not required to start |

## Pre-implementation review

### Ready

- Windowed leftover named from `rebuildDisplayNotesInWindow`
- Span copy helper already used by idle / overdub source

### Resolved

| Topic | Decision |
|---|---|
| Live capture merge | Keep existing gather when `includeActiveCapture && captureActive()` |
| Overlay | Reject on the windowed fallback, same as idle / last-resort |
| Hydrate / raise `N` | Not this slice |

### Open before coding

None.
