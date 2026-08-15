# Loop content resolution — TickIndex flat event index (5.17)

**Status:** 5.17a–c native **complete** 2026-08-15 — pick **A**. Production `byTick` stays C until 5.17d/e.  
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
5.17d  device append / sort / query  — only after A picked
5.17e  swap TickIndex representation — only if 5.17d PASS
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

**Pick A.** No B. No A2. Do not swap production `byTick` until 5.17d measures append / sort / query on device.

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
