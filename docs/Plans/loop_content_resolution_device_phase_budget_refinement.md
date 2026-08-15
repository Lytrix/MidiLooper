# Loop content resolution — device phase-budget audit (5.16)

**Status:** Open 2026-08-15 — paper audit from [`151450`](../captures/session_20260815_151450.log). No firmware change in this slice.  
**Change:** `openspec/changes/loop-content-resolution/` (DEC-037 Stage 9)  
**Parent:** 5.15 closed — [`loop_content_resolution_span_boundary_index_refinement.md`](loop_content_resolution_span_boundary_index_refinement.md)

**Does not start:** A2, B, `byTick` swap, `resolveState` / checkpoint changes, 5.1 / 5.2, Stage 6, restoring the arm cap.

**Boundary:** trust `walk=0` and `st=13 ms`. Do not reopen the resolver query model. Measure **phase total** vs **largest uninterrupted slice**. The user-visible 5.7 leftover is the latter (and `DFRAME` log gaps), not `reb=13.85 s`.

---

## Goal

Identify which remaining `LoopContentResolution` rebuild phases violate the realtime slice contract after 5.15c.

Not the goal: make the whole rebuild finish faster. `reb=13.85 s` on a 139-bar / 2394-note loop can be acceptable if every slice stays bounded.

---

## 5.15 closed (do not reopen)

| | C [`143009`](../captures/session_20260815_143009.log) | A [`151450`](../captures/session_20260815_151450.log) |
|--|--|--|
| `reb` | 104.36 s | 13.85 s |
| span-index build | ~108 s map emplace | `app=5.49 s` + `sort=10.2 ms` |
| `resolveState` | 16.4 ms | 13.0 ms |
| `walk` | 0 | 0 |

A wins. A2 unjustified. B unjustified. `startsByTick` representation is solved.

---

## Contract

For every phase, record when the capture has it:

| Field | Meaning |
|-------|---------|
| wall | first `phase,<name>` → next phase (or complete) |
| `idle_maint` max | 5 s DIAG window that overlaps the phase |
| `loop_rem,idle_maint` max | one-shot remainder — **largest measured slice** |
| `DFRAME` duration | paint cost (µs) |
| `DFRAME` gap | wall between `DFRAME` lines (1 Hz when healthy ≈ 0.96 s after complete) |
| work units | `ev` / `span` / `notes` on the last phase line |
| allocations | **not in [`151450`](../captures/session_20260815_151450.log)** |

Healthy after-complete in [`151450`](../captures/session_20260815_151450.log): `idle_maint` 276 µs, `DFRAME` duration ~10 ms, `DFRAME` gap ~0.96 s.

---

## Paper audit — [`151450`](../captures/session_20260815_151450.log)

139 bars, track 0 slot 0, `hist=2394`, complete @ 147.76 s.

| Phase | Work | Wall | Largest slice | `DFRAME` duration | `DFRAME` gap |
|-------|------|------|---------------|-------------------|--------------|
| `idx` pass 0 | 4400 events | 32.86 s | `loop_rem` **83 ms** | 11–20 ms | 1.05–2.98 s |
| `pair` pass 0 | 4408 events | 41.93 s | `loop_rem` **63 ms** | 20–24 ms | 2.00–2.53 s |
| `idx` pass 1 | 224 events | 5.23 s | `loop_rem` **121 ms** | 24–25 ms | 4.39 s (one) |
| `pair` pass 1 | 200 events | 2.99 s | `loop_rem` **92 ms** | 25 ms | (no pair) |
| `idx`/`pair` 2–4 | small passes | <1 s each | `loop_rem` 64–102 ms | — | — |
| **`prep`** | materialize + edits, **one slice** | 0.31 s | **`loop_rem` 182 ms** | none in window | none |
| `recon` | 4536 events / 8 | 10.71 s | `loop_rem` **116 ms** | 25 ms | 0.98–1.01 s |
| `proj` | 2304 notes / 8 | 12.75 s | `loop_rem` **64 ms** | 25 ms | 1.26–1.28 s |
| `dedup` | 2394 notes, one slice | 0.06 s | `loop_rem` **60 ms** | none | none |
| `spans` | 2394 notes / 8; `app=5.49 s` | 17.43 s | 5 s `idle_maint` **60 ms**; **no** `loop_rem` | 25 ms | 1.38–2.24 s |
| `sort` | 4788 entries | 0.08 s | CPU **10.2 ms** | none | none |
| `ckpt` | sparse `soundingAt` | 0.76 s | (no `loop_rem` line) | none | none |
| `state` | `st=13 ms` | <1 ms wall | 13 ms | none | none |

`idx`/`pair` after pass 0 are the same `byTick` owner; pass 0 dominates wall.

---

## Findings (from this capture only)

1. **No phase shows a 1.4–2.2 s uninterrupted slice.** `DFRAME` duration stays 11–25 ms. After complete, `DFRAME` is logged every ~0.96 s. Gaps of 1.4–2.2 s mean the 1 Hz line is late, not that one call ran 1.4 s.
2. **`DFRAME` gaps >1 s occur in more than one sliced phase:** `idx` pass 0, `pair` pass 0, `proj`, `spans`. They are not unique to the span-boundary index.
3. **`prep` is the only remaining one-slice rebuild step clearly over 50 ms** (182 ms). Same owner as 5.6; still unsliced.
4. **`byTick` (`idx`/`pair`) is the largest remaining wall** (pass 0 ≈ 75 s) with per-slice `loop_rem` 63–83 ms on pass 0 and 92–121 ms on pass 1. That is a later representation candidate, not a 5.15 reopen, and not proven to be *the* 1 Hz `DFRAME` culprit.
5. **Allocations per phase are unmeasured.** Do not add that instrumentation unless a later slice needs it to pick an owner.

---

## Open after this paper audit

1. Slice `prep` so `RebuildPrepare` is bounded work + yield (5.6 pattern). First implementation candidate — not started here.
2. Leave `byTick` until a named 5.17 (same A lens as 5.15). Do not infer it is next from wall time alone.
3. Do not treat `reb=13.85 s` as the optimization target.
4. Do not start 5.1 / 5.2 / 6.x until 5.7’s remaining slice bar is decided.

---

## Out of scope

`resolveState`, checkpoints, A2, B, resolver architecture, `TickIndex::byTick` swap, production consumer swap.
