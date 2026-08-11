# Long overdub wrap — source-view overlap / display freeze

**Status:** G2 Phase 2 shipped; Phase 3 device verify in progress (OLED PASS on [`010000`](../../captures/session_20260812_010000.log))

**Branch:** `feature/overdub-pass-overlap-resolution`  
**OpenSpec:** [`openspec/changes/overdub-pass-overlap-resolution/`](../../openspec/changes/overdub-pass-overlap-resolution/)  
**Baseline failure:** [`session_20260811_183525.log`](../../captures/session_20260811_183525.log)  
**Parent stack:** PR [#29](https://github.com/Lytrix/MidiLooper/pull/29) → `dev`

## Capture verdict (183525 — pre-fix)

| Fact | Evidence |
|------|----------|
| First wrap | ≈ 885.494 s wall |
| First deny | 885.932 s — `Capture append failed (duplicate)` |
| Deny class | **191× `duplicate`**, **0× `pool_alloc`** |
| CAP | `RING,overflow` ~632 s gap mid-overdub |
| 5a-3 reclaim | **Falsified** for this failure class |

## Phase 3 pass criteria (corrected)

Under G2, overlapping overdub notes are **Add / Shorten / Hide** via `overdubSourceView` + `resolveConstrainedGeometry`. Capture-store `isDuplicateCaptureEvent` is **skipped** when the source view is established.

| Gate | Pass signal | Not a pass/fail signal |
|------|-------------|------------------------|
| Overlap behavior | User OLED: source notes shorten/hide; new notes appear across wraps | Counting `Capture append failed (duplicate)` — expected **absent** |
| Session continuity | OLED keeps updating through wrap(s) | CAP `DFRAME` gaps alone (may be RING/serial; confirm OLED) |
| Stop | Overdub commits; undo removes session delta when exercised | — |
| Optional | `RING,overflow` / deny CAP throttle if serial still floods | Treating zero `duplicate` as proof of notes played |

## Device evidence

| Capture | Wraps | `duplicate` denies | OLED (user) | Notes |
|---------|-------|--------------------|-------------|-------|
| [`005502`](../../captures/session_20260812_005502.log) | No (135-bar loop) | 0 | — | Non-qualifying length / no wrap |
| [`010000`](../../captures/session_20260812_010000.log) | Yes — abs 10224→40800 on loopLen **9984** (3 wraps) | 0 (expected) | **PASS** — hide/shorten observed | Stop via global transport; `hot stop verify check=2`; CAP DFRAME gap during overdub (OLED still updated) |

## Phase 1–2 shipped

- `overdubSourceView` + pending Add/Shorten/Hide + stop seal companions + STK2 undo
- Restore gate skipped when `hasOverdubSourceView()`
- `isDuplicateCaptureEvent` demoted under source view (Record path unchanged)

## Debugging boundary

```
Phase 1–2 (done): source view + pending + seal + demote capture dedup under view
Phase 3: OLED/wrap product verify; optional RING throttle; retire helper if Record-only
Persistence / Critical reclaim: out of scope
```

## Next

1. Confirm Phase 3 closeout (4.4/4.5) or any remaining HITL notes
2. Optional deny/RING CAP throttle if serial still floods
3. `/opsx:archive` when Phase 3 gates accepted
