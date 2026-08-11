# Long overdub wrap — source-view overlap / display freeze

**Status:** OpenSpec refined — Phase 1 = `overdubSourceView` + native tests  
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

## Normative model (OpenSpec)

- **`overdubSourceView`** established at overdub start (not a “loop freeze”)
- Materialize-aware, stable for the session; includes `editPasses`
- Per-note evaluate-on-insert across wraps against that view
- **`overdubPass` = complete delta** (Add + Shorten/Remove); source immutable
- One session → one pass / one undo
- `183525` fix framing: efficient wrap-safe **candidate lookup into the source view** — not “faster duplicate detection”

## Debugging boundary

```
Phase 1: overdubSourceView + native tests (no deny wiring)
Phase 2+: resolve → delta encode; reverse-tick early-out retirement; lookup efficiency
Separate: deny WARN/CAP throttle if RING floods
Persistence / Critical reclaim: out of scope
```

## Next

1. Architecture gate Phase 1
2. Implement establish/clear `overdubSourceView`
3. Native tests; then Phase 2 encode pin (design Open Q4)
