# Loop content resolution — 5.7 DFRAME during sliced append (reserve)

**Status:** **5.7c FROZEN** 2026-08-15 — representation part of Stage 5.7 closed [`170024`](../captures/session_20260815_170024.log). Successor: [`loop_content_resolution_pair_index_refinement.md`](loop_content_resolution_pair_index_refinement.md) (`pair` `DFRAME` 1.277 s). Do not reopen 5.7c.  
**Change:** `openspec/changes/loop-content-resolution/` (DEC-037 Stage 9)  
**Parent:** 5.17 complete — [`loop_content_resolution_tick_index_flat_event_index_refinement.md`](loop_content_resolution_tick_index_flat_event_index_refinement.md)  
**Evidence:** [`162630`](../captures/session_20260815_162630.log)

**Does not start:** B, A2, `recon` / `pair` rewrites, 5.1 / 5.2, Stage 6, restoring the arm cap. **5.7c is frozen** — pair is a new investigation.

**Boundary:** `StateCheckpoints::appendSpansFromNotes` owns span-boundary growth. `TickIndex::appendTickEventEntries` owns tick-event growth. Same sliced-append reserve contract. Query contracts unchanged.

---

## Architecture checkpoint

1. **Ownership change?** NO — extend the two existing append functions.
2. **State transition change?** NO — idle `DeviceGateSession` phases unchanged.

No formal trigger. Lightweight preflight: owner = those appends; change = reserve known remaining/final size; tests = native capacity + existing range equivalence; device = 5.7 remasure.

---

## Goal

**Representation (closed 5.7c):** no per-entry PSRAM map on `spanBoundaries` / `tickEvents` / channel lookup; `walk=0`.

**Whole-gate `DFRAME` ≤ 1 s:** not this plan. Successor 5.18.

Not the goal: make `reb` smaller as the pass criterion. Not the goal: rewrite `pair` / `recon`.

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

---

## 5.7a device remasure — [`163942`](../captures/session_20260815_163942.log)

```
mat=0,win=14552,reb=4419525,st=3113,rep=350,hist=2394,walk=0,app=2349,sort=9142,iapp=202981,isort=26846
```

| | [`162630`](../captures/session_20260815_162630.log) before reserve | [`163942`](../captures/session_20260815_163942.log) after |
|--|--:|--:|
| LCR wall | 55.32 s | 45.81 s |
| `reb` | 6.875 s | 4.420 s |
| `app` | 2.456 s | **2.3 ms** |
| `iapp` | 4.816 s | **203 ms** |
| `isort` | 27.1 ms | 26.8 ms |
| `win` / `st` / `walk` | 14.0 ms / 3105 µs / 0 | 14.6 ms / 3113 µs / 0 |
| `idx` p0 wall | 9.09 s | **3.64 s** |
| `idx` p0 events/s | 952 → 328 | **1310 → 1212** (last 0.63 s bucket 977) |
| `spans` wall | 9.68 s | 6.42 s |
| `spans` notes/s | 565 → 125 | **930 → 169** (still drops) |
| `spans` `DFRAME` | 1.003 → 1.591 s | **0.992 → 1.195 s** (consecutive `frameIndex`) |
| `idx` p0 `DFRAME` | 0.981 → 1.337 s | **0.985 → 1.045 s** |
| `idle_maint` `loop_rem` | none | none |
| after complete `idle_maint` | 271–276 µs | 277 µs |
| after complete `DFRAME` | 0.968 s | 0.968 s |

**Reserve invariant PASS.** Boundary realloc is gone (`app=2349`). `idx` rate no longer falls with cursor.

**5.7 `DFRAME` bar not closed.** `spans` notes/s still falls and `DFRAME` still stretches to 1.195 s with consecutive `frameIndex` (+30). That remaining growth is in `appendSpansFromNotes` after the boundary append: `channelForNoteId` scans `resolved` from the start for each note, so later notes take longer. It is not constant per slice.

`pair` still has a 1.275 s `DFRAME` (different owner). The 2.890 s gap at 57.13 s skips `frameIndex` 1200 and 1230 — two missing log lines, not a measured 2.89 s slice. Paint duration stays 12–14 ms.

Do not start 5.1. Next 5.7 slice is `channelForNoteId` in `appendSpansFromNotes` only if asked.

---

## 5.7b — channel map from full `resolved`

**Ownership change?** NO. **Transition change?** NO.

`appendSpansFromNotes` fills `channelByNoteId` from **every** NOTE_ON in `resolved` (first wins), then each sliced note is an O(1) lookup. It does **not** scan only the current 8 notes or the current 8 events.

Open notes still evaluate:

| Path | Already in code | 5.7b test |
|------|-----------------|-----------|
| Reconstruct | `CanonicalSpanBuild::activeNoteStacks` persists across `appendCanonicalSpansFromMidi` slices; `finishCanonicalSpansFromMidi` closes leftover ons to `loopLength` | `test_stage57_recon_keeps_open_note_across_event_slice` — ON at index 7, OFF at 8; unpaired ON at 10 |
| Pair | `openOnByPitch` persists; `byNoteId.offIndex = -1` until the off slice | `test_stage57_pair_keeps_open_note_across_event_slice` |
| Span channel | Map built from all resolved NOTE_ONs, including ons whose off is later or missing | `test_stage57_span_channel_uses_full_resolved_not_note_slice` — crossed note channel 5, open note channel 9 |

Do not rewrite `pair` / `recon`. Native contract (full `resolved`, open notes) **held**. Device map fill **FAIL**.

---

## 5.7b device FAIL — [`164922`](../captures/session_20260815_164922.log)

```
mat=0,win=14503,reb=16305251,st=6725,rep=350,hist=2394,walk=0,app=1160,sort=10284,iapp=201606,isort=27138
```

| | [`163942`](../captures/session_20260815_163942.log) 5.7a | [`164922`](../captures/session_20260815_164922.log) 5.7b |
|--|--:|--:|
| `reb` | 4.420 s | **16.305 s** |
| `app` | 2.3 ms | 1.2 ms |
| `iapp` | 203 ms | 202 ms |
| `st` | 3113 µs | 6725 µs |
| `walk` / `hist` | 0 / 2394 | 0 / 2394 |
| `idx` p0 events/s | 1310 → 1212 | 1315 → 1215 |
| `spans` notes/s | 930 → 169 | **1000 → 1031 → 969** (flat after fill) |
| `spans` wall | 6.42 s | 2.37 s after fill |
| `dedup` → first `spans` | ~0 | **14.75 s** |
| `idle_maint` `loop_rem` | none | **14.744 s** at first `spans` slice |
| largest LCR `DFRAME` | 1.195 s | **15.702 s** (`frameIndex` +30, real stall) |
| after complete `DFRAME` | 0.968 s | 0.966–0.967 s |
| after complete `idle_maint` | 277 µs | 288–290 µs |

First `appendSpansFromNotes` (`begin==0`) calls `fillChannelByNoteId`: one PSRAM `unordered_map` `emplace` per NOTE_ON. Same class of stall as `startsByTick` / `byTick` before 5.15 / 5.17. After the map exists, span rate is flat ~1000 notes/s — the lookup is not the leftover.

Open-note contract is not the fail. Do not shrink the map to the current 8 events.

**Next:** 5.7c — C-order append of `{noteId, channel}` + one sort + first-wins unique. Not another PSRAM map. Not slicing `emplace`. Not 5.1.

---

## 5.7c — flat channel lookup

**Ownership change?** NO. **Transition change?** NO.

### Derived-index storage invariant (DEC-037, not a new DEC)

Formalized after 5.7c [`170024`](../captures/session_20260815_170024.log). Authority: [DEC-037](../DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype) amendment 2026-08-15, [`design.md`](../../openspec/changes/loop-content-resolution/design.md) decision 10, architecture hard invariant 8.

| Derived structure | Associative PSRAM | Flat A | [`170024`](../captures/session_20260815_170024.log) |
|-------------------|-------------------|--------|--------|
| `startsByTick` | `multimap` 224–413 ms / 8 | `spanBoundaries` | `app=1.4 ms` `sort=9.4 ms` |
| `TickIndex::byTick` | `multimap` 50–121 ms / batch | **removed 5.17e**; `tickEvents` | `iapp=201 ms` is **reserved bulk append total**, not a remaining map |
| channel lookup | `unordered_map` 14.7 s | `{noteId, channel}[]` | `capp=12.4 ms` `csort=1.8 ms` |

**5.7 state:** channel index PASS · span boundaries PASS · tick events PASS · resolver `walk=0` PASS · **`pair` OPEN** (`DFRAME` 1.277 s). `pair` / `recon` / `byNoteId` are not this representation experiment. Do not start 5.1. Do not add B.

### Contract (from code — pin before the swap)

`fillChannelByNoteId` / `channelByNoteId.find(note.noteId)` is **not** “find the canonical entry for `(NoteId, channel)`.”

```text
Input:
    resolved MIDI events; each NOTE_ON carries (noteId, channel)

Required operation:
    given NoteId, return the channel of the first NOTE_ON with that noteId
    in resolved C-order

Required multiplicity:
    first-wins unique on NoteId only
    (a later NOTE_ON with the same noteId is ignored, even if the channel differs)

Required ordering:
    none for query — point find, not a range scan
    build: C-order append of NOTE_ONs, stable_sort by noteId, unique keep-first
```

Channel is the **value**, not part of the key and not a partition. Sorting or uniquing by `(noteId, channel)` would keep two channels for one noteId and violate first-wins.

Open notes: every NOTE_ON in `resolved` is appended, including ons whose off is later or missing. The current 8-note span slice is not the input.

`channelForNoteId` linear scan stays on the non-checkpoint `resolveState` path.

### Representation

| Id | Representation | Build | Query |
|----|----------------|-------|-------|
| **C** | 5.7b `unordered_map<NoteId,uint8_t>` | one PSRAM `emplace` per NOTE_ON | `find` |
| **A** | `ChannelByNoteIdEntry[]` `{noteId, channel}` | C-order append + `stable_sort` by noteId + unique keep-first | `lower_bound` by noteId |

No `(noteId, channel, index)` row. The consumer only needs channel. No B / A2.

Device sequencing (first `spans` slice must not contain the index build):

```text
dedup → chan (8 events / slice) → csort → spans (8 notes / slice)
```

### Pre-implementation review

#### Ready

- Owner traced: `fillChannelByNoteId` / `appendSpansFromNotes` lookup / device RebuildSpans after `dedup`.
- 5.15 / 5.17 pattern: `appendSpanBoundaryEntries` + `appendTickEventEntries` (reserve remaining in the pass).
- Oracle: `channelForNoteId` (first NOTE_ON in C-order).

#### Resolved (user / code)

| Topic | Decision |
|-------|----------|
| Lookup key | `NoteId` only; channel is the value |
| Unique | first-wins on `NoteId` after `stable_sort` by `noteId` |
| Device shape | `chan` / `csort` before `spans`; not a one-shot inside the first span slice |
| `recon` / `pair` | Do not rewrite |
| Formal LCR invariant | After 5.7c device remasure, not this commit |

#### Open before coding

None — implementer pin-down: native first-wins + sliced append + reserve tests.

#### Proceed?

YES.

---

## 5.7c native shipped

`channelByNoteId` is `ChannelByNoteIdEntry[]`. `appendChannelByNoteIdEntries` reserves remaining events in `resolved`. `sortAndUniqueChannelByNoteIdEntries` `stable_sort`s by `noteId` then unique keep-first. Device RebuildSpans runs `chan` then `csort` before `spans`. Complete line adds `capp=` / `csort=`. Native **1191/1191**.

---

## 5.7c device PASS — [`170024`](../captures/session_20260815_170024.log)

```
mat=0,win=14564,reb=1438128,st=3107,rep=350,hist=2394,walk=0,app=1420,sort=9439,iapp=201044,isort=27183,capp=12373,csort=1779
```

Sequence: `dedup` → `chan` (11.55 ms later) → `csort` → `spans` (22.63 ms after `csort`).

| | [`164922`](../captures/session_20260815_164922.log) 5.7b | [`170024`](../captures/session_20260815_170024.log) 5.7c |
|--|--:|--:|
| LCR wall | 56.82 s | 45.07 s |
| `reb` | 16.305 s | **1.438 s** |
| `app` / `sort` | 1.2 ms / 10.3 ms | 1.4 ms / 9.4 ms |
| `iapp` / `isort` | 202 ms / 27.1 ms | 201 ms / 27.2 ms |
| channel build | **14.7 s** map `emplace` | **`capp=12.4 ms` `csort=1.8 ms`** |
| `dedup` → first `spans` | 14.75 s | **3.78 s** sliced `chan` (not one-shot) |
| `idle_maint` `loop_rem` | **14.744 s** first `spans` | **none** (LCR max `idle_maint` **38.7 ms**) |
| `spans` notes/s | 1000 → 1031 → 969 after fill | **1271 → 1251** (flat, no fill stall) |
| `chan` events/s | — | **1265 → 1292** (flat) |
| `idx` p0 events/s | 1315 → 1215 | 1315 → 1223 |
| `chan`/`spans` `DFRAME` | **15.702 s** | **0.933–0.965 s** (consecutive `frameIndex`) |
| after complete `DFRAME` | 0.966–0.967 s | 0.964–0.965 s |
| after complete `idle_maint` | 288–290 µs | 275–277 µs |

The first `spans` slice is no longer a multi-second prerequisite. `walk=0`. `hist=2394`.

**5.7 leftover (different owner):** `pair` `DFRAME` **1.277 s** at 28.46 s (`frameIndex` 390→420, consecutive). Closed by **5.18b** [`173842`](../captures/session_20260815_173842.log) (`bn=225`, pair `DFRAME` 0.980–1.026 s). **5.7c stays frozen.** 5.1 scored on the same capture. Do not rewrite `recon` here. Arm cap stays off.
