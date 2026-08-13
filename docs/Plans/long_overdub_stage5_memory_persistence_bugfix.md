# Long overdub Stage 5 — memory / persistence pressure

**Status:** 5a **closed** (5a-3 abandoned 2026-08-12); 5b backlog parked — 5a-1/5a-2 on `dev` (PR #29); 5a-3 `duplicate` closed via overdub OpenSpec; `pool_alloc` proof **not pursued**  
**Parent:** [`long_overdub_display_freeze_bugfix.md`](long_overdub_display_freeze_bugfix.md) §19  
**Branch:** merged via PR #29; overlap follow-up `feature/overdub-pass-overlap-resolution`

---

## Scope

| Track | Topic | Primary evidence |
|-------|--------|------------------|
| **5a** | Capture append / chunk pool | [`session_20260811_021117`](../../captures/session_20260811_021117.log), [`024505`](../../captures/session_20260811_024505.log) |
| **5b** | Boot deferred save + clear gating | [`session_20260811_110408`](../../captures/session_20260811_110408.log), plan §004608 |

Display RC slice (LEN, overview, RC4d) is closed — [`session_20260811_111528`](../../captures/session_20260811_111528.log).

---

## 5a — Capture / storage integrity

### Evidence (`021117` @ ~511 s)

- **41×** `Capture append failed (chunk pool or memory pressure)` on ch4 overdub grid notes.
- Same window: `heap free=196 KB` — **not** internal-heap exhaustion.
- `loop_chunks=23–24` in periodic memory log — chunk pool at **`CHUNK_RESERVE`** admission gate (`16`).
- Phase 1B derived-cache reclaim does **not** free pass chunk refs.

### Root cause (5a-1)

[`memory_pressure_reclaim_refinement.md`](memory_pressure_reclaim_refinement.md) policy table: **Critical** includes
`reclaimUnreferencedDisabledPasses()`. In [`main.cpp`](../../src/main.cpp) that reclaim runs only when
`!timingCriticalTrackActive` (idle transport). During RECORDING/PLAYING/OVERDUBBING, disabled-pass
chunk reclaim never runs → sustained append failures under multi-track load.

### 5a-1 fix (shipped)

After `tryReclaimDerivedViewCachesUnderPressure`, when `pressure >= Critical`, call
`trackManager.reclaimUnreferencedDisabledPasses()` even during timing-critical transport.

### 5a-2 — Authoritative append-deny + reclaim CAP (shipped)

- `Loop::appendCaptureEventWithResult()` — single deny authority (`phase_none`, `pending_pass`, `duplicate`, `pool_alloc`, `store_other`)
- `#CAP,append,deny` on rejection; `#CAP,DIAG,reclaim` on Critical visibility / resource release; pressure latch on first append fail
- Plan: [`long_overdub_stage5a3_critical_reclaim_verification_refinement.md`](long_overdub_stage5a3_critical_reclaim_verification_refinement.md)

### 5a-3 — Verification (partial)

| Class | Status |
|-------|--------|
| `duplicate` (`183525`) | **Closed** — not reclaim; OpenSpec G2 + device [`010000`](../../captures/session_20260812_010000.log); [`wrap_duplicate` plan](long_overdub_wrap_duplicate_display_freeze_bugfix.md) **FROZEN** |
| `pool_alloc` / Critical reclaim | **Abandoned** — verification not pursued; see [`stage5a3` plan](long_overdub_stage5a3_critical_reclaim_verification_refinement.md) |

### RC4e — Rolling window overdub (shipped, verify pending)

Bounded `rebuildVisualCacheIdleSlice` during overdub — [`long_overdub_rolling_window_overdub_bugfix.md`](long_overdub_rolling_window_overdub_bugfix.md)

### 5a backlog (not this commit)

- Canonicalization at overdub stop (`non-canonical storage` captures)
- Stronger persist admission under chunk pressure
- Optional undo trim interaction audit

---

## 5b — Boot deferred save + clear gating

### Evidence (`111528`)

- `PERS,request,…,sync_drain_already_pending` during overdub-stop save drain (completes).
- No `Clear aborted` in recent Aug 11 captures.
- Boot track-cycling blank piano: expected until `LoadLoopJob` commit (documented in overview plan).

### Backlog

- Clear without full workspace sync drain when only selected slot mutates
- `shouldDeferHeavyDisplayRebuild` coupling to full `hasPersistenceWorkPending()`

### 5b observability — save/load buildup pointers

**Firmware** (`teensy41-capture-serial`):

| Line | When | Key fields |
|------|------|------------|
| `#CAP,<us>,PERS,backlog,...` | Every ~5s during capture (~10s idle) while save pending or transport active | `workQ`, `writingItems`, `chunkQ`, `dirtyAgeMs`, `estSliceSteps`, `estSdBytes`, `savePending`, `urgent`, `transportBlk`, `budgetBlk`, `heapBlk` |
| `#CAP,<us>,PERS,diag,...` | Same interval | Chunk pool + block counters (`transportBlk`, `budgetBlk`, `heapBlk`, `sliceDone`) — **chunkQ only**, not workspace work queue |
| `#CAP,<us>,PERS,request,...` | Save admission | `queued`, `sync_drain`, `sync_drain_already_pending`, `already_pending` |
| `#CAP,<us>,PERS,work,...` | Work-item slice progress | `LoopPersist`, `slot:t:s`, `start` / `complete` |
| `#CAP,<us>,PERS,drain,...` | Sync drain failure (clear path) | `stuck`, `budget`, `flush_pending`, `failed` + queue depths + est drain size |
| `#CAP,<us>,SAVE,<phase>,rotate` | Save spinner FSM transitions | `pending`, `in_progress`, `completed`, `idle` |
| `#CAP,<us>,DIAG,pressure,...` | Memory pressure FSM | Last field is **mid-pass chunk queue** only |

**Host parse:**

```bash
python scripts/parse_persistence_buildup.py captures/session_*.log
rg 'PERS,backlog|PERS,drain|Clear aborted|Persistence drain stuck' captures/session.log
```

**Hypothesis — save more often:** compare `est_slice_steps` / `est_sd_bytes` peaks and `dirty_age_ms` growth between sessions. If cooperative slices shrink peak `workQ` but `transportBlk` stays high during RECORD, transport gating is still the limiter. If peaks drop after a policy change, more frequent idle saves help.

**Evidence gap (`014136`):** pre-backlog captures only had `PERS,diag` chunk-queue depth; the ~3359-step / ~117KB stuck drain was Serial-only. Re-run long RECORD with capture-serial after flashing backlog telemetry.

---

## Acceptance

- [x] **5a:** 5a-1/5a-2 shipped; 5a-3 `pool_alloc` proof abandoned (not `021117`-verified)
- [ ] **5b:** Clear selected slot within bounded time after boot (or documented policy)
- [ ] No sustained `save_state fail` through normal record/stop/play
- [ ] `pio test -e native`

---

## Implementation order

1. **5a-1** — Critical pass reclaim during transport (`main.cpp`)
2. **5a-2** — Authoritative append-deny + reclaim CAP (**shipped**)
3. **5a-3** — Verification capture + hypothesis confirm/falsify
4. **5b-1** — Clear / sync-drain policy (design session if scope expands)
