# Handoff — Slot selection focus (implementation)

**Date:** 2026-07-05  
**Branch:** `derived-note-overlap-logic` (uncommitted partial work — see below)  
**OpenSpec:** [`openspec/changes/slot-selection-focus/`](../../openspec/changes/slot-selection-focus/) — **proposed, reviewed, apply-ready**  
**Apply command:** `/opsx:apply slot-selection-focus` or implement from [`tasks.md`](../../openspec/changes/slot-selection-focus/tasks.md) §1–8  
**Parallel to:** UIP Phase 5 — **does not block** UIP 5.5 HITL  
**Refinement plan:** [`slot_selection_orchestration_refinement.md`](slot_selection_orchestration_refinement.md)

---

## Status summary

| Phase | Status |
|-------|--------|
| OpenSpec propose + review | **Done** (§0 tasks complete) |
| Firmware implementation | **Not started** (consolidate partial branch work) |
| Native tests | **Not started** |
| Manual / HITL | **Not started** |

**Primary OpenSpec still active on branch:** [`unified-interval-projection`](../../openspec/changes/unified-interval-projection/) Phase 5.5 HITL remaining.

---

## One-line outcome

Centralise slot selection in **`TrackManager::setSelectedSlotIndex`** with canonical **Departure → Transition → Arrival** lifecycle, session-type-agnostic edit rebind, **`DisplayManager`-owned cache invalidation**, and independent persistence of **`selectedSlotIndex`** vs **`activeLoopIndex`**.

---

## Original bug

Switching loop slots on the selected track does not refresh piano-roll MIDI in **LOOP_EDIT** (and future session types). **NOTE_EDIT** works via ad-hoc `reopenNoteEditSession`. Root cause: fragmented two-index model + no shared focus lifecycle.

---

## Locked decisions (do not re-litigate without design session)

### Index invariants

```
selectedSlotIndex  = UI / editor focus (piano roll, LEDs, edit binding)
activeLoopIndex    = playback / capture focus (getActiveLoop(), record/overdub)
```

Both persisted independently in SD footer. Intentional desync allowed (`finalizeCaptureAndSelectSlot`, multi-slot playback).

### Selection vs editing

```
Selection is observational.
Editing is transformational.
```

Slot change may rebind edit session RAM and invalidate caches — must not mutate `Loop` pass data.

### SyncPlayback (Phase 1)

| Parameter | Behaviour |
|-----------|-----------|
| **Default** | **`SyncPlayback::Yes`** — omit third arg → immediate `activeLoopIndex` sync |
| **Playing** (incl. edit mode) | Explicit **`SyncPlayback::No`** + **`requestSlotSwitch(NextGrid \| LoopEnd)`** |
| **Why playing uses No+queue** | Departure commits edit pass / geometry **before** index change; queued active switch at grid gives save side effects time to finish |

Queued tick is **already shipped** (UIP Phase 4) via `requestSlotSwitch` → `queuePlaybackStartAtGrid`. **Not** inside orchestrator.

### Lifecycle (application pattern)

```
Departure   → EditManager::commitEditSessionOnDepart
Transition  → selectedSlotIndex (+ optional active sync per SyncPlayback)
Arrival     → reenterEditSessionForFocusChange + DisplayManager::invalidateForSlotChange + LEDs + deferred save
```

- Full lifecycle only when **`trackIndex == selectedTrack`**
- Background track: **transition only** (index update, no edit/display arrival)

### Display

`DisplayManager::invalidateForSlotChange` — **cache invalidation only**, no synchronous `display()`. Redraw on next `DisplayManager::update()`.

### SD footer extension

After `activeLoopIndex[NUM_TRACKS]`:

1. `uint32_t 0x534C4F54` (`'SLOT'`) if extension present  
2. `selectedSlotIndex[NUM_TRACKS]`  
3. Existing `kGlobalUndoStackToken` + undo stacks  

Legacy read: peek after active array — if not `'SLOT'`, `selectedSlotIndex[t] = activeLoopIndex[t]`.

**Revision load:** footer when bundled transport has it; else **first occupied slot** for both indices (`writeDefaultRevisionLoadTransportBody`).

### API

- `TrackManager::getSelectedLoop(trackIndex)` + **`const`** overload (prefer const for read paths)
- Future: `PlaybackSelectionPolicy` enum — not v1

---

## Partial branch work (consolidate, do not duplicate)

Uncommitted changes on `derived-note-overlap-logic` (~226 lines across 4 files):

| File | Partial content |
|------|-----------------|
| [`src/EditManager.cpp`](../../src/EditManager.cpp) | `beforeSelectedSlotChange`, `onSelectedSlotChanged`, `loopForNoteEditFocus`, track-switch loop commit |
| [`src/LoopEditManager.cpp`](../../src/LoopEditManager.cpp) | `loopForLoopEdit`, `selectedSlotForTrack`, `commitLoopEditOnDepart`, `reopenLoopEditSession` partial |
| [`src/TrackManager.cpp`](../../src/TrackManager.cpp) | Edit hooks on slot change, edit-mode active sync patch |
| [`include/DisplayManager.h`](../../include/DisplayManager.h) | `invalidateLiveDisplayCache` made public |

**Next session:** Refactor into spec design (shared `commitEditSessionOnDepart` / `reenterEditSessionForFocusChange`, `SyncPlayback` param, `invalidateForSlotChange`, default Yes, playing explicit No+queue). Remove ad-hoc helpers in favour of `getSelectedLoop`.

---

## Implementation order (from OpenSpec tasks)

1. **§1 TrackManager** — `SyncPlayback`, `getSelectedLoop`, full orchestrator, background-track rule  
2. **§4 DisplayManager** — `invalidateForSlotChange` (before or with §1 arrival wiring)  
3. **§3 EditManager** — shared depart/arrive; `LoopEditManager::reopenLoopEditSession`  
4. **§5 Route consumers** — replace `loopForNoteEditFocus` / `loopForLoopEdit`  
5. **§2 MidiButtonActions** — caller matrix + [`Loops.md`](../Guides/control-surface/Loops.md)  
6. **§6 StorageManager** — footer extension + revision fallback  
7. **§7 Native tests** — `test_slot_switch_edit_sessions` + stress + footer  
8. **§8 Manual** — NOTE_EDIT / LOOP_EDIT slot 1↔2 stopped + playing  

---

## Key files

| Area | Files |
|------|--------|
| Orchestrator | [`include/TrackManager.h`](../../include/TrackManager.h), [`src/TrackManager.cpp`](../../src/TrackManager.cpp) |
| Edit lifecycle | [`include/EditManager.h`](../../include/EditManager.h), [`src/EditManager.cpp`](../../src/EditManager.cpp) |
| Loop edit | [`include/LoopEditManager.h`](../../include/LoopEditManager.h), [`src/LoopEditManager.cpp`](../../src/LoopEditManager.cpp) |
| Display | [`include/DisplayManager.h`](../../include/DisplayManager.h), [`src/DisplayManager.cpp`](../../src/DisplayManager.cpp) |
| Buttons | [`src/MidiButtonActions.cpp`](../../src/MidiButtonActions.cpp) |
| Persistence | [`src/StorageManager/WorkspaceSave.cpp`](../../src/StorageManager/WorkspaceSave.cpp), [`src/StorageManager.cpp`](../../src/StorageManager.cpp), [`include/StorageSession.h`](../../include/StorageSession.h) |
| Revision fallback | [`src/StorageManager/RevisionLoad.cpp`](../../src/StorageManager/RevisionLoad.cpp) |
| Reference test | [`test/test_note_edit_track_switch/`](../../test/test_note_edit_track_switch/) |

---

## Architecture checkpoint (already passed)

| Question | Answer |
|----------|--------|
| Ownership change? | Extend `TrackManager` / `EditManager` / `DisplayManager` — no new top-level module |
| State transitions? | Formalise Departure → Transition → Arrival — agreed |
| Musical data on selection? | No — observational selection invariant |

---

## Tests to add

| Case | Suite |
|------|--------|
| Note edit slot switch | `test_slot_switch_edit_sessions` |
| Loop edit slot switch | same |
| ControlChange stub | same |
| Default `SyncPlayback::Yes` vs playing `No`+queue | same |
| Background track transition only | same |
| Repeated slot 1↔2×5 stress | same |
| Footer round-trip both indices | native storage harness or dedicated test |

Register suite in `platformio.ini` `[env:native]`.

**Gate:** `pio test -e native` before push.

---

## Manual verification matrix

| Mode | Action | Expect |
|------|--------|--------|
| LOOP_EDIT, stopped | Slot 1↔2 | Piano roll + loop faders match selected slot |
| NOTE_EDIT, stopped | Slot 1↔2 | Session store + display match selected slot |
| LOOP_EDIT / NOTE_EDIT, **playing** | Short-press other slot | UI focus immediate; departure commits edit; active at next 16th |
| Multi-slot playing | Focus-only move | Display = selected; audio = active until grid |
| Reboot | After deferred save | Both indices restored independently |

---

## Out of scope

- UIP / `IntervalProjection` changes  
- Full ControlChange edit  
- `edit-session-action-geometry` overlap pipeline  
- EEPROM / `settings.json`  
- Generic `beforeFocusChange` API (v1 uses slot/track hook names)

---

## Agent session start checklist

1. Read [`docs/Runtime/PROJECT_STATE.md`](../Runtime/PROJECT_STATE.md) + [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md)  
2. Read [`openspec/changes/slot-selection-focus/tasks.md`](../../openspec/changes/slot-selection-focus/tasks.md)  
3. Skim [`design.md`](../../openspec/changes/slot-selection-focus/design.md) D1–D6  
4. Run architecture checkpoint only if changing ownership/transitions beyond this spec  
5. Consolidate partial diff before adding parallel paths  
6. Update `tasks.md` checkboxes + `CURRENT_WORK.md` on completion  
7. `pio test -e native` before push  

---

## Related links

| Doc | Role |
|-----|------|
| [`openspec/changes/slot-selection-focus/proposal.md`](../../openspec/changes/slot-selection-focus/proposal.md) | Why / capabilities |
| [`openspec/changes/slot-selection-focus/specs/slot-selection-focus/spec.md`](../../openspec/changes/slot-selection-focus/specs/slot-selection-focus/spec.md) | Normative requirements |
| [`docs/Guides/control-surface/Loops.md`](../Guides/control-surface/Loops.md) | Button semantics (update in task 2.4) |
| UIP cross-link | [`unified-interval-projection/proposal.md`](../../openspec/changes/unified-interval-projection/proposal.md) Relationship table |
