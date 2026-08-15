# Handoff — LoopContentResolution 6D.3 (repeated overdub commits)

**Date:** 2026-08-15  
**Kind:** handoff  
**Branch:** `feature/loop-content-resolution` (local; not pushed)  
**HEAD:** local `feature/loop-content-resolution` — 6D.4 publish landed. Not pushed.  
**OpenSpec:** [`openspec/changes/loop-content-resolution/`](../../openspec/changes/loop-content-resolution/)  
**Authority:** [DEC-037](../DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype)  
**Plan:** [`loop_event_sourced_resolution_architecture.md`](loop_event_sourced_resolution_architecture.md)  
**6D:** [`loop_content_resolution_incremental_commit_maintenance_refinement.md`](loop_content_resolution_incremental_commit_maintenance_refinement.md)

---

## Paste this to start the next chat

> Continue DEC-037 from [`docs/Plans/loop_content_resolution_incremental_commit_maintenance_refinement.md`](docs/Plans/loop_content_resolution_incremental_commit_maintenance_refinement.md).
>
> **Now:** **6D.4 restamp holds** on [`210508`](../../captures/session_20260815_210508.log) (same boot as [`205928`](../../captures/session_20260815_205928.log)). Undo miss → 3b `begin_capture` **120 µs**. Next two PLAYING publishes consume `6c` at **31128 µs** then **312636 µs**. Restamp is not the 6C cost. Do not start midi_gap / 6.3.
>
> Read CURRENT_WORK + the 6D plan first.

---

## One-line status

**6D.4 restamp holds** [`210508`](../../captures/session_20260815_210508.log) — undo → 3b **120 µs**; then `6c` **31128 µs** / **312636 µs**. Not all of LCR live.

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
  6C overdub source      consume-when-ready native; device recapture optional
  6D post-commit maint.  6D.3 repeated overdub PASS native (not live LCR)
Stage 6                  6C consume-when-ready; 6D.3 is evidence, not a firmware gate
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
- Implement A (re-arm STOPPED cold-build) or B (slice the 30–60 s full-history build during PLAYING)
- Implement 6D firmware before native scaling pass + PLAYING-admission amendment
- Investigate MIDI Input Gap > 50 ms ([`192334`](../../captures/session_20260815_192334.log) 135 / 119 / 138 ms) during 6D
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

### Device [`194015`](../../captures/session_20260815_194015.log) — consume not exercised

Idle LCR never reached `deviceGateComplete`. No `DIAG,lcr,mat=`. No `DIAG,lcr,6c`. First PLAYING overdub at 28.719 s while `phase,idx` / `phase,pair` still running; visual cache already `slice_clean` notes **2375** `dirty=0`, so 3b copy ran.

| Check | Bar | [`194015`](../../captures/session_20260815_194015.log) | Result |
|-------|-----|----------|--------|
| `DIAG,lcr,6c` on scored entry | present | none in the capture | **not scored** |
| `ODUB,stage,begin_capture` | **< 50 ms** | **3612 / 6464 / 8076 / 8618 µs** | hard gate holds (3b copy) |
| vs 3b **2214 µs** | 6C consume | not measured | recapture |
| `VCACHE,full` | none | none | holds |
| `PlaybackFullMaterialize` | 0 | **0** | holds |
| `DisplayFullRebuild` | no bump on entry | stays **4** | holds |

Recapture after [`194015`](../../captures/session_20260815_194015.log): stay **STOPPED** until `DIAG,lcr,mat=`, then PLAYING overdub. Look for `DIAG,lcr,6c`. Do not start midi_gap / 6.3.

### Device [`194643`](../../captures/session_20260815_194643.log) — same boot; CAP stages lost

Continuation of the [`194015`](../../captures/session_20260815_194015.log) boot. Idle LCR completed: `DIAG,lcr,mat=0,win=13656,reb=600907,st=385,rep=350,hist=2614,walk=0` then `DIAG,lcr,6a,win=2249,proj=8513,oracle=13264,tot=10762,ev=78,notes=40,match=1`.

PLAYING at 397.8 s. First overdub INFO 402.132 s (button 402.090 → opened **42 ms**) — prepared-ready consume scenario. `ODUB,stage,begin_capture` and `DIAG,lcr,6c` are absent; `RING,overflow` at 417.334 s (stop). Second overdub INFO 420.288 s (button 420.279 → opened **9 ms**) after 6B `stale_range` `dcnt` 10 and `slice_clean` notes **2648**.

| Check | Bar | [`194643`](../../captures/session_20260815_194643.log) | Result |
|-------|-----|----------|--------|
| `DIAG,lcr,mat=` | complete before overdub | `hist=2614` `walk=0` | holds |
| `DIAG,lcr,6a` | `match=1` | `match=1` | holds |
| `DIAG,lcr,6c` | present on scored entry | none (RING) | **not scored** |
| `ODUB,stage,begin_capture` | **< 50 ms** | none (RING) | INFO 42 ms / 9 ms |
| `VCACHE,full` | none | none | holds |
| `PlaybackFullMaterialize` | 0 | **0** | holds |
| Overdub `clockrate` | ~47–48 | 47–49 | holds |

Recapture: overdub and stop within ~1 s so entry CAP is not evicted. Score `begin_capture` against **2214 µs**. Do not start midi_gap / 6.3.

### Device [`205928`](../../captures/session_20260815_205928.log) — 6D.4 restamp PASS; 6C consume scored

STOPPED `DIAG,lcr,mat=0,...,hist=91,walk=0` then `6a` `match=0`. No later `lcr,phase`. PLAYING overdub stop 115.796 s (`stale_range` `dcnt=4`, `published`). Next overdub 117.269 s:

`DIAG,lcr,6c,win=23723,proj=13466,tot=37189,ev=363,notes=194` → `begin_capture,37747`.

| Check | Bar | [`205928`](../../captures/session_20260815_205928.log) | Result |
|-------|-----|----------|--------|
| `DIAG,lcr,mat=` before scored consume | complete | `hist=91` `walk=0` | holds |
| Second `deviceGateComplete` before next overdub | none | none | holds |
| `DIAG,lcr,6c` after PLAYING commit | present | `tot=37189` `ev=363` | **6D.4 PASS** |
| `ODUB,stage,begin_capture` | vs 3b **2214 µs** | **37747 µs** | 6C consume slower than 3b |
| 6B `stale_range` on PLAYING stop | present | `dcnt` 8 / 7 / 4 | holds |
| First post-`mat=` overdub `6c` | present | RING at stop | **not scored** |

`6c` `tot` is `tryResolvePreparedWindow` + `reconstructDisplayNotes`, not the 6D.2 `findRawWindow` 4 µs. Do not start midi_gap / 6.3.

### Device [`210508`](../../captures/session_20260815_210508.log) — same boot; third `6c` 312 ms

No `BOOT`, no `lcr,phase`. Two STOPPED undos (`kind=1`) at 340.053 / 341.339.

| Overdub | `6c` | `begin_capture` | Score |
|---------|------|-----------------|-------|
| 352.581 after undo | none | **120 µs** | 3b miss. Expected |
| 363.558 | `win=14482,proj=16019,tot=30501,ev=427` | **31128 µs** | restamp PASS |
| 374.884 | `win=298215,proj=13546,tot=311761,ev=289` | **312636 µs** | restamp hits; consume 312 ms |

`win=` is full two-source `resolveWindow`. 6B `stale_range` `dcnt=4` on both scored stops. Do not start midi_gap / 6.3.

---

## Why LCR is not ready before overdub (2026-08-15)

The 3b `visualCache.notes` copy is the path that is ready on a PLAYING overdub. Prepared LCR is not. Three independent firmware rules, all in `Track::processDeferredIdleMaintenance` / `maybeQueueContentResolutionDeviceGate` / `preparedWindowReady`:

1. **STOPPED only.** LCR slices run only when `!isPlaying() && !isRecording() && !isOverdubbing() && !isStoppedRecording()`. Visual-cache idle slices **do** run while PLAYING. After overdub stop the display can `slice_clean` during PLAYING; LCR cannot.

2. **Full rebuild is tens of seconds of STOPPED slices.** Uninterrupted: [`173842`](../../captures/session_20260815_173842.log) idx 20.7 s → `mat=` 52.4 s (**31.8 s**). [`185931`](../../captures/session_20260815_185931.log) 14.2 → 53.8 s (**39.6 s**). [`194015`](../../captures/session_20260815_194015.log) armed, then `reset,dirty` at undo 17.358 s, idx restarted, PLAYING at 26.8 s froze the gate; [`194643`](../../captures/session_20260815_194643.log) finished at 285.4 s.

3. **One-shot stamp (closed for overdub publish by 6D.4).** `deviceGateComplete` still does not re-run after the first finish. Overdub commit still increments `playbackRevision`. 6D.4 `publishPreparedOverdubPass` restamps without another gate. [`205928`](../../captures/session_20260815_205928.log) next overdub after a PLAYING commit emitted `6c`. [`194643`](../../captures/session_20260815_194643.log) second overdub (INFO **9 ms**) was before 6D.4.

Also: arm/run requires `!visualCacheDirty`. In-progress LCR is discarded on dirty (`DIAG,lcr,reset,dirty` in [`194015`](../../captures/session_20260815_194015.log)). Boot/save defer: `skip,restore` / `skip,save`.

**Always-ready before overdub is not 6C.** 6.0 forbids construct/sort/checkpoint/resolve on start/stop. Pick (2026-08-15): **C as 6D investigation**, not 6C firmware. **A rejected. B rejected.** Plan: [`loop_content_resolution_incremental_commit_maintenance_refinement.md`](loop_content_resolution_incremental_commit_maintenance_refinement.md).

| Option | Status |
|--------|--------|
| A. Re-arm after stamp mismatch (STOPPED only) | **Rejected** — still tens of seconds STOPPED |
| B. Slice the existing full-history LCR build while PLAYING | **Rejected** — another continuously maintained O(history) cache on the perform path |
| C / **6D**. Incremental index + affected checkpoint repair after commit | **6D.4 landed** — overdub-query delta + restamp. Device restamp PASS [`205928`](../../captures/session_20260815_205928.log). Not all of LCR live |

6C recapture (short overdub after `mat=`) still scores consume-when-ready only. It does not address always-ready.

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
| `8e63439` | Record 6B device PASS |
| `02bb187` | Park MIDI Input Gap as post-6C |
| `fb477f9` | 6C firmware: consume prepared LCR into overdubSourceView |
| `8534989` | Record why prepared LCR is not ready before PLAYING overdub |

---

## OpenSpec remaining

- [x] 5.2 overdub entry — **PASS** [`180624`](../../captures/session_20260815_180624.log) `begin_capture` 10050 µs
- [x] 6.0 consume-only invariant — **pinned**
- [x] 6A idle display range — **PASS** [`185931`](../../captures/session_20260815_185931.log) `match=1`
- [x] 6B commit invalidation — **PASS** [`192334`](../../captures/session_20260815_192334.log)
- [x] 6C overdub source — consume-when-ready. Device scored [`205928`](../../captures/session_20260815_205928.log) `6c` `begin_capture` **37747 µs** (slower than 3b **2214 µs**). Does not address always-ready.
- [x] **6D.4** incremental overdub publish — native PASS; device restamp **PASS** [`205928`](../../captures/session_20260815_205928.log) `6c` after PLAYING commit, no second gate. 6C consume **37747 µs** (slower than 3b **2214 µs**). Not all of LCR live.
- [ ] After 6C device score: MIDI Input Gap > 50 ms [`192334`](../../captures/session_20260815_192334.log) (135 / 119 / 138 ms, `clockrate` 47). Do not start during 6D. 6C consume is now scored in [`205928`](../../captures/session_20260815_205928.log); still do not start midi_gap from that capture.
