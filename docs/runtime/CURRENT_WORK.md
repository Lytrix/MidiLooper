# Current work (implementation scope)

**Highest operational priority.** Defines what to implement **now**. Load with [PROJECT_STATE.md](PROJECT_STATE.md) before planning or coding.

Last updated: 2026-06-29 (StorageSession Tier 3 partial)

---

## Now implementing

Set revision persistence + load/save overlay UX + **StorageSession state refactor (DEC-012)** on branch `load-save-sets-loops`:

- **Primary OpenSpec (Tier 3):** `openspec/changes/storage-session-state-refactor/tasks.md`
- Supporting OpenSpec: `set-revision-persistence`, overlay display regression, save-status display
- Handoff (open items): [storage_session_state_refactor_open_items_handoff.md](../plans/storage_session_state_refactor_open_items_handoff.md)
- Handoff (full DEC-012 spec): [storage_session_state_refactor_handoff.md](../plans/storage_session_state_refactor_handoff.md)

## Explicitly NOT implementing

- D13 arrangement jam **capture** — future roadmap only ([ROADMAP.md](ROADMAP.md))
- `currentset-savedset-storage-layout` — parked; superseded by revision model
- JamRecorder, M10 Scenes, playback hardening — not this sprint
- New `*Manager` classes for persistence — extend `StorageManager` (DEC-008)
- GPIO `ButtonManager` revival — unless explicitly scoped in a new CURRENT_WORK revision

## Current target

1. **Tier 3 (remaining)** — [`storage-session-state-refactor` tasks](../openspec/changes/storage-session-state-refactor/tasks.md): Step **§8** next (`StorageSession` job struct migration); §7.6 HITL overlay verification pending hardware
2. Remaining `set-revision-persistence` `tasks.md` items (overlay slice — LoopPick parked)

## Completion conditions

- [x] Tier 0 complete per storage session handoff
- [x] Tier 2 complete per storage session handoff
- [x] Tier 3 FSM TU split complete (`storage-session-state-refactor` tasks §1–§6)
- [x] Overlay TU complete (`storage-session-state-refactor` tasks §7.1–§7.5)
- [ ] Overlay HITL verification (`storage-session-state-refactor` tasks §7.6)
- [ ] `StorageSession` job struct migration + API renames (`storage-session-state-refactor` tasks §8–§11)
- [x] `pio test -e native` passes for touched logic
- [ ] Overlay load/save flow manually verified on hardware when UI/SD paths change
- [ ] PROJECT_STATE + this file updated at session close

## Blocked by

- None for revision/overlay track
- Parked flat SavedSet layout — do not revive without new OpenSpec + DECISION_LOG entry
