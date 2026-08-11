# Long overdub wrap — source-view overlap / display freeze

**Status:** Phase 2 PREFLIGHT + DEC-031 (C→A) — next pending-op buffer  

**Branch:** `feature/overdub-pass-overlap-resolution`  
**OpenSpec:** [`openspec/changes/overdub-pass-overlap-resolution/`](../../openspec/changes/overdub-pass-overlap-resolution/)  
**Evidence:** [`session_20260811_183525.log`](../../captures/session_20260811_183525.log)  
**Parent stack:** PR [#29](https://github.com/Lytrix/MidiLooper/pull/29) → `dev`

## Capture verdict

| Fact | Evidence |
|------|----------|
| First wrap | ≈ 885.494 s wall |
| First deny | 885.932 s — `Capture append failed (duplicate)` |
| Deny class | **191× `duplicate`**, **0× `pool_alloc`** |
| CAP | `RING,overflow` ~632 s gap mid-overdub |
| 5a-3 reclaim | **Falsified** for this failure class |

## Phase 1 shipped

- `Loop::establishOverdubSourceView` / `clearOverdubSourceView` on overdub capture lifecycle
- Materialize via `gatherCommittedEvents` (editPass-aware)
- Wrap-safe `gatherOverdubSourceViewEventsInWindow` / `gatherOverdubSourceViewNotesInWindow`
- Native: `test/test_overdub_source_view/`
- Append accept/reject **not** wired to the view yet

## Debugging boundary

```
Phase 1 (done): overdubSourceView + native tests (no deny wiring)
Phase 2+: resolve → delta encode; reverse-tick early-out retirement; lookup efficiency
Separate: deny WARN/CAP throttle if RING floods
Persistence / Critical reclaim: out of scope
```

## Next

1. Phase 2 slice 1 — pending-op buffer + native constrain→ops (`PREFLIGHT.md` / DEC-031)
2. Slice 2 — stop seal + `OverdubPassAdded`+`editPassIds` + restore gate
3. Device verify wrap + bar 41 (`183525` class)
