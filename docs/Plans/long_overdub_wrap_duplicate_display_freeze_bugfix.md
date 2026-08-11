# Long overdub wrap duplicate storm → display freeze

**Status:** OpenSpec proposed — Phase 1 apply next  
**Branch:** `feature/overdub-pass-overlap-resolution`  
**OpenSpec:** [`openspec/changes/overdub-pass-overlap-resolution/`](../../openspec/changes/overdub-pass-overlap-resolution/)  
**Evidence:** [`session_20260811_183525.log`](../../captures/session_20260811_183525.log)  
**Parent display stack:** PR [#29](https://github.com/Lytrix/MidiLooper/pull/29) → `dev`

## Capture verdict

| Fact | Evidence |
|------|----------|
| Loop length | 103680 ticks = 135 bars |
| First wrap | ≈ 885.494 s wall |
| First deny | 885.932 s — `Capture append failed (duplicate)` |
| Deny class | **191× `duplicate`**, **0× `pool_alloc`** |
| CAP | `RING,overflow` ~632 s gap mid-overdub |
| 5a-3 reclaim | **Falsified** for this failure class |

## Debugging boundary

```
isDuplicateCaptureEvent reverse-tick early-out (append order)
        → 183525 Phase 1: source-pass candidate lookup + throttle
NoteGeometryResolver / overdubPass ops
        → OpenSpec Phase 2 (not Phase 1)
Persistence / Critical reclaim
        → out of scope
```

## Normative model

See OpenSpec design D1–D7: one immutable **source pass** (pre-session canonical geometry via chunk/window); evaluate on each inserted note across wraps; one session → one `overdubPass` / undo; no `lastSeenTick` authority.

## Next

1. Architecture gate Phase 1 ([ARCHITECTURE-REVIEW.md](../../openspec/changes/overdub-pass-overlap-resolution/ARCHITECTURE-REVIEW.md))
2. Source-pass lookup + deny throttle
3. Native tests; device wrap+bar41 after Phase 2
