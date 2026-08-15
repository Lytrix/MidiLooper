# Loop content resolution — device phase-budget audit (5.16)

**Status:** Open 2026-08-15 — 5.16c device **PASS** for `prep` [`153920`](../captures/session_20260815_153920.log). 182 ms one-shot gone. 5.7 still open on `idx`/`recon` slices and `DFRAME` gaps.  
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
3. **`prep` was the only remaining one-slice rebuild step clearly over 50 ms** (182 ms on [`151450`](../captures/session_20260815_151450.log)). 5.16c sliced it.
4. **`byTick` (`idx`/`pair`) is the largest remaining wall** (pass 0 ≈ 75 s) with per-slice `loop_rem` 63–83 ms on pass 0 and 92–121 ms on pass 1. That is a later representation candidate, not a 5.15 reopen, and not proven to be *the* 1 Hz `DFRAME` culprit.
5. **Allocations per phase are unmeasured.** Do not add that instrumentation unless a later slice needs it to pick an owner.

---

## 5.16c shipped (native)

`RebuildPrepare` is no longer one 182 ms call.

- `beginRebuildResolvedEvents` — clear working state (one slice).
- Record / empty-base pass: `appendMaterializePassEvents` 8 events per slice.
- Later passes: `mergeSortedMidiEventRange` — same `a.tick < b.tick` compare as `mergeSortedMidiVectors` / `std::merge`, 8 output events per slice.
- `applyNoteEditPassSequence` — one slice after materialize. `EditApply` unchanged.
- Native `materializeActive` / `prepareRebuildResolvedEvents` still compose the full operation.
- Native `test_stage9_range_prep_matches_full_prepare` PASS.

## 5.16c device PASS — [`153920`](../captures/session_20260815_153920.log)

139 bars, track 0 slot 0, `hist=2394`, complete @ 186.93 s.

```
mat=0,win=0,reb=13803604,st=13046,rep=350,hist=2394,walk=0,app=5485380,sort=10129
```

| | [`151450`](../captures/session_20260815_151450.log) unsliced `prep` | [`153920`](../captures/session_20260815_153920.log) sliced `prep` |
|--|--|--|
| `reb` | 13.85 s | 13.80 s |
| `st` / `walk` | 13.0 ms / 0 | 13.0 ms / 0 |
| `prep` wall | 0.31 s | 38.65 s (cooperative) |
| `prep` `loop_rem` | **182 ms** one-shot | **none** |
| `prep` `idle_maint` after idx overlap | — | **22–25 ms** (333 µs quiet samples) |
| `idx` p1 `loop_rem` | 121 ms | 120.6 ms |
| `recon` `loop_rem` | 116 ms | 115.7 ms |
| after complete | `idle_maint` 276 µs, `DFRAME` ~0.96 s | `idle_maint` 296 µs, `DFRAME` 0.967–0.969 s |

`prep` phase lines: pass 0 `ev` 0→4440, pass 1 456→4416, pass 2 152→4616, pass 3 328→4792. First 5 s `idle_maint` that overlaps prep start is 102.4 ms — same value as `idx` pass 3 `loop_rem`, not a prep slice.

## Next long-running phase — [`153920`](../captures/session_20260815_153920.log)

`loop_rem,idle_maint` is one main-loop call of all 8 tracks’ `processDeferredIdleMaintenance` (one LCR slice on the selected track). Phase lines are 1 Hz, so each remainder is bracketed by the `idx`/`recon` cursor around it.

| Rank | Phase | Largest `loop_rem` | Shape | Owner |
|------|-------|--------------------|-------|-------|
| 1 | `idx` pass 1 | **120.6 ms** (34 remainders 116–121 ms) | 275 events into an already-filled `byTick` | `TickIndex::indexCapturePassEventRange` → `byTick.emplace` |
| 2 | `recon` first ~200 events | **115.7 ms** (4 remainders, then none) | startup only; rest of 10.7 s is quiet | `NoteUtils::appendCanonicalSpansFromMidi` |
| 3 | `idx` pass 2/3 | 101–102 ms | `ev` 0→0 then `pair` ev=8 — one 8-event index into the large map | same `byTick.emplace` |
| 4 | `idx` pass 0 | 50→**83 ms** | grows with cursor | same `byTick.emplace` |
| 5 | `pair` pass 1 / 0 | 91 / 63 ms | `byNoteId` pairing | `pairCapturePassEventRange` |

`idx` pass 0 remainders start only at `ev=2792` (50.1 ms) and rise to 83.4 ms at `ev=4392`. That is map-size cost, not a fixed 8-track floor.

Largest LCR-period `DFRAME` gap is **4.38 s** during `idx` pass 1 (paint duration still 25 ms).

`prep` is closed (no `loop_rem`). `spans` has no `loop_rem`. `reb=13.80 s` is not the fail.

**Next owner:** `TickIndex::byTick` closed by 5.17d [`161355`](../captures/session_20260815_161355.log). Plan: [`loop_content_resolution_tick_index_flat_event_index_refinement.md`](loop_content_resolution_tick_index_flat_event_index_refinement.md). Do not fold `recon` into that slice — different owner.

## Open after this identification

1. 5.17d **PASS**. 5.7 leftover attributed [`162630`](../captures/session_20260815_162630.log): sliced `spanBoundaries` / `tickEvents` reserved this slice only. `spans` 565→125 notes/s; `DFRAME` 1.003→1.591 s (paint 12 ms). Plan: [`loop_content_resolution_spans_dframe_gap_refinement.md`](loop_content_resolution_spans_dframe_gap_refinement.md).
2. Do not treat `reb` as the optimization target.
3. Do not start 5.1 / 5.2 / 6.x until 5.7’s remaining slice bar is decided.

---

## Out of scope

`resolveState`, checkpoints, A2, B, resolver architecture, `TickIndex::byTick` swap, production consumer swap.
