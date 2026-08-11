# Long overdub wrap — source-view overlap / display freeze

**Status:** FROZEN — Phase 3 PASS; OpenSpec **archived** `2026-08-12-overdub-pass-overlap-resolution`

**Branch:** `feature/overdub-pass-overlap-resolution`  
**OpenSpec:** [`archive/2026-08-12-overdub-pass-overlap-resolution`](../../openspec/changes/archive/2026-08-12-overdub-pass-overlap-resolution/) · specs [`overdub-pass-overlap-resolution`](../../openspec/specs/overdub-pass-overlap-resolution/)  
**Baseline failure:** [`session_20260811_183525.log`](../../captures/session_20260811_183525.log)  
**Device PASS:** [`session_20260812_010000.log`](../../captures/session_20260812_010000.log)  
**Parent stack:** PR [#29](https://github.com/Lytrix/MidiLooper/pull/29) → `dev`

## Capture verdict (183525 — pre-fix)

| Fact | Evidence |
|------|----------|
| First wrap | ≈ 885.494 s wall |
| First deny | 885.932 s — `Capture append failed (duplicate)` |
| Deny class | **191× `duplicate`**, **0× `pool_alloc`** |
| CAP | `RING,overflow` ~632 s gap mid-overdub |
| 5a-3 reclaim | **Falsified** for this failure class |

## Phase 3 pass criteria

Under G2, overlapping overdub notes are **Add / Shorten / Hide** via `overdubSourceView` + `resolveConstrainedGeometry`. Capture-store `isDuplicateCaptureEvent` is **skipped** when the source view is established.

| Gate | Pass signal | Not a pass/fail signal |
|------|-------------|------------------------|
| Overlap behavior | User OLED: source notes shorten/hide; new notes appear across wraps | Counting `Capture append failed (duplicate)` — expected **absent** |
| Session continuity | OLED keeps updating through wrap(s) | CAP `DFRAME` gaps alone (may be RING/serial; confirm OLED) |
| Stop | Overdub commits; undo removes session delta when exercised | — |

## Device evidence

| Capture | Wraps | `duplicate` denies | OLED (user) | Notes |
|---------|-------|--------------------|-------------|-------|
| [`005502`](../../captures/session_20260812_005502.log) | No (135-bar loop) | 0 | — | Non-qualifying |
| [`010000`](../../captures/session_20260812_010000.log) | Yes — abs 10224→40800 on loopLen **9984** (3 wraps) | 0 (expected) | **PASS** — hide/shorten | Stop via global transport; CAP DFRAME gap (OLED still updated) |

## Shipped (Phases 1–3)

- `overdubSourceView` + pending Add/Shorten/Hide + stop seal companions + STK2 undo
- Restore gate skipped when `hasOverdubSourceView()`
- `isDuplicateCaptureEvent` demoted under source view (Record path unchanged)
- Device OLED wrap PASS (`010000`); 4.2 deny throttle **cancelled** (not indicated)

## Stage 5 cross-link

`183525` **duplicate** class is closed here (not Critical reclaim). Stage 5a-3 **`pool_alloc`** proof remains open — [`long_overdub_stage5a3_critical_reclaim_verification_refinement.md`](long_overdub_stage5a3_critical_reclaim_verification_refinement.md).

## 4.2 Investigation (cancelled)

Post-G2 captures: 0 deny WARNs. [`010000`] mid-overdub CAP silence + stop **SEVT** burst — not `append,deny`. Deny-log throttle would not fix that. See git history / prior §4.2 table in commit `20c177b`.

## Misattribution guard

Do **not** reopen this plan for:

- Critical `pool_alloc` reclaim (Stage 5a-3)
- Stop-path SEVT/REVT CAP ring pressure (separate observability)
- U1 unified persistent pass storage (out of scope; DEC-032)

## Closeout

| Item | Status |
|------|--------|
| OpenSpec tasks 4.1–4.4 | Done |
| Guide | [`LOOP_MIDI_STORAGE_AND_VALIDATION.md`](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) |
| Runtime | CURRENT_WORK / PROJECT_STATE |
| Archive | **Done** — `openspec/changes/archive/2026-08-12-overdub-pass-overlap-resolution/` |
