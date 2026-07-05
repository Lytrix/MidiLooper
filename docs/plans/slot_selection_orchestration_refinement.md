# Slot selection orchestration refinement

Side fix **parallel to UIP Phase 5** — does not block UIP HITL or overlap resume.

| Artifact | Location |
|----------|----------|
| Implementation plan | [`.cursor/plans/slot_selection_orchestration_e2452b69.plan.md`](../../.cursor/plans/slot_selection_orchestration_e2452b69.plan.md) |
| **Next-chat handoff** | [`slot_selection_focus_implementation_handoff.md`](slot_selection_focus_implementation_handoff.md) |
| OpenSpec | `openspec/changes/slot-selection-focus/` — **proposed** (run `/opsx:apply` to implement) |
| UIP link | One row in `unified-interval-projection/proposal.md` only — **not** UIP tasks |

## OpenSpec decision

**Not** a UIP spike. Separate change `slot-selection-focus` for formal tracking. UIP Phase 4 `queuedStartTick` is a dependency consumed via `requestSlotSwitch`, not extended.

## Canonical lifecycle (application pattern)

**Departure → Transition → Arrival** — reuse for track, slot, editor, timeline window, layer, and Control Change focus. v1 slot/track hooks are thin wrappers; evolve to `beforeFocusChange` / `afterFocusChange`.

## Index invariants

```
selectedSlotIndex  = UI / editor focus
activeLoopIndex    = playback / capture focus
```

Both persisted independently in SD footer.

## Selection vs editing

```
Selection is observational.
Editing is transformational.
```

Selection may change focus, rebind edit sessions, invalidate display caches, update UI. It must not modify loop contents, rewrite passes, or transform musical data.

## Phase 1 playback API

`SyncPlayback::Yes | No` on `setSelectedSlotIndex` — **default `Yes`**. Playing paths (including edit mode) explicitly pass **`No`** + `requestSlotSwitch` so departure can commit edit pass before active switches at grid.

## Display on slot change

`DisplayManager::invalidateForSlotChange` — **invalidate caches only**. No synchronous redraw. Normal `DisplayManager::update()` loop performs redraw.

## `getSelectedLoop`

Provide mutable + `const` overloads; prefer **`const`** for read-only callers.

## Queued tick

`queuedStartTick` shipped (UIP Phase 4). Orchestrator does not queue; playing paths chain `requestSlotSwitch` after `setSelectedSlotIndex(…, SyncPlayback::No)`. See [`Loops.md`](../Guides/control-surface/Loops.md).

## Tests

Single-transition cases plus **repeated slot 1↔2 stress** — no stale caches, leaked bindings, or pass mutations.

## Ownership

| Concern | Owner |
|---------|--------|
| Focus lifecycle orchestration | `TrackManager::setSelectedSlotIndex` |
| Playback sync (Phase 1) | Caller (`SyncPlayback`) |
| Queued playback | Caller (`requestSlotSwitch`) |
| Edit rebind | `EditManager` |
| Display cache invalidation | `DisplayManager` (no sync draw) |
| Persistence | `StorageManager` footer |

## Propose checklist

OpenSpec change created at `openspec/changes/slot-selection-focus/`:

- [x] `proposal.md`, `design.md`, `tasks.md`, delta specs
- [x] UIP **Relationship** row in `unified-interval-projection/proposal.md`
- [x] `CURRENT_WORK.md` — parallel track section
- [ ] Implement via `/opsx:apply` or agent session — **firmware shipped 2026-07-05**; manual §8 pending
