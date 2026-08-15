# Handoff — LoopContentResolution Stage 6B

**Date:** 2026-08-15  
**Kind:** handoff  
**Branch:** `feature/loop-content-resolution` (local; not pushed)  
**HEAD:** 6B firmware (native shipped; device gate open)  
**OpenSpec:** [`openspec/changes/loop-content-resolution/`](../../openspec/changes/loop-content-resolution/)  
**Authority:** [DEC-037](../DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype)  
**Plan:** [`loop_event_sourced_resolution_architecture.md`](loop_event_sourced_resolution_architecture.md)

---

## Paste this to start the next chat

> Continue DEC-037 from [`docs/Plans/loop_content_resolution_stage9_handoff.md`](docs/Plans/loop_content_resolution_stage9_handoff.md).
>
> **Now:** 6B native shipped; device gate open. Keep the 3b copy. Do not start 6C. Do not reopen 5.18, flatten `openOnByPitch`, rewrite `recon`, or add representation B.
>
> Read CURRENT_WORK + this handoff first.

---

## One-line status

**6B native shipped** (device gate open). **6A PASS** [`185931`](../../captures/session_20260815_185931.log) `match=1`. Overdub stays 3b copy. Do not start 6C.

---

## DEC-037 scoreboard

```text
correctness             PASS  (native vs materialize+reconstruct)
complexity              PASS  (walk=0)
derived-index RAM       PASS
flat/bulk construction  PASS
device latency
  spanBoundaries        PASS  5.15
  tickEvents            PASS  5.17
  channel lookup        PASS  5.7c
  pair                  PASS  5.18
  5.1 idle complete     PASS  173842
  5.2 overdub entry     PASS  180624  10050 µs
  6A idle display       PASS  185931  match=1 win=784 proj=5539 oracle=9192
  6B commit invalidation native shipped; device gate open
Stage 6                  6B device next; 6C not started
```

---

## Do not

- Reopen 5.15 / 5.17 / 5.7c / 5.18
- Flatten `openOnByPitch` (LIFO; `op=2.3 ms`, `pk=1`)
- Rewrite `recon`
- Build representation B or A2
- Restore the 16-bar arm cap
- Delete `materializeToEventVector`
- Put resolution on `handleMidiInput` or `startOverdubbing` / `stopOverdubbing` (including `ensure*` LCR rebuild helpers)
- Start Stage 6 firmware without an explicit implement request (first slice is **6A**, not overdub)
- Start **6C** before 6B device PASS
- Remove the 3b `visualCache.notes` copy
- Rename `byNoteId` or “clean up” pairing
- Put probes in `ExternalMemoryFirstAllocator` (ITCM / RAM1 overflow)
- Commit `lib/SSD1322_OLED` submodule dirt
- Push unless asked

---

## Frozen representations (do not reopen)

| Structure | Contract | Representation | Evidence |
|-----------|----------|----------------|----------|
| `spanBoundaries` | tick range → start/end apply | flat append + `stable_sort` by tick | 5.15 [`151450`](../../captures/session_20260815_151450.log) |
| `tickEvents` | tick window → Active `(passId, eventIndex)` | flat/bulk; `byTick` **removed** | 5.17 [`162630`](../../captures/session_20260815_162630.log) |
| `channelByNoteId` | `NoteId` → first NOTE_ON channel (stored byte, not a key) | flat + unique **keep-first** | 5.7c [`170024`](../../captures/session_20260815_170024.log) `capp=12.4 ms` |
| `byNoteId` | `NoteId` → last `{passId, on, off}` | flat + unique **keep-last** | 5.18 [`173842`](../../captures/session_20260815_173842.log) `bn=225` `nsort=10.0 ms` |
| `openOnByPitch` | per-pass pitch → open ON indexes | **retain LIFO** `std::map<uint8_t, vector>` (not PSRAM) | 5.18a [`172927`](../../captures/session_20260815_172927.log) `op=2.4 ms` `pk=1` |
| `passById` | `PassId` → slot | `unordered_map` (pass-count) | out of swap list |

**Invariant (DEC-037 amendment, not a new DEC):** derived indexes + PSRAM + per-entry construction. Flatten from the **query**, not the container type. B only if a measured flat query is too expensive. LIFO pairing is why this is not “replace all maps with arrays.”

Loop-internal channel is not a resolution key (DEC-033). Pairing stays pitch-only.

---

## 5.18 closed (do not continue optimizing pair)

The 1.27 s pair `DFRAME` was PSRAM `unordered_map` assignment into `byNoteId`, not the pairing algorithm.

| | [`172927`](../../captures/session_20260815_172927.log) map | [`173842`](../../captures/session_20260815_173842.log) flat |
|--|--:|--:|
| `bn` | **5.346 s** | **225 µs** |
| pair `tot` | 5.357 s | 7.95 ms |
| `nsort` | — | **10.0 ms** |
| `op` / `pk` | 2.4 ms / 1 | 2.3 ms / 1 |
| pair `DFRAME` | 1.273 s | 0.980–1.026 s |

Device `nsort` is one slice after all pair ranges, before `isort`. Sliced pair does **not** unique every 8 events. Unique is keep-last (opposite of 5.7c keep-first).

Plan: [`loop_content_resolution_pair_index_refinement.md`](loop_content_resolution_pair_index_refinement.md) — **FROZEN**.

---

## 5.1 PASS — idle complete path only

Capture: [`173842`](../../captures/session_20260815_173842.log)

LCR window 20.67–52.43 s (`phase,idx` → `DIAG,lcr,mat=`). `hist=2394` `walk=0`. 139 bars (larger than `035414`).

| Check | Worst |
|-------|-------|
| OLED | consecutive `DFRAME` **1.034 s** (`frameIndex` +30, paint 9.9 ms) vs healthy **0.968 s** |
| MIDI | `midi_gap` **39.1 ms**; `idle_maint` **34.9 ms**; no `loop_rem` |
| `VCACHE,full` during LCR | none |

`processDeferredContentResolutionDeviceGate` runs only when transport is **not** PLAYING / RECORDING / OVERDUBBING / STOPPED_RECORDING. 5.1 cannot stall PLAYING MIDI because the gate does not run then. `clockrate` was 0 during 173842 LCR (STOPPED). That is expected.

Complete line:

```
mat=0,win=7129,reb=368240,st=531,rep=350,hist=2394,walk=0,app=1341,sort=9237,iapp=197743,isort=28116,capp=10682,csort=1897
pair,tot=7950,bn=225,op=2256,lk=546,oth=4923,ent=2396,ins=2396,ow=0,pu=2396,po=2395,pk=1,oa=58,hb=116,nsort=10003
```

Healthy after-complete `DFRAME` cadence is **~0.968 s**. Consecutive `frameIndex` (+30) is a real stretch. Do not treat a 31 s wall-clock gate as a stall — it is cooperative idle slices.

---

## 5.2 PASS — [`180624`](../../captures/session_20260815_180624.log)

**Task:** [`openspec/changes/loop-content-resolution/tasks.md`](../../openspec/changes/loop-content-resolution/tasks.md) item 5.2 **checked**.

Same 139-bar class: `DISP,0,PLAYING,106752` notes **2388**. Scored PLAYING→OVERDUB at 32.312 s after `slice_clean` notes=2388 dirty=0.

| Check | Bar | [`180624`](../../captures/session_20260815_180624.log) | Result |
|-------|-----|----------|--------|
| `ODUB,stage,begin_capture` | **< 50 ms** | **10050 µs** | **PASS** (prior FAIL [`175544`](../../captures/session_20260815_175544.log) **108979 µs**; 3b [`045556`](../../captures/session_20260814_045556.log) **2214 µs**) |
| `VCACHE,stale_all` immediately before `begin_capture` | **none** | none on the scored window | **PASS** |
| `VCACHE,full` on overdub entry | **none** | none in the capture | **PASS** |
| Overdub `clockrate` | ~47–48 | 47 during OVERDUB; 48 later PLAYING | **PASS** |

Scored entry window:

```
ST,Track,PLAYING,OVERDUBBING
ODUB,stage,set_state,7
ODUB,stage,begin_capture,10050
ODUB,stage,undo_session,1
ODUB,stage,complete,10134
ODUB,stage,manager_done,30869
```

INFO wall-clock for that press: button 32.312 → `Overdub session opened` 32.322 (10 ms). A later PLAYING overdub at 56.259–56.271 (12 ms INFO) has no `ODUB,stage` CAP lines — `RING,overflow` at the previous stop dropped USB CAP. First overdub at 17.942–17.950 (8 ms INFO) also has no CAP stages.

`VCACHE,stale_all` on this capture is boot/load and overdub **stop** (`adopt_partial`), not entry.

### Prior FAIL [`175544`](../../captures/session_20260815_175544.log)

`startOverdubbing` called `markDisplayCachesStale()` before `beginCapture`. `establishOverdubSourceView` reconstructed instead of copying `visualCache.notes`. Restored in `0978ffe`.

### Architecture checkpoint (5.2)

1. Ownership change? **NO** — still `establishOverdubSourceView` / 3b copy.
2. State transition change? **NO**.

If a fix would put LCR on the overdub path → **stop**, design session.

---

## After 5.2 PASS — Stage 6 experiment

LCR is the **producer of prepared derived state**, not a replacement for `overdubSourceView`.

```text
WRONG:  OVERDUB → LCR construction → consume
RIGHT:  IDLE → LCR construction → READY
                         ↓
        OVERDUB ──────→ consume
```

```text
6A  idle display range: resolveWindow → projection
    (CommittedEventRange + reconstruct stays oracle)
6B  stop: commit → mark affected ranges → return
    (idle prepares; no VCACHE,full)
6C  prepared LCR range → overdubSourceView
    (3b visual-cache copy stays fallback)
    score begin_capture vs 2214 µs, not 10050 µs
```

`< 3 ms` is a **regression target**. `< 50 ms` is the hard gate. Firmware waits for an explicit implement request; start with **6A**.

---

## Hybrid runtime (still holds)

| Path | Owner |
|------|--------|
| Production MIDI / display | `materializeToEventVector` / Layer D 3b visual-cache copy |
| LCR | one-shot extra walk in idle maintenance; `deviceGateFinished` after one complete |
| `DFRAME` | every 30th `DisplayManager::update` |

Linker: `linker/imxrt1062_t41_lcr.ld`. Last firmware RAM1 free **6560** (6B helpers are `LOOP_COLD_MEM`; ITCM helpers overflowed one FlexRAM bank).

---

## 6B native (device gate open)

Overdub stop commits, marks only affected display bars, returns. No materialize, no whole-loop reconstruct, no LCR construct/sort/checkpoint/resolve on stop.

| Piece | Owner |
|-------|--------|
| Dirty only affected bars | `Loop::markAffectedDisplayCacheRanges` — this pass’s chunks via `appendChunkRefEvent`; companion Hide/Shorten spans; `VCACHE,stale_range` |
| Wire at commit | `Track::finalizeCommitSideEffects` — seal companions, then mark ranges on overdub stop; record stop still `markDisplayCachesStale` |
| Keep loop-wide cache | `refreshViewportAfterOverdubStop` skips `adopt_partial` when `visualCache.notes` is nonempty |

Native: `test_mark_affected_display_cache_ranges_dirties_sparse_bars` (8-bar loop; overdub in bar 3 dirties a neighborhood, not all 8; Hide of bar 0 does not dirty the last bar). `pio test -e native` **1198/1198**.

**Device score vs [`185931`](../../captures/session_20260815_185931.log):**

- no `VCACHE,stale_all` on long-loop overdub stop
- no `adopt_partial` shrinking notes (185931: 2403 → 496, 117 bars dirty)
- `VCACHE,stale_range` with `dcnt` ≪ 139
- no `VCACHE,full`
- stop bounded; idle sliced

Do not start 6C until this device gate PASSes.

---

## Key files

| Role | Path |
|------|------|
| Owner | `include/LoopContentResolution.h`, `src/LoopContentResolution.cpp` |
| Idle gate hook | `Track::processDeferredContentResolutionDeviceGate` in `src/Track/TrackDeferredMaintenance.cpp` |
| 6B invalidation | `Loop::markAffectedDisplayCacheRanges` in `src/Loop/LoopVisualCache.cpp`; `Track::finalizeCommitSideEffects`; `DisplayManager::refreshViewportAfterOverdubStop` |
| Tests | `test/test_loop_content_resolution/test_loop_content_resolution.cpp`; `test/test_effective_event_store` 6B oracle (native **1198/1198**) |
| Overdub 3b | `establishOverdubSourceView` / visual-cache copy (DEC-036) |

---

## Local commits this arc (not pushed)

| Commit | What |
|--------|------|
| `f180cd5` | 5.18b firmware: last-wins flat `byNoteId` |
| `6bf3297` | 5.18b device PASS docs |
| `c8c47dd` | 5.18 closed + 5.1 PASS docs |
| `0978ffe` | Restore DEC-036 3b overdub entry for 5.2 recapture |
| `4b82ad3` | Record 5.2 PASS (`begin_capture` 10050 µs) |
| `9e075c4` | Pin Stage 6 consume-only invariant |
| `c3570b5` | 6A firmware: keep TickIndex; idle consume resolveWindow |
| `27d94bb` | Record 6A device PASS |

---

## OpenSpec remaining

- [x] 5.2 overdub entry — **PASS** [`180624`](../../captures/session_20260815_180624.log) `begin_capture` 10050 µs
- [x] 6.0 consume-only invariant — **pinned**
- [x] 6A idle display range — **PASS** [`185931`](../../captures/session_20260815_185931.log) `match=1`
- [ ] 6B commit invalidation — native shipped; device gate open
- [ ] 6C overdub source (3b copy stays)
