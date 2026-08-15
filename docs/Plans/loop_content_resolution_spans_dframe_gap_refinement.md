# Loop content resolution — 5.7 DFRAME during sliced append (reserve)

**Status:** **5.7b native PASS** 2026-08-15 — device remasure owed. 5.7a reserve PASS [`163942`](../captures/session_20260815_163942.log).  
**Change:** `openspec/changes/loop-content-resolution/` (DEC-037 Stage 9)  
**Parent:** 5.17 complete — [`loop_content_resolution_tick_index_flat_event_index_refinement.md`](loop_content_resolution_tick_index_flat_event_index_refinement.md)  
**Evidence:** [`162630`](../captures/session_20260815_162630.log)

**Does not start:** B, A2, `recon` / `pair` rewrites, 5.1 / 5.2, Stage 6, restoring the arm cap.

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

Do not rewrite `pair` / `recon`. Device remasure owed.
