# Loop content resolution — 5.7 DFRAME during sliced append (reserve)

**Status:** Native **5.7a PASS** 2026-08-15 — device remasure owed.  
**Change:** `openspec/changes/loop-content-resolution/` (DEC-037 Stage 9)  
**Parent:** 5.17 complete — [`loop_content_resolution_tick_index_flat_event_index_refinement.md`](loop_content_resolution_tick_index_flat_event_index_refinement.md)  
**Evidence:** [`162630`](../captures/session_20260815_162630.log)

**Does not start:** B, A2, `recon`, `pair` / `byNoteId`, `channelForNoteId` rewrite, 5.1 / 5.2, Stage 6, restoring the arm cap.

**Boundary:** `StateCheckpoints::appendSpansFromNotes` owns span-boundary growth. `TickIndex::appendTickEventEntries` owns tick-event growth. Same sliced-append reserve contract. Query contracts unchanged.

---

## Architecture checkpoint

1. **Ownership change?** NO — extend the two existing append functions.
2. **State transition change?** NO — idle `DeviceGateSession` phases unchanged.

No formal trigger. Lightweight preflight: owner = those appends; change = reserve known remaining/final size; tests = native capacity + existing range equivalence; device = 5.7 remasure.

---

## Goal

Close the 5.7 leftover: **no 1 s `DFRAME` gaps** on the >63-bar idle gate. Size and `idle_maint` remainder already PASS.

Not the goal: make `reb` smaller as the pass criterion. Not the goal: rewrite `channelForNoteId`.

---

## Proof from [`162630`](../captures/session_20260815_162630.log)

`DFRAME` is every 30th `DisplayManager::update` (`dframeCounter % 30`). Paint duration stays **12.3–13.3 ms** during `spans`. After complete, gaps are **0.968 s**. The leftover is the 30-frame cadence stretching, not a 1.5 s paint.

`appendSpanBoundaryEntries` (and `appendTickEventEntries`) reserved **this slice only** (`size + 16` / `size + 8`). `spans` already reserved `notes.size()` once. `spanBoundaries` did not.

`spans` notes/s drops as the cursor grows (quadratic copy):

| span cursor | notes/s |
|------------:|--------:|
| 8 → 584 | 564.9 |
| 584 → 1000 | 413.1 |
| 1000 → 1280 | 271.0 |
| 1280 → 1496 | 209.5 |
| 1496 → 1688 | 184.9 |
| 1688 → 1864 | 172.0 |
| 1864 → 2032 | 161.4 |
| 2032 → 2192 | 153.0 |
| 2192 → 2344 | 145.9 |
| 2344 → 2394 | 124.7 |

`DFRAME` gaps during `spans` grow with the same cursor: **1.003 → 1.183 → 1.191 → 1.346 → 1.472 → 1.591 s**. Complete `app=2456271` times only the boundary append (realloc copies), not `channelForNoteId`.

`channelForNoteId` is O(events) per note, constant per 8-note slice. It cannot produce that notes/s drop. Leave it.

`idx` pass 0 has the same growth (952 → 328 events/s; `DFRAME` to 1.337 s). Same reserve defect on `tickEvents`. 5.7 is the whole gate, not `spans` alone.

No `loop_rem,idle_maint` during LCR. 5 s `idle_maint` max during `spans` is **41.6 ms** (under the 50 ms remainder bar).

---

## Contract

- `appendSpansFromNotes`: `spanBoundaries.capacity() >= 2 * notes.size()` after the first sliced call that sees `notes`.
- `appendTickEventEntries`: on a pass of `limit` events, a slice at `begin` reserves `out.size() + (limit - begin)` (remaining in this pass), not this slice only.
- Existing range equivalence tests stay green. `walk=0` stays.

---

## Pre-implementation review

### Ready

- Owner and call sites traced: `appendSpansFromNotes` → `appendSpanBoundaryEntries`; device `RebuildSpans`; `indexCapturePassEventRange` → `appendTickEventEntries`.
- `spans.reserve(notes.size())` is the pattern to extend.
- Native can assert `capacity()` after the first 8-row slice.

### Resolved (user / code)

| Topic | Decision |
|-------|----------|
| 5.7 leftover | `DFRAME` cadence stretch from sliced PSRAM realloc, not paint duration, not `reb` |
| `channelForNoteId` | Out of this slice (constant per 8 notes) |
| `tickEvents` | Same reserve defect; in this slice because 5.7 fails `idx` `DFRAME` too |
| B / A2 / `recon` / `pair` | Do not start |

### Open before coding

None — implementer pin-down only: native capacity tests.

### Proceed?

YES.

---

## 5.7a native shipped

`appendSpansFromNotes` reserves `spanBoundaries` to `2 * notes.size()`. `appendTickEventEntries` reserves remaining events in the current pass. `test_stage57_span_boundaries_reserve_final_size` / `test_stage57_tick_events_reserve_pass_remainder` PASS. Native **1185/1185**. `teensy41-capture-serial` SUCCESS.

---

## Device gate (after native)

Same 139-bar / 2394-note loop. PASS when:

- `spans` notes/s does not drop with cursor
- `idx` pass 0 events/s does not drop with cursor
- largest LCR-period `DFRAME` gap ≤ 1.0 s (healthy after-complete is 0.968 s)
- still no `idle_maint` `loop_rem`
- `walk=0`

Do not start 5.1 until this remasure.
