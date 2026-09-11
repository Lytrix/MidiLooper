# Deferred persist loopId track routing bugfix

**Status:** **FROZEN / SHIPPED** (2026-09-11) — RC1–RC4 complete  
**Kind:** bugfix (persistence / LSV1 verification blocker)  
**Evidence:** repro [`190525`](../../captures/session_20260911_190525.log); fix verify [`192637`](../../captures/session_20260911_192637.log), SD `loop_04_00.bin` (12 261 B, LSV1 offset 10)

## Symptom

Manual session on **track 5 / slot 0** (boot `focus track=4`, overdub `track=4`):

- Serial shows `LoopPersist,loop:0,done,ok` (twice).
- SD **`loop_00_00.bin`** updated (mtime 19:06) — **78-byte legacy empty slot**.
- SD **`loop_04_00.bin`** unchanged (mtime 15:07) — still 78-byte empty.
- OpenSpec **5.4** LSV1 byte probe cannot pass until the correct slot file is written.

## Root cause (RC1)

**`LoopId` is per-track slot index, not globally unique.**

`LoopPool::assignStableIds1To1` sets `loops_[i].loopId = i` on **each** track. Every track’s slot 0 has `loopId == 0`.

Hot paths call `StorageManager::admitLoopPersist(track.loopIdForSlot(slotIndex))`, which queues `persistKeyForLoop(loopId)`.

`resolveTrackSlotForLoopId` scans tracks and returns the **first** `(track, slot)` with matching `loopId`. For `loopId == 0` that is always **track 0, slot 0**. A fallback also forces `trackIndexOut = 0`, `slotIndexOut = loopId`.

`stepLoopPersistWorkItem` then writes `trackManager.getTrack(0).getLoop(0)` to `loop_00_00.bin`, not the capturing track’s loop.

**Correct API already exists:** `StorageManager::admitLoopSlotPersist(trackIndex, slotIndex)` uses `persistKeyForSlot` and resolves unambiguously.

## Architecture checkpoint

| Question | Answer |
|---|---|
| **Ownership change?** | NO — extend `StorageManager` admission; same deferred save owner |
| **State transition change?** | NO |
| **Unbounded interval owner?** | NO |
| **Reuse** | YES — route callers to `admitLoopSlotPersist`; tighten `resolveTrackSlotForLoopId` |

## Invariant (post-fix)

`LoopPersist` work keyed by **`(trackIndex, slotIndex)`** must read/write `loop_TT_SS.bin` for that same pair. `loopId` alone must not admit or resolve loop body persistence when multiple tracks share the same slot-local id.

## Stages

| Stage | Scope | Verification |
|---|---|---|
| **RC1** | Replace every `admitLoopPersist(loopIdForSlot(...))` with `admitLoopSlotPersist(trackIndex, slotIndex)` where track+slot are known | Native; grep shows zero hot-path `admitLoopPersist(` except deprecated wrapper |
| **RC2** | `resolveTrackSlotForLoopId`: remove track-0 fallback; return false on ambiguous/missing id (queue item fails resolve telemetry) | Native fixture |
| **RC3** | Deprecate `admitLoopPersist(LoopId)` — thin wrapper only if legacy callers remain; prefer delete | Build |
| **RC4** | Device: record+overdub on track 5 slot 0; `probe_lsv1_slot_body.py loop_04_00.bin` non-empty; `loop_00_00.bin` unchanged if track 0 unused | **SHIPPED** — [`192637`](../../captures/session_20260911_192637.log): `LoopPersist,slot:4:0,done,ok` (L30602); `loop_04_00.bin` 12 261 B LSV1; `loop_00_00.bin` 78 B stale |

### RC1 call sites (implementer checklist)

| File | Symbol / context |
|---|---|
| `TrackCaptureStopCommit.cpp` | overdub/record stop — has `persistTrackIndex`, `persistSlotIndex` |
| `TrackStopTelemetryColdHelpers.cpp` | stop telemetry persist admit |
| `TrackManagerCaptureQueue.cpp` | capture queue |
| `TrackUndo.cpp` | undo/redo — has `resolveTrackIndexForPersistence` |
| `LoopEditManager.cpp` | edit autosave admit |
| `PersistenceAdmit.cpp` | `processEditAutosave` — already has `trackIndex` / `slotIndex` in loop |

Use `admitLoopSlotPersist` (includes slot meta admit). Callers that already `markLoopSlotMaterialDirty` stay unchanged; duplicate slot-meta admit is queue-deduped.

### RC2 — resolve hardening

In `resolveTrackSlotForLoopId`:

- Keep scan for unique match.
- **Delete** block that sets `trackIndexOut = 0`, `slotIndexOut = loopId` when scan fails.
- If multiple tracks share same `loopId`, treat as **resolve failure** (log + complete work item `resolve,false`).

Optional follow-up (out of RC1): globally unique `LoopId` at pool init — not required if all admits use slot keys.

## Tests

| Test | Owner |
|---|---|
| Native: admit `persistKeyForSlot(4,0)` resolves to track 4 slot 0; `persistKeyForLoop(0)` with two tracks’ slot 0 both `loopId==0` must **not** resolve to track 0 when admit used slot key for track 4 | `test_persistence_work_queue` or new `test_persistence_loop_slot_resolve` |
| Existing `test_persistence_work_queue` key equality unchanged for slot keys | regression |
| Device: [`session_20260911_190525.log`](../../captures/session_20260911_190525.log) class repro — track 5, verify `loop_04_00.bin` mtime + size >> 78 | HITL manual or `base` on track 5 |

## Observability (RC1)

After fix, `#CAP` persist lines should show `LoopPersist,slot:4:0,...` (not `loop:0`) when track 5 slot 0 saves.

## Explicit non-goals

- Globally unique `LoopId` pool redesign (unless RC2 leaves orphan `persistKeyForLoop` users)
- Boot `CurrentSetBootLoad` LSV1
- Changing `loop_TT_SS` path layout

## Pre-implementation review

### Ready

- Root cause confirmed in code + capture + SD artifacts.
- Fix reuses `admitLoopSlotPersist` / `persistKeyForSlot` — no new persistence owner.

### Resolved

| Topic | Decision |
|---|---|
| Fix shape | Slot-keyed admit at all call sites; remove dangerous loopId resolve fallback |
| API | Prefer `admitLoopSlotPersist`; deprecate `admitLoopPersist(LoopId)` |
| 5.4 impact | Re-open SD byte probe after RC4 device gate |

### Proceed?

**YES** — RC1 implementation may start.

## RC4 device closeout (2026-09-11)

| Check | Result |
|---|---|
| Persist key telemetry | **PASS** — all `LoopPersist` lines use `slot:4:0`; zero `loop:0` |
| `done,ok` | **PASS** — L24993, L30602 |
| SD `loop_04_00.bin` | **PASS** — 12 261 B, `SAVE` trailer, LSV1 `formatVersion=1` at offset 10 |
| SD `loop_00_00.bin` | **PASS** — 78 B unchanged (wrong slot not written) |
| Probe script | Fixed — `EPOCH_FILE_HEADER_BYTE_SIZE=10` matches `CurrentWorkspaceStorage::kEpochFileHeaderByteSize` |
