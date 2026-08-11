# Long overdub wrap — source-view overlap / display freeze

**Status:** G2 Phase 2 firmware complete — device verify (Phase 3) open

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

## Phase 2 shipped (G2)

- Pending Add/Shorten/Hide via `resolveConstrainedGeometry` + shared `noteMinLengthTicks`
- Stop seal companions + `OverdubPassAdded` undo (GUS STK2)
- Restore gate skipped when `hasOverdubSourceView()`
- **`isDuplicateCaptureEvent` demoted** on overdub append when source view established (Record path unchanged)

## Debugging boundary

```
Phase 1–2 (done): source view + pending + seal + demote capture dedup under view
Phase 3: device wrap+bar41; optional deny CAP throttle; retire helper if unused
Persistence / Critical reclaim: out of scope
```

## Next

1. Device verify wrap + bar 41 (`183525` class) — continuous DFRAME / OLED
2. Optional deny WARN/CAP throttle if RING still floods
3. Closeout / archive when Phase 3 gates pass
