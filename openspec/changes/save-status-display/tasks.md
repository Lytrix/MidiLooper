## 1. OpenSpec and API

- [x] 1.1 Add `DeferredSaveDisplayPhase`, `DeferredSaveDisplayStatus`, and `getDeferredSaveDisplayStatus(nowMs)` to `StorageManager`.
- [x] 1.2 Record `deferredSaveCompletedAtMs` / `deferredSaveFailedAtMs` when deferred save job finishes.
- [x] 1.3 `BYPASS_STOP_UNDO_SAVE` returns Idle phase always.

## 2. Display

- [x] 2.1 Add `DisplayManager::drawSaveStatusIndicator(nowMs)` — 4-dot horizontal row, y≈47, right-aligned in sidebar.
- [x] 2.2 Call from `drawSidebar` after undo field.
- [x] 2.3 Verify layout on 256×64 SSD1322: no overlap with BPM/mode/undo or bottom info strip.

## 3. Telemetry and docs

- [x] 3.1 Optional: `#CAP,SAVE,<phase>,rotateStep` on phase transitions in `DebugSessionCapture.h`.
- [x] 3.2 Add sidebar indicator paragraph to `docs/Guides/DEFERRED_RUNTIME_PERSISTENCE.md`.

## 4. Verification and closeout

- [x] 4.1 Native: `test_save_status_display` — phase mapping (Idle, Pending, InProgress, Completed, Failed, rotateStep).
- [x] 4.2 Run `pio test -e native`.
- [ ] 4.3 Manual: record → PLAYING → spinner completes before transport stop; serial `PERS,result,...,ok`.
- [x] 4.4 Run `openspec validate save-status-display`.
