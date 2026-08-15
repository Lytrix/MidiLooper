# Handoff — LoopContentResolution Stage 6C (native; device next)

**Date:** 2026-08-15  
**Kind:** handoff  
**Branch:** `feature/loop-content-resolution` (local; not pushed)  
**HEAD:** 6C native — consume prepared LCR into `overdubSourceView` (device score next)  
**OpenSpec:** [`openspec/changes/loop-content-resolution/`](../../openspec/changes/loop-content-resolution/)  
**Authority:** [DEC-037](../DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype)  
**Plan:** [`loop_event_sourced_resolution_architecture.md`](loop_event_sourced_resolution_architecture.md)

---

## Paste this to start the next chat

> Continue DEC-037 from [`docs/Plans/loop_content_resolution_stage9_handoff.md`](docs/Plans/loop_content_resolution_stage9_handoff.md).
>
> **Now:** 6C native landed. Flash and score PLAYING overdub `begin_capture` against **2214 µs** (look for `DIAG,lcr,6c`). Keep the 3b copy. Do not reopen 5.18, flatten `openOnByPitch`, rewrite `recon`, or add representation B. Do not start midi_gap / 6.3 until 6C device is scored.
>
> Read CURRENT_WORK + this handoff first.

---

## One-line status

**6C native** — `establishOverdubSourceView` consumes prepared LCR; 3b copy fallback. Device score next vs **2214 µs**. **6B PASS** [`192334`](../../captures/session_20260815_192334.log).

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
  6B commit invalidation PASS  192334  stale_range dcnt 15/5/5 notes kept
  6C overdub source      native  tryResolvePreparedWindow → overdubSourceView
Stage 6                  6C device next; 3b copy stays fallback
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
- Investigate MIDI Input Gap > 50 ms ([`192334`](../../captures/session_20260815_192334.log) 135 / 119 / 138 ms) before 6C device is scored
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

Linker: `linker/imxrt1062_t41_lcr.ld`. Last firmware RAM1 free **6560** (6C consume is `LOOP_COLD_MEM` in `establishOverdubSourceView`; same as 6B).

---

## 6B PASS — [`192334`](../../captures/session_20260815_192334.log)

Overdub stop commits, marks only affected display bars, returns. No materialize, no whole-loop reconstruct, no LCR construct/sort/checkpoint/resolve on stop.

Three PLAYING 139-bar overdub stops:

| Stop | `stale_range` notes | `dcnt` | `ODUB,stop,display` | idle `slice_clean` |
|------|--------------------:|-------:|--------------------:|--------------------|
| 52.028 s | 2375 | 15 | 35 ms | 52.489 s notes **2437** |
| 59.182 s | 2437 | 5 | 30 ms | 59.389 s notes **2450** |
| 67.831 s | 2450 | 5 | 26 ms | 67.989 s notes **2464** |

- no `adopt_partial` in the capture
- no `VCACHE,full`
- 139-bar `stale_all` only at boot (`notes=0`)
- `DisplayFullRebuild` stays **5** across all three stops
- `PlaybackFullMaterialize` **0**
- Contrast [`185931`](../../captures/session_20260815_185931.log): `adopt_partial` 2403 → 496 notes, 117 bars dirty

`RING,overflow` at each stop is USB CAP drop during flush (same class as [`180624`](../../captures/session_20260815_180624.log)).

**After 6C (not now):** MIDI Input Gap > 50 ms in this capture — `DIAG,midi_gap` **135 / 119 / 138 ms** at 54.7 / 64.7 / 69.8 s while `clockrate` stayed **47**. 6B clock gate still holds (no half-tempo while PLAYING). Do not fold into 6C. Do not treat as RC-J.

Do not start midi_gap / 6.3 until 6C device is scored. Score 6C `begin_capture` against 3b **2214 µs**, not 5.2 **10050 µs** / [`192334`](../../captures/session_20260815_192334.log) **10339 µs**. Look for `DIAG,lcr,6c`.

---

## 6C native — prepared LCR → `overdubSourceView`

Owner: `Loop::establishOverdubSourceView` in [`src/Loop/LoopCapture.cpp`](../../src/Loop/LoopCapture.cpp).

Order:

1. `tryResolvePreparedWindow` for the overdub source window (same 16-bar window as the dirty fallback). Miss or stamp mismatch returns false and never rebuilds.
2. On hit: fill `overdubSourceViewEvents_` from the prepared window and reconstruct `overdubSourceViewNotes_` (`NoteUtils::reconstructDisplayNotes` — not LCR construct). Emit `DIAG,lcr,6c`.
3. Else 3b copy of authoritative `visualCache.notes`.
4. Else `copyEffectiveCommittedEventsInRange`.

Native (`test_overdub_source_view`): dirty cache + prepared stamp fills notes and events; stamp mismatch does not consume stale LCR (events from the windowed walk, notes empty). 3b path unchanged when LCR is not ready. `pio test -e native` **1200/1200**.

Device still owed: same 139-bar PLAYING overdub class. `begin_capture` vs **2214 µs**. `DIAG,lcr,6c` on the scored entry when idle LCR completed before the press. No `VCACHE,full`. `clockrate` 47–48. No LCR construct on entry.

---

## Key files

| Role | Path |
|------|------|
| Owner | `include/LoopContentResolution.h`, `src/LoopContentResolution.cpp` |
| Idle gate hook | `Track::processDeferredContentResolutionDeviceGate` in `src/Track/TrackDeferredMaintenance.cpp` |
| 6B invalidation | `Loop::markAffectedDisplayCacheRanges` in `src/Loop/LoopVisualCache.cpp`; `Track::finalizeCommitSideEffects`; `DisplayManager::refreshViewportAfterOverdubStop` |
| Tests | `test/test_overdub_source_view`; `test/test_loop_content_resolution`; `test/test_effective_event_store` (native **1200/1200**) |
| Overdub 3b fallback | `establishOverdubSourceView` visual-cache copy when LCR is not ready |
| 6C consume | `Loop::establishOverdubSourceView` → `tryResolvePreparedWindow` |

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
| `b87dce1` | 6B firmware: mark affected display bars only |

---

## OpenSpec remaining

- [x] 5.2 overdub entry — **PASS** [`180624`](../../captures/session_20260815_180624.log) `begin_capture` 10050 µs
- [x] 6.0 consume-only invariant — **pinned**
- [x] 6A idle display range — **PASS** [`185931`](../../captures/session_20260815_185931.log) `match=1`
- [x] 6B commit invalidation — **PASS** [`192334`](../../captures/session_20260815_192334.log)
- [ ] 6C overdub source — **native landed**; device score next vs 3b **2214 µs** (`DIAG,lcr,6c`)
- [ ] After 6C: MIDI Input Gap > 50 ms [`192334`](../../captures/session_20260815_192334.log) (135 / 119 / 138 ms, `clockrate` 47)
