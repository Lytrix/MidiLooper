# Loop content resolution — TickIndex flat event index (5.17)

**Status:** 5.17d firmware ready 2026-08-15 — device gate IndexCommit is flat A (`iapp` / `isort` / `win`). Awaiting 139-bar remasure vs [`155953`](../captures/session_20260815_155953.log). Native `commitCapturePass` still fills `byTick`.  
**Change:** `openspec/changes/loop-content-resolution/` (DEC-037 Stage 9)  
**Parent:** 5.16 closed `prep`; next owner from [`153920`](../captures/session_20260815_153920.log) — [`loop_content_resolution_device_phase_budget_refinement.md`](loop_content_resolution_device_phase_budget_refinement.md)

**Does not start:** B, A2, `recon`, `pair` / `byNoteId`, 5.1 / 5.2, Stage 6, restoring the arm cap, collapsing this index into `spanBoundaries`.

**Boundary:** `TickIndex` owns event-range lookup. `StateCheckpoints` owns span-boundary / state lookup. Both may use contiguous storage. They stay separate owners and contracts.

---

## Goal

Replace the **derived lookup representation**, not the query contract.

Not the goal: a faster `emplace()` into the same `std::multimap`. Not the goal: a universal “fast index.”

---

## Why 5.17 (from [`153920`](../captures/session_20260815_153920.log))

Same PSRAM-node failure as `startsByTick` before 5.15:

| `byTick` events | `loop_rem` |
|----------------:|-----------:|
| 2792 | 50.1 ms |
| 3520 | 63.0 ms |
| 3976 | 73.4 ms |
| 4392 | **83.4 ms** |

Then 8 inserts into the filled tree: **101–102 ms**. Pass 1: **34 remainders of 116–121 ms**. Shrinking the batch is not a fix.

### C remasure [`155953`](../captures/session_20260815_155953.log)

Same firmware path as [`153920`](../captures/session_20260815_153920.log) (production `byTick.emplace`). Not 5.17d — `app=` / `sort=` are still the 5.15 span-boundary counters.

```
mat=0,win=0,reb=13808671,st=13046,rep=350,hist=2394,walk=0,app=5488207,sort=10167
```

| | [`153920`](../captures/session_20260815_153920.log) | [`155953`](../captures/session_20260815_155953.log) |
|--|--:|--:|
| `reb` | 13.804 s | 13.809 s |
| `st` | 13046 | 13046 |
| `walk` | 0 | 0 |
| `idx` p0 @ 2792 | 50.1 ms | 50.0 ms |
| `idx` p0 @ 3520 | 63.0 ms | 64.4 ms |
| `idx` p0 last | 83.4 ms @ 4392 | **83.3 ms** @ 4400 |
| `idx` p1 | 34 × 116–121 ms | 34 × 116.1–**120.6** ms |
| `idx` p2 / p3 | 101–102 ms | 101.3 / 102.3 ms |
| `recon` startup | 4 × ≤115.7 ms | 4 × ≤115.7 ms @ ev=8 |
| `pair` p1 | 91 ms | 91.5 ms |
| `prep` `loop_rem` | none | none |
| largest `DFRAME` gap | 4.38 s (`idx` p1) | 4.38 s (`idx` p1) |
| after complete `idle_maint` | 296 µs | 273–282 µs |
| after complete `DFRAME` | 0.967–0.969 s | 0.967–0.969 s |

Boot `lcr,skip,restore` at 7.30 s; LCR `idx` starts at 21.84 s after restore. `load_frame` remainders (98.7 / 866.7 / 7578.6 ms) are boot visual-cache, not LCR. The 49.3 ms `idle_maint` 5 s window at 189.87 s overlaps the LCR tail (`state` at 187.31 s); later windows are 273–282 µs.

---

## Contract (from code)

`indexCapturePassEventRange` inserts one entry per event:

```text
byTick.emplace(event.tick, {passId, eventIndex})
```

`visitTickRange` / `findRawWindow` then:

```text
lower_bound(beginTick) … lower_bound(endTickExclusive)
  skip if pass is not Active
  collect EventRef {passId, eventIndex}
```

So the contract is:

```text
tick ∈ [begin, end) → Active (passId, eventIndex)
```

Wrap windows call that twice: `[start, loopLength)` and `[0, start + windowLength - loopLength)`.

`findRawWindow` then inserts refs into `std::set<EventRef>` ordered by `(passId, eventIndex)` and copies those events. `resolveWindow` applies edits, then `sortResolvedEvents` (tick, type, channel, pitch, noteId).

Effective `resolveWindow` order is **not** raw `byTick` walk order. Equal-tick `multimap` insertion order must still be preserved in the flat list so the range walk matches C (`stable_sort` by tick only).

Disabled passes stay in the index; the query skips them. Do not drop them at build.

`pair` / `byNoteId` is a different structure. Out of 5.17.

`walk=0` on the complete line is `resolveState` after checkpoints exist. Preserve it. Find must not walk pass lists (`indexEntriesVisited` only).

---

## Entry type

Sketch `TickEntry { tick, event }` is not enough: events live on `CapturePassEntry`. Private index entry:

```text
TickEventEntry { tick, passId, eventIndex }
```

Not a musical noun. Not a span-boundary entry.

---

## Three representations (measure A vs C only)

| Id | Representation | Build | Query |
|----|----------------|-------|-------|
| **C** | Current `byTick` `std::multimap` | 1 PSRAM `emplace` / event | `lower_bound` + scan |
| **A** | Tick-ordered `TickEventEntry[]` | C-order append + `stable_sort` by tick | `lower_bound` + scan |
| **B** | A plus a coarse bucket | after A | bucket + scan |

Do **not** build B unless A’s **query** cost requires it. Do not build A2 unless **sort** is a material share of A’s total.

---

## Success criteria

1. Same `findRawWindow` event identities as C (including wrap and disabled-pass skip).
2. Same equal-tick order as C after `stable_sort` by tick only.
3. Same `resolveWindow` as C and as the materialize oracle.
4. Find does not walk pass lists.
5. Index construction: no per-entry tree allocation in the steady state. Measure **append**, **sort**, **total**, **query** separately.
6. RAM lower or no worse than C.

Then implement **only the winner** (5.17e). Production `byTick` stays C until that swap.

---

## Sequence

```text
5.17a  contract (this doc) — done
5.17b  native C vs flat A (append / sort / emplace / query) — done
5.17c  native equivalence (window, order, wrap, oracle, walk) — done
5.17d  device append / sort / query  — firmware ready; capture next
5.17e  keep A / drop `byTick` from `commitCapturePass` — only if 5.17d PASS
```

---

## Native 5.17b / 5.17c

`test_stage517b_equal_tick_event_order` + `test_stage517b_flat_tick_events_match_map` PASS.

| Check | Result |
|-------|--------|
| Same window identities as C | PASS — full loop, 16-bar, wrap |
| Same event order as C walk | PASS — `byTick` vs flat entry-by-entry |
| Equal-tick insertion order | PASS — constructed NOTE_OFF then NOTE_ON at tick 192; `stable_sort` by tick only keeps OFF then ON. Canonical fixture has **0** adjacent equal-tick pairs. |
| Loop-boundary / wrap | PASS |
| `resolveWindow` = C = materialize oracle | PASS |
| Disabled-pass skip | PASS — `findRawWindow` C vs A |
| `walk` | 0 on C and A |
| No pass-list traversal | `indexEntriesVisited` only |

Host microbench (canonical fixture, 94 entries):

| Counter | µs |
|---------|---:|
| C emplace | 23 |
| A append | 11 |
| A sort | 15 |
| A index total | 26 |
| C query (`resolveWindow` × 3) | 151 |
| A query | 144 |

Host malloc is cheap, so C vs A totals are similar. That does not contradict [`153920`](../captures/session_20260815_153920.log): device PSRAM `emplace` is the measured failure (50→83 ms, then 101–121 ms). Query is not worse. Sort is not a material share of a device-class build.

**Pick A.** No B. No A2.

### 5.17d device gate (firmware)

IndexCommit appends `TickEventEntry` rows (8 per slice), then one `isort` slice (`stable_sort` by tick). Arduino then runs the existing `Window` sample (`win=`) and goes to `prep`. `findRawWindow` reads `tickEvents` when that list is non-empty.

Complete line adds `iapp=` / `isort=` (tick-event append / sort). Span `app=` / `sort=` are unchanged.

`indexCapturePassEventRange` still `emplace`s `byTick` for native C tests. Device gate does not call it.

Compare the next capture to [`155953`](../captures/session_20260815_155953.log): `idx` p0/p1 `loop_rem` should collapse; `iapp` + `isort` replace map emplace; `win` should be set; `walk=0`; `st` not worse.

---

## Pre-implementation review

### Ready
- Owner is `TickIndex`. Helpers parallel 5.15 (`append` / `stable_sort` / `findRawWindowFromTickEvents`).
- Production `indexCapturePassEventRange` still `byTick.emplace`.
- `recon` and `pair` untouched.

### Resolved
| Topic | Decision |
|-------|----------|
| Query contract | `tick ∈ [begin, end)` → Active `(passId, eventIndex)` |
| Entry type | `TickEventEntry { tick, passId, eventIndex }` — not `TickEntry { tick, event }` |
| Equal-tick order | `stable_sort` by tick only |
| Owners | `TickIndex` vs `StateCheckpoints` stay separate |
| B / A2 / recon / pair | Out until A is measured on device |

### Open before 5.17d
1. Device append / sort / query on the 139-bar class. Production stays C until that PASS.

### Proceed?
- YES for 5.17a–c. 5.17d is the next move; do not start it in this native commit.

---

## Out of scope

`recon`, `pair`, `byNoteId`, `spanBoundaries`, `resolveState`, B, A2, 5.1 / 5.2 / 6.x.
