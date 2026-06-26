## Context

[`DisplayManager::drawSidebar`](../../../src/DisplayManager.cpp) renders BPM, transport mode, and undo count in a 30px right column. Unused vertical space exists below undo (`undoY = 37`) before the bottom info strip (`DISPLAY_HEIGHT - 12`).

[`StorageManager`](../../../include/StorageManager.h) tracks deferred save via internal flags (`deferredSavePending`, `deferredSaveInProgress`, `deferredSaveLastCompletedOk`) but exposes no display-facing snapshot. Loop and edit saves share one FSM ([`processDeferredSaveState`](../../../src/StorageManager.cpp), [`processEditAutosave`](../../../src/StorageManager.cpp)).

**Locked product decision:** Single 4-dot spinner for combined deferred save (no separate edit channel).

Primary files: `DisplayManager.cpp`, `StorageManager.cpp`, `StorageManager.h`.

## Goals / Non-Goals

**Goals:**

- Visible save progress during long **PLAYING** sessions without transport stop.
- Agent-launcher-style rotating dot animation during in-progress saves.
- Brief completed/failed flash so users know when SD write finished.
- Allocation-free read path for display hot loop (~30 FPS).

**Non-goals:**

- Save policy changes (playback deferral, dirty tracking, slice sizing).
- Separate indicators for loop vs edit saves.
- HITL gate on spinner pixels (optional serial phase telemetry only).

## Decisions

### Decision 1: Single combined spinner channel

One 4-dot row reflects the unified deferred save job (loop slots, meta, undo, edit dirty flush).

**Rationale:** Loop and edit mutations both call `requestDeferredSaveState`; splitting channels would require FSM reason tracking with no user benefit in v1.

### Decision 2: Read-only snapshot from StorageManager

```cpp
enum class DeferredSaveDisplayPhase : uint8_t { Idle, Pending, InProgress, Completed, Failed };

struct DeferredSaveDisplayStatus {
    DeferredSaveDisplayPhase phase;
    uint8_t rotateStep; // 0–3 when InProgress
};

static DeferredSaveDisplayStatus getDeferredSaveDisplayStatus(uint32_t nowMs);
```

DisplayManager calls this from `drawSidebar`; it does not read internal static flags directly.

**Rationale:** Keeps display decoupled; phase logic stays with save FSM owner.

### Decision 3: Completion timestamps, not sticky flags

Record `deferredSaveCompletedAtMs` / `deferredSaveFailedAtMs` when job finishes (alongside `PERS,result`). Completed/Failed phases expire after 800 ms. New `requestDeferredSaveState` immediately shows Pending/InProgress.

**Rationale:** Avoids stale "completed" state; matches brief flash UX.

### Decision 4: Draw in drawSidebar below undo

Placement: y≈47, right-aligned, 4 single-pixel dots with 2px spacing. Brightness: 2 (pending dim), 8 (active rotate), 15 (completed), 6 (failed).

Spinner draws even when `isDeferredSaveActive()` blocks full display refresh — dots are cheap pixels in the sidebar region.

### Decision 5: Optional phase-transition telemetry

Emit `#CAP,SAVE,<phase>,rotateStep` only on phase changes (not every frame) when `SESSION_CAPTURE` is enabled.

## Display phase mapping

| Phase | Condition |
|-------|-----------|
| **InProgress** | `deferredSaveInProgress` |
| **Pending** | `deferredSavePending && !deferredSaveInProgress` |
| **Completed** | `!hasDeferredSaveWork()` && last result ok && `(nowMs - completedAt) < 800` |
| **Failed** | `!hasDeferredSaveWork()` && last result failed && `(nowMs - failedAt) < 800` |
| **Idle** | otherwise |

`rotateStep = (nowMs / 200) % 4` when InProgress.

## Risks / Mitigations

| Risk | Mitigation |
|------|------------|
| Dot overlaps bottom info strip | y=47 keeps 5px gap above strip at y=52 (64px display) |
| Completed flash missed at low FPS | 800 ms window spans ~24 frames at 30 FPS |
| BYPASS_STOP_UNDO_SAVE builds | API returns Idle always |

## Verification

- Native: phase mapping unit test with injected timestamps.
- Manual: record → PLAYING → spinner Pending → rotate → flash complete before transport stop.
- HITL baseline unchanged except optional `#CAP,SAVE` lines.
