# Stage 5a-3 — Critical reclaim verification

**Status:** Partial — [`183525`](../../captures/session_20260811_183525.log) classifies append WARNs as `duplicate` (reclaim hypothesis **falsified** for that class); `pool_alloc` / Critical reclaim chain still needs a pressure capture  
**Parent:** [`long_overdub_stage5_memory_persistence_bugfix.md`](long_overdub_stage5_memory_persistence_bugfix.md)  
**Branch tip:** includes 5a-1 (`b5e479f`) + 5a-2/RC4e (`e0ea018`)

## Principle

Do **not** assume `Capture append failed` means chunk-pool exhaustion. Classify every failure via authoritative `CaptureAppendDenyReason` before evaluating Critical reclaim.

## 5a-2 CAP lines

### `CAP,append,deny`

```text
#CAP,<us>,append,deny,<reason>,<freeChunks>,<usedChunks>,<pressure>,<ch>,<note>,<tick>,<pendingPass>
```

Reasons: `phase_none`, `pending_pass`, `duplicate`, `pool_alloc`, `store_other`.

Authority: `Loop::appendCaptureEventWithResult()` — single owner for deny classification.

### `CAP,DIAG,reclaim`

```text
#CAP,<us>,DIAG,reclaim,<chunksFreeBefore>,<chunksFreeAfter>,<passesReclaimed>,<chunksReleased>,<durationUs>,<pressure>,<transport>
```

Emitted when resources released or Critical diagnostic visibility requested.

### Pressure latch

On first `notifyCaptureAppendFailed` per latch window:

```text
#CAP,<us>,DIAG,pressure,latch,Critical,<heapFree>,<chunksFree>,<queueDepth>
```

## Preliminary evidence (pre-verification)

### `113519` (pre–5a-2 CAP)

| Signal | Finding |
|--------|---------|
| Append failures | 4× (vs 41× in `021117`) |
| `PERS,diag` free chunks | 423 / 404 — not at reserve when sampled |
| Pool exhausted log | 0 |
| Prior telemetry | No pressure/reclaim correlation |

### Post–5a-2 telemetry smoke (not `021117`-comparable)

| Session | `append,deny` | `Capture append failed` | `DIAG,reclaim` |
|---------|---------------|-------------------------|----------------|
| [`174742`](../../captures/session_20260811_174742.log) | 0 | 0 | 3× (`Normal`; 2× `transport=1`) |
| [`180107`](../../captures/session_20260811_180107.log) | 0 | 0 | 1× (`Normal`, `transport=0`) |
| [`182949`](../../captures/session_20260811_182949.log) | 0 | 0 | 0 |

Reclaim CAP works; transport-time reclaim with resource release observed at **Normal** pressure (undo/stop/edit call sites). **Critical**-pressure reclaim chain and authoritative deny reasons are still unverified.

### `183525` (post–5a-2; wrap overdub)

| Signal | Finding |
|--------|---------|
| Append WARNs | **191×** `Capture append failed (duplicate)` — **0×** `pool_alloc` |
| First wrap vs first deny | wrap ≈ 885.5 s; first duplicate ≈ 885.9 s |
| CAP gap | `RING,overflow` ~632 s — mid-overdub `append,deny` CAP lost |
| Chunk pressure | Not implicated for these WARNs |

**Hypothesis status:** **Falsified** for `183525` failure class (`duplicate`, not pool). Open follow-up: overdub overlap OpenSpec / wrap candidate lookup (dedicated branch). Still need a separate capture if proving Critical `pool_alloc` → reclaim recovery.

## Verification recipe (`021117` shape)

Baseline failure session [`021117`](../../captures/session_20260811_021117.log):

- ~560 s wall time; long RECORD from ~34 s; overdub passes ~274 / ~539 / ~560 s
- Dense ch4 grid during overdub; `undo_entries` ≈ 20–22
- At failure window (~511–527 s): `loop_chunks=23–24`, heap free ≈ 196 KB, **41×** `Capture append failed`

**Manual procedure (capture-serial firmware):**

1. Upload `teensy41-capture-serial`; start `capture_session.py` on `/dev/cu.usbmodem154944801`.
2. Load / keep multi-track content so disabled passes and undo history can accumulate (target `undo_entries` ≥ 20 on the active track).
3. Long RECORD with dense MIDI, then PLAY + multiple OVERDUB passes (same density as `021117`).
4. Keep auto-follow rolling past bar 18 (RC4e co-check via `DISP` `wStart`).
5. Run until wall time ≥ ~8–10 min **or** until `loop_chunks` approaches reserve / Critical pressure appears — whichever comes first.
6. Stop capture; parse with checklist below.

## Parse checklist (one capture)

```bash
SESSION=captures/session_YYYYMMDD_HHMMSS.log

rg 'append,deny|Capture append failed|DIAG,reclaim|DIAG,pressure|chunk pool exhausted' "$SESSION"
rg 'DFRAME' "$SESSION" | awk -F, '$NF+0 > 25000'   # adjust field if needed
rg 'DISP,' "$SESSION" | head
rg '\[Memory\].*loop_chunks=' "$SESSION"
```

| Check | Pass condition |
|-------|----------------|
| Deny authority | Every failure has `#CAP,append,deny,<reason>,...` |
| Classify | Count by reason; only `pool_alloc` enters reclaim hypothesis |
| Critical reclaim | `#CAP,DIAG,reclaim,...,Critical,1` during transport if Critical hit |
| `pool_alloc` chain | deny → Critical reclaim → `chunksReleased>0` → later append OK |
| Sustained health | 0 sustained append failures under comparable load |
| Timing | No recurring reclaim-linked `DFRAME` stalls |
| RC4e (same capture) | `wStart` advances; notes intersect current window |

## 5a-3 acceptance

| Criterion | Verify |
|-----------|--------|
| Authoritative deny reasons | Every `append,deny` has `reason=` |
| `pool_alloc` only for real allocation failures | Cross-check freeChunks at deny time |
| Pool failure → reclaim → recovery | `pool_alloc` → `DIAG,reclaim` → subsequent append success |
| Non-pool failures distinct | Other reasons → separate backlog |
| 021117-comparable workload | 0 sustained append failures |
| No timing regression | No sustained `DFRAME` stalls from reclaim |

Closeout must state: chunk-pressure hypothesis **confirmed or falsified**.

## Failure classification

| Reason | Action |
|--------|--------|
| `pool_alloc` | Critical reclaim causal chain |
| `phase_none` | Capture phase lifecycle |
| `pending_pass` | Pending-pass admission |
| `duplicate` | Separate (not pressure) |
| `store_other` | Identify store failure |
