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

## 4.2 Investigation — deny WARN/CAP throttle (2026-08-12)

**Task intent:** If CAP/RING flooding from append-deny diagnostics still blocks `DFRAME` observation, add a behavior-preserving throttle on deny logs only (design § interim safety).

### Evidence

| Capture | Append deny WARNs | `#CAP,append,deny` | `RING,overflow` | Notes |
|---------|-------------------|--------------------|-----------------|-------|
| [`183525`](../../captures/session_20260811_183525.log) (pre-fix) | **191×** `duplicate` | **0** in file (dropped or not retained) | 1 (coalesced flag) | Deny path was semantic + logger storm |
| [`005502`](../../captures/session_20260812_005502.log) | 0 | 0 | 4 at clear/stop | No deny flood |
| [`010000`](../../captures/session_20260812_010000.log) | 0 | 0 | 1 at transport stop | OLED PASS |

**[`010000`] CAP mix:**

| Window | CAP lines | Dominant tags |
|--------|-----------|---------------|
| During overdub ≈329.7–409.3 s | **0** | — (ring silent / dropped before flush) |
| Stop burst ≈409.3–411 s | **1333** | **SEVT 1061**, REVT 205, LED/MO/MI… |

`emitOverflowNotice` emits **one** `#CAP,RING,overflow` per pending flag — count understates discard volume.

### Verdict

| Question | Answer |
|----------|--------|
| Is deny-log throttle still indicated for G2? | **NO** — duplicate append denies are demoted under `overdubSourceView`; post-fix captures show **0** deny WARNs |
| Does remaining RING/`DFRAME` CAP gap match deny storm? | **NO** — [`010000`] mid-overdub has **0** CAP lines; stop burst is **SEVT/REVT** dump, not `append,deny` |
| Implement 4.2 in this change? | **NO** — wrong lever; would not address stop-path SEVT flood or mid-overdub CAP silence |

**Out of scope follow-up (not 4.2):** stop-path SEVT/REVT CAP burst / ring pressure during long overdub — separate observability work if CAP mid-overdub continuity is required for HITL.

## Next

1. Phase 3 closeout (4.4/4.5); mark 4.2 cancelled (not indicated)
2. `/opsx:archive` when gates accepted
