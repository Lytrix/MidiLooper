# Current work (implementation scope)

**Highest operational priority.** Defines what to implement **now**. Load with [PROJECT_STATE.md](PROJECT_STATE.md) before planning or coding.

Last updated: 2026-06-29 (StorageSession Tier 3 partial)

---

## Now implementing

Set revision persistence + load/save overlay UX + **StorageSession state refactor (DEC-012)** on branch `load-save-sets-loops`:

- Primary OpenSpec: `openspec/changes/set-revision-persistence/tasks.md`
- Supporting: overlay display regression, save-status display, workspace-session-persistence as tasks demand
- Handoff (persistence features): [set_revision_persistence_handoff.md](../plans/set_revision_persistence_handoff.md)
- Handoff (state refactor Tier 0–3): [storage_session_state_refactor_handoff.md](../plans/storage_session_state_refactor_handoff.md) — **Tier 3 partial** (Internal + Overlay TUs); FSM split next

## Explicitly NOT implementing

- D13 arrangement jam **capture** — future roadmap only ([ROADMAP.md](ROADMAP.md))
- `currentset-savedset-storage-layout` — parked; superseded by revision model
- JamRecorder, M10 Scenes, playback hardening — not this sprint
- New `*Manager` classes for persistence — extend `StorageManager` (DEC-008)
- GPIO `ButtonManager` revival — unless explicitly scoped in a new CURRENT_WORK revision

## Current target

1. **Tier 3 (remaining)** — split deferred save / revision commit / revision load FSM TUs ([handoff](../plans/storage_session_state_refactor_handoff.md))
2. Remaining `set-revision-persistence` `tasks.md` items (overlay slice)

## Completion conditions

- [x] Tier 0 complete per storage session handoff
- [x] Tier 2 complete per storage session handoff
- [ ] Tier 3 FSM TU split complete
- [ ] `set-revision-persistence` tasks.md items for current slice marked done or explicitly deferred in OpenSpec
- [x] `pio test -e native` passes for touched logic
- [ ] Overlay load/save flow manually verified on hardware when UI/SD paths change
- [ ] PROJECT_STATE + this file updated at session close

## Blocked by

- None for revision/overlay track
- Parked flat SavedSet layout — do not revive without new OpenSpec + DECISION_LOG entry
