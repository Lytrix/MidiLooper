# Handoff — LoopContentResolution Stage 9 (5.2 3b restore native; device recapture)

**Date:** 2026-08-15  
**Kind:** handoff  
**Branch:** `feature/loop-content-resolution` (local; not pushed)  
**HEAD:** (this commit) — DEC-036 3b overdub-entry restore for 5.2 recapture.  
**OpenSpec:** [`openspec/changes/loop-content-resolution/`](../../openspec/changes/loop-content-resolution/)  
**Authority:** [DEC-037](../DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype)  
**Plan:** [`loop_event_sourced_resolution_architecture.md`](loop_event_sourced_resolution_architecture.md)  
**Prior chat:** 3b restore native after 5.2 FAIL [`175544`](../../captures/session_20260815_175544.log).

---

## Paste this to start the next chat

> Continue DEC-037 Stage 9 from [`docs/Plans/loop_content_resolution_stage9_handoff.md`](docs/Plans/loop_content_resolution_stage9_handoff.md).
>
> **Now:** 5.2 3b restore is native-shipped. Flash `teensy41-capture-serial` and re-score PLAYING overdub on the same 139-bar loop. Do not wire LCR onto overdub. Do not start Stage 6. Do not reopen 5.18, flatten `openOnByPitch`, rewrite `recon`, or add representation B.
>
> Read CURRENT_WORK + this handoff first. Score `ODUB,begin_capture` < 50 ms, no `VCACHE,stale_all` immediately before it, no `VCACHE,full` on entry. Compare to 3b [`045556`](captures/session_20260814_045556.log) **2214 µs**. Prior FAIL [`175544`](captures/session_20260815_175544.log) **108979 µs**.

---

## One-line status

Idle LoopContentResolution complete-path is realtime-clean. Pair is frozen. **5.2 3b restore native** (host 1195/1195). **Device recapture next** after flash. Prior FAIL [`175544`](../../captures/session_20260815_175544.log) `begin_capture` **108979 µs**. Production MIDI/display still use `materializeToEventVector` / 3b copy. Stage 6 stays blocked. Do not wire LCR onto overdub.

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
  5.2 overdub entry     native restore; device recapture (FAIL 175544 108979 µs)
Stage 6 production swap BLOCKED
```

---

## Do not

- Reopen 5.15 / 5.17 / 5.7c / 5.18
- Flatten `openOnByPitch` (LIFO; `op=2.3 ms`, `pk=1`)
- Rewrite `recon`
- Build representation B or A2
- Restore the 16-bar arm cap
- Delete `materializeToEventVector`
- Put resolution on overdub / MIDI / `handleMidiInput`
- Start Stage 6 without user approval after 5.2
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

## 5.2 FAIL — [`175544`](../../captures/session_20260815_175544.log)

**Task:** [`openspec/changes/loop-content-resolution/tasks.md`](../../openspec/changes/loop-content-resolution/tasks.md) item 5.2 stays open until device recapture.

Same 139-bar class: `DISP,0,PLAYING,106752` notes **2393**. First PLAYING→OVERDUB at 14.680 s (`GS,15,36,0`).

| Check | Bar | [`175544`](../../captures/session_20260815_175544.log) | Result |
|-------|-----|----------|--------|
| `ODUB,stage,begin_capture` | **< 50 ms** | first **108979 µs**; later 92070 / 123219 / 164929 µs | **FAIL** (3b [`045556`](../../captures/session_20260814_045556.log) **2214 µs**) |
| `VCACHE,full` on overdub entry | **none** | none between `manager_enter` and `manager_done` | pass on this bar |
| Overdub `clockrate` | ~47–48 | 47–48 after settle | pass on this bar |

Entry window (first overdub):

```
ST,Track,PLAYING,OVERDUBBING
VCACHE,stale_all,...,notes,2393,...,total,139,...,dirty,1
ODUB,stage,begin_capture,108979
ODUB,stage,complete,114104
ODUB,stage,manager_done,132389
```

Cause: `startOverdubbing` called `markDisplayCachesStale()` before `beginCapture`. `establishOverdubSourceView` ran `copyEffectiveCommittedEventsInRange` + `reconstructDisplayNotes` instead of copying `visualCache.notes`.

`VCACHE,full` at 55.1 / 56.9 / 59.0 s is after STOPPED, not on the entry path.

Do **not** wire `resolveWindow` onto overdub. No Stage 6.

### 3b restore (native shipped this session)

Restored from stash `cbfe0fe` (Layer D 3b WIP) onto this branch. Did **not** add unused `rebuildVisualCacheAfterPassToggle`.

| Site | Restored contract |
|------|-------------------|
| `Track::startOverdubbing` | Do **not** `markDisplayCachesStale` |
| `Loop::establishOverdubSourceView` | If `committedDisplayVisualCacheAuthoritative`: copy `visualCache.notes`; no reconstruct at entry |
| `Loop::copyEffectiveCommittedEventsInRange` | Fallback: `CommittedEventRange::inWindow` + `applyNoteEditPassSequence` (no full materialize) |
| `Loop::notifyCommittedContentChanged` | `markPassDerivedStale` only |
| `TrackUndo` Record/Overdub/NoteEditPassClosed | No `rebuildVisualCacheFromPasses` |

Host: `pio test -e native` **1195/1195**. Firmware `teensy41-capture-serial` SUCCESS, RAM1 free **6592**.

### Device recapture (next)

Flash this firmware. Same 139-bar PLAYING overdub as [`175544`](../../captures/session_20260815_175544.log). Score:

| Check | Bar |
|-------|-----|
| `ODUB,stage,begin_capture` | **< 50 ms** (3b [`045556`](../../captures/session_20260814_045556.log) **2214 µs**) |
| `VCACHE,stale_all` immediately before `begin_capture` | **none** |
| `VCACHE,full` between `manager_enter` and `manager_done` | **none** |
| Overdub `clockrate` | ~47–48 |

### Architecture checkpoint (5.2)

1. Ownership change? **NO** — still `establishOverdubSourceView` / 3b copy.
2. State transition change? **NO**.

If a fix would put LCR on the overdub path → **stop**, design session.

---

## After 5.2 PASS (not this capture)

```text
5.2 PASS
    ↓
Stage 9 complete (three device-latency bullets all scored)
    ↓
only then Stage 6 production-swap review (user approval)
    6.1 dirty overdub fallback → resolveWindow; keep 3b clean-cache copy
    6.2 idle visual slices
    6.3 long-loop playback gather
    6.4 short-loop / NOTE_EDIT hydrate last
    6.5 never delete materialize; never resolve from handleMidiInput
```

5.2 FAIL is recorded. 3b restore is native. Do not “fix” by wiring LCR onto overdub.

---

## Hybrid runtime (still holds)

| Path | Owner |
|------|--------|
| Production MIDI / display | `materializeToEventVector` / Layer D 3b visual-cache copy |
| LCR | one-shot extra walk in idle maintenance; `deviceGateFinished` after one complete |
| `DFRAME` | every 30th `DisplayManager::update` |

Linker: `linker/imxrt1062_t41_lcr.ld`. Last firmware RAM1 free **6592**.

---

## Key files

| Role | Path |
|------|------|
| Owner | `include/LoopContentResolution.h`, `src/LoopContentResolution.cpp` |
| Idle gate hook | `Track::processDeferredContentResolutionDeviceGate` in `src/Track/TrackDeferredMaintenance.cpp` |
| Tests | `test/test_loop_content_resolution/test_loop_content_resolution.cpp` (native **1195/1195** after 3b restore) |
| Overdub 3b | `establishOverdubSourceView` / visual-cache copy (DEC-036) |

---

## Local commits this arc (not pushed)

| Commit | What |
|--------|------|
| `f180cd5` | 5.18b firmware: last-wins flat `byNoteId` |
| `6bf3297` | 5.18b device PASS docs |
| `c8c47dd` | 5.18 closed + 5.1 PASS docs |
| (this commit) | Restore DEC-036 3b overdub entry for 5.2 recapture |

---

## OpenSpec remaining Stage 9

- [ ] 5.2 overdub entry — **FAIL** [`175544`](../../captures/session_20260815_175544.log); **3b restore native**; device recapture next
- [ ] 6.x production swap — **do not start**
