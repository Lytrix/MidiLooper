## Why

After record or overdub stop, deferred save runs in the background while transport may stay in **PLAYING** for minutes. There is no on-device feedback that the loop is safely on SD. Users need a lightweight, non-blocking indicator that save is queued, writing, or finished.

Brownfield context: [`docs/plans/workspace_session_persistence_handoff.md`](../../../docs/plans/workspace_session_persistence_handoff.md), [`docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md`](../../../docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md).

## What Changes

- **Sidebar save status indicator:** A single row of **4 dots** in the bottom-right sidebar (below undo), right-aligned. One combined channel for all deferred save activity (loop capture, edit-pass flush, loop-length edit, transport stop).
- **Animation:** Agent-launcher style — one bright dot rotates through positions 0–3 every ~200 ms while save is in progress.
- **States:** Idle (off), Pending (all dim), InProgress (rotate), Completed (all bright ≤800 ms), Failed (mid brightness ≤800 ms).
- **StorageManager snapshot API:** `getDeferredSaveDisplayStatus(nowMs)` — allocation-free read for DisplayManager.
- **Optional telemetry:** `#CAP,SAVE,<phase>,rotateStep` on phase transitions when `SESSION_CAPTURE` is enabled.

**Non-goals:**

- Separate edit vs loop save channels (v1 uses one spinner).
- SavedSet / CURRENT browser UI (see [`workspace-session-persistence` task 4.7](../workspace-session-persistence/tasks.md)).
- Text labels ("SAV", "SD") or blocking modal toasts.
- Changes to deferred save scheduling policy.

## Capabilities

### New Capabilities

- `save-status-display`: 4-dot sidebar spinner reflecting combined deferred-save phase.

### Related Capabilities (referenced, not redefined)

- `current-set-persistence`: deferred CurrentSet FSM is the data source for save phase.
- `long-record-memory-headroom`: display gating during SD I/O slices unchanged; spinner draws in sidebar regardless.

## Impact

| Area | Primary files |
|------|---------------|
| Display | `DisplayManager.cpp`, `DisplayManager.h` |
| Save phase API | `StorageManager.cpp`, `StorageManager.h` |
| Telemetry | `DebugSessionCapture.h` (optional `#CAP,SAVE`) |
| Tests | `test_save_status_display` (native phase mapping) |
| Docs | `docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md` (one paragraph) |

**Apply order:** After M1 CurrentSet persistence (shipped). Independent of M2 SavedSet browser. Coordinate with active `long-loop-piano-roll-window` (sidebar untouched by overview strip).

## Open decisions (TBD)

| Decision | Default in this change |
|----------|------------------------|
| Completed flash duration | 800 ms |
| Rotate interval | 200 ms per step |
| Dot Y position | y≈47 below undo (y=37) |
