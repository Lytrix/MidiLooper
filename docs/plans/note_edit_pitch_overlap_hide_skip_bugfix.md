# Note edit pitch-then-overlap — Hide skip after shortened stub bugfix

**Kind:** bugfix  
**Capture:** [`captures/session_20260808_110111.log`](../../captures/session_20260808_110111.log)  
**Status:** **FROZEN** — native + HITL PASS [`session_20260808_112202`](../../captures/session_20260808_112202.log) @88.669

## Symptom

After `ChangePitch` onto lane 89 and an initial overlap shorten cycle, moving back over a **visible shortened stub** emits `actions=1` (`MoveNote` only). The stub stays visible at its shortened length instead of hiding.

| Time | Event |
|------|-------|
| ~38.7s | Note 7 `ChangePitch` → lane 89 @ 1477–1583 |
| ~41.4s | First overlap into notes 9/13 — Shorten/Hide **works** |
| 42.446 | `HideNote` note 9 (OK) |
| 42.715 | Move 949→997: already Hidden — no-op Hide (OK) |
| 43.044 | `ShortenNote` note 9 on OverlapNoteOff (OK) |
| **44.425 / 47.012** | Move to 997–1103 with note 9 **visible stub** 1008–1044: `actions=1` MoveNote only — **bug** |

Orthogonal to §12 R1–R5 orthogonal-state representation (behavioral authority unchanged).

## Debugging boundary

```
analyzeEditSessionInteractions → resolveConstrainedGeometry → appendOverlapTargetActions
```

Trust constrained geometry (`visible=false` for OverlapNoteOn Hide). Fix **action builder only** — do not reopen `NoteGeometryResolver` or R5 storage encoding.

## Root cause

In `appendOverlapTargetActions`, when `!constrained.visible` and the live row is already right-tail shortened with active overlap closure:

1. CompleteCover → `HideNote` (022849 E1) — OK.
2. Partial cover (202538 skip path) → **no action** — bug.

The outer unconditional `HideNote` branch is unreachable once inside the shortened branch. Hide geometry sets `constrained.endTick == committed.endTick`, so `constrainedShortensTail` is false and the shorten sub-branch never runs.

## Fix

In the already-shortened + `!constrained.visible` branch:

1. Keep CompleteCover → `HideNote` with committed baseline (E1).
2. Keep `constrainedShortensTail` → `ShortenNote` when end differs.
3. **Else** (partial-cover OverlapNoteOn Hide): emit `HideNote` with **live stub span** (`currentSpan`), not full committed baseline — avoids re-inflating stub to committed length (202538 regression).

Already-Hidden (`!livePresent`) remains silent no-op.

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| Ownership change? | No — extend `appendOverlapTargetActions` |
| State transition change? | No — apply constrained Hide already decided |

## Tests

| Test | Source |
|------|--------|
| `test_builder_partial_cover_hides_shortened_stub_110111` | `session_20260808_110111` @44.425 |
| `test_builder_closure_active_shortened_skips_hide_on_invisible_constrained` | Updated — expect Hide-with-stub (was intentional skip) |

`pio test -e native` — 968/968

## HITL validation

[`session_20260808_112202`](../../captures/session_20260808_112202.log):

| Time | Evidence |
|------|----------|
| 76–77s | Lane 89 pitch-then-overlap: consistent `actions=2` (Shorten/Hide + Move) through shorten cycle |
| **88.669** | Partial-cover re-overlap: `HideNote noteId=26 end=1284` (stub, not committed 1391) + `MoveNote` — fix anchor |

Contrast: [`session_20260808_110111`](../../captures/session_20260808_110111.log) @47.012 — `actions=1` MoveNote only (bug).

## Out of scope

- OverlapNoteOn resolve policy (Hide vs Shorten)
- Telemetry `candidates`/`pairs` rename
- `NOTE_EDIT_PROJECTED_STORE_COMPAT` removal
