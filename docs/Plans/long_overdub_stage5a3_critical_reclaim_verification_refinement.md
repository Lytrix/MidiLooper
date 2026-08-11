# Stage 5a-3 — Critical reclaim verification

**Status:** 5a-2 shipped; awaiting verification capture  
**Parent:** [`long_overdub_stage5_memory_persistence_bugfix.md`](long_overdub_stage5_memory_persistence_bugfix.md)

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

## Preliminary `113519`

| Signal | Finding |
|--------|---------|
| Append failures | 4× (vs 41× in `021117`) |
| `PERS,diag` free chunks | 423 / 404 — not at reserve when sampled |
| Pool exhausted log | 0 |
| Prior telemetry | No pressure/reclaim correlation |

**Hypothesis status:** Unconfirmed — need `append,deny` reason codes from verification capture.

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
