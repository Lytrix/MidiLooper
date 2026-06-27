# Supersession — M3–M4 and load policy

**Date:** 2026-06-26  
**Superseded by:** `openspec/changes/set-revision-persistence/`

## Shipped (keep)

M1 CurrentSet persistence, M2 backend (`saveNewSet`, `loadSetIntoCurrent`, SetIndex, 8h failsafe),
2.13–2.14 display + load policy tests, save-status display.

## Superseded for new work

| Task | Was | Now |
|------|-----|-----|
| 2.11 auto-save before load | `shouldAutoSaveBeforeLoadIntoCurrent` | **Removed** — load revision without auto-commit |
| M3 slot import | SavedSet folder sources | `set-revision-persistence` §5 — revision sources |
| M4 RecoveryPoint + browser | Flat SavedSet polish | `set-revision-persistence` §6 recovery + §4 overlay |
| 2.8 eight-hour failsafe | auto `saveNewSet` | **Parked** — Current continuous save; revisit post-revision Save |

## Read-compat

Legacy `Sets/` trees are **invalidated** at format bump. Remove shims when new layout lands; no migration tests.
