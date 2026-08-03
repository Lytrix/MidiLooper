# Note edit control-surface split

**Status:** Implementation-ready — Phase 0 inventory + sign-off before coding  
**Branch:** `chore/note-edit-control-surface-split`  
**Kind:** Structural refinement (behavior-preserving per phase)  
**Supersedes:** rename-only `note_edit_manager_rename_refinement.md` (removed)

---

## Definitions

**ControlSurfaceManager** coordinates the NOTE_EDIT control surface. It translates **hardware gestures** into **edit operations** and **edit events** into **hardware feedback**. It owns no editing state or note data.

**Ownership model (who owns what):**

| Component | Role |
|-----------|------|
| `MidiHandler` | MIDI reception and routing |
| GPIO / encoder drivers | Hardware interrupts and low-level I/O |
| `EditManager` | Editing state, session, undo |
| `ControlSurfaceManager` | Coordinates NOTE_EDIT interaction between hardware ingress and `EditManager` |

**Allowed ControlSurfaceManager state** — transient hardware state only:

- Debounce timers, ignore windows, motor scheduling, outbound queues, hardware synchronization
- **Never:** editing state, selection state, undo state, or note geometry

**Hardware gesture** — a physical interaction originating from the control surface (button press, encoder movement, MIDI CC, motor-fader touch, etc.).

**Edit operation** — user intent independent of the physical hardware that produced it. Examples: `DeleteSelectedNote`, `MoveSelection`, `ToggleLengthMode`, `MoveSelectedNote`.

**Edit event** — notification from `EditManager` that editing state changed. Edit events decouple editing from hardware feedback: `EditManager` owns editing state; `ControlSurfaceManager` independently decides how hardware should react.

**Recommended terminology**

| Physical layer | Editing layer |
|----------------|---------------|
| Hardware gesture | Edit operation |
| `ControlSurfaceManager` | `EditManager` |
| Edit event | Hardware feedback |

**Architectural reference flow** (event-driven boundary — preserve this):

```text
Hardware gesture
        │
        ▼
ControlSurfaceManager
        │
        ▼
Edit operation
        │
        ▼
EditManager
        │
        ▼
Edit event
        │
        ▼
ControlSurfaceManager
        │
        ▼
Motor feedback
Display feedback
Future hardware feedback
```

**Target layering (EditManager responsibilities)**

```text
EditManager
    ├── EditSession
    ├── EditNoteState (FSM)
    └── EditNoteGeometry   ← note move/resize/overlap (via NoteMovementUtils / EditApply; not storage)
```

`EditNoteGeometry` is an architectural responsibility label, not a new class name in Phase 1–7 unless implementation needs one.

---

## Ownership contracts (architectural law)

| Component | May know | Must not know |
|-----------|----------|---------------|
| **ControlSurfaceManager** | Hardware protocols, MIDI CCs, GPIO events, motor timing, ignore windows, outbound scheduling, **transient hardware state only** (see Definitions) | Note storage, undo, selection internals, loop geometry algorithms, `EditPass` commit, editing/selection/undo state |
| **EditManager** | Edit session, selection, undo, edit state, **EditNoteGeometry** (via `NoteMovementUtils` / `EditApply`) | MIDI CC numbers, motor scheduling, fader feedback ignore periods |
| **Loop / Storage** | Event storage, passes, persistence | Hardware concepts |

Utility modules (`NoteMovementUtils`, `NoteEditFader*`, `SelectNavigation` helpers) stay **stateless** unless ownership genuinely moves — move **ownership**, not utility code.

---

## Problem

[`NoteEditManager`](../../include/NoteEditManager.h) (~2.5k lines) is misnamed **and** overloaded:

| Concern | Examples today | Intended owner |
|---------|----------------|----------------|
| **Control surface** | `handleMidiCC/Pitchbend`, fader ignore windows, outbound pipeline, motor sync | `ControlSurfaceManager` (after Phase 6 rename) |
| **Edit operations** | `deleteSelectedNote`, `moveNoteToPosition`, `changeNoteEndWithOverlapHandling` | `EditManager` |
| **Edit reads** | `buildSelectNavigationSlots`, `selectableDisplayNotesForEditUi` | `EditManager` |
| **Loop geometry edit** | `loopEditManager` nested member | `LoopEditManager` at peer level |

Bidirectional coupling today:

- **EditManager → NoteEditManager:** fader feedback, length-mode queries, read helpers
- **NoteEditManager → EditManager:** mutations, session reads

**Mixed-method examples** (split responsibilities before moving — do not move whole methods):

| Method | Edit responsibility | Surface responsibility |
|--------|--------------------|------------------------|
| `deleteSelectedNote` | focus rebuild, `commitEditAction`, selection clear | length-mode reset on select, audition release |
| `toggleLengthEditingMode` | `beginGeometryMutation`, `setSelectedTick`, `commitAllPendingNoteEditActions` | debounce, `requestFaderOutbound`, driver-fader latch |
| `handleCoarseFaderInput` | tick mapping + `moveNoteToPosition` | `refreshEditingActivity`, `scheduleOtherFaderUpdates`, ignore windows |

**Length-mode lifecycle** (from code):

- Cleared on session boundary and note select
- Within active NOTE_EDIT session, toggles fader routing and syncs `NoteEditKind` (`Move` vs `Length`)
- **Decision:** semantic length/position mode is **session-level** on `EditManager` / `NoteEditSessionState.kind`; surface reacts to `LengthModeChanged` edit events

---

## Edit events (one-way port)

Replace `EditManager` → `noteEditManager.refreshMotors()` direct calls.

```cpp
enum class EditEvent {
    SessionOpened,
    SessionClosed,
    SelectionChanged,
    GeometryChanged,
    LengthModeChanged,
};

class EditEventListener {
 public:
    virtual void onEditEvent(EditEvent event) = 0;
};
```

**Payload policy (Option A — preferred):** events carry **no payload** and **no `Track&`**. `ControlSurfaceManager` queries `EditManager` (and `trackManager` when needed) for only the information required for hardware feedback. Keeps the interface small and avoids leaking storage into the hardware layer.

**Option B (if a payload is unavoidable later):** dedicated minimal structs per event type — never expose full `Track` through the listener.

- `EditManager` holds optional `EditEventListener*` (wired in `main.cpp`)
- `ControlSurfaceManager` implements listener; maps events → motor outbound
- **No** reverse calls from `EditManager` into surface methods

---

## Non-goals

- Changing NOTE_EDIT fader timing contracts or HITL pass criteria without approval
- Renaming `EditManager`, `NoteEditSession*`, CAP/HITL strings that mean the **feature**
- Capture/stop/persistence ownership changes
- Renaming `MidiButtonManager` / `MidiFaderManager` types in early phases

---

## Pre-implementation review

### Resolved

| Topic | Decision |
|-------|----------|
| Locked name | **`ControlSurfaceManager`** / `controlSurfaceManager` (Phase 6) |
| Split before rename | **YES** |
| Session owner | **`EditManager`** unchanged |
| Edit ↔ surface link | `EditEvent` listener (one-way, payload-free) |
| Length mode | Session-level semantic on `EditManager`; surface reacts to `LengthModeChanged` |
| Utils | Stay put; move ownership not algorithms |
| Phase 8 scope | **Optional north star** — future extension after Phase 7; not required to close the refactor |

### Open before Phase 1

- [ ] Phase 0 method inventory + mixed-method split table
- [ ] User sign-off on ownership contracts and phase order

### Proceed?

**NO** until Phase 0 complete.

---

## Phase dependency evolution

### After Phase 1

```text
ControlSurface ──edit operations──▶ EditManager ──▶ Loop
     │ (transitional wrappers; mixed reads/motors remain)
```

**Success:** No edit mutations remain inside interim surface type.

### After Phase 2

**Success:** `EditManager` no longer queries control-surface read helpers.

### After Phase 3

```text
ControlSurface ◀──EditEvent── EditManager ──▶ Loop
```

**Success:** Length editing mode has exactly one semantic owner.

### After Phase 4

**Success:** Interim type contains no edit algorithms; wrappers removed; ingress/egress + event reactions only.

### After Phase 5

**Success:** `LoopEditManager` is not nested inside surface type.

### After Phase 6

`NoteEditManager` → **`ControlSurfaceManager`** (mechanical rename).

### After Phase 7

Docs + hygiene review item 3 closed. **Refactor complete.**

### Optional Phase 8 — Architectural north star (future extension)

Phases 1–7 complete the NOTE_EDIT split. Phase 8 documents the preferred long-term direction; implement when consolidating physical ingress is prioritized.

```text
MidiHandler / GPIO ──▶ ControlSurfaceManager
                           ├── ButtonInput
                           ├── FaderInput
                           ├── EncoderInput
                           ├── MotorFeedback
                           └── FeedbackScheduler
                                    │
                                    ▼
                              EditManager
```

**North-star goal:** route **all NOTE_EDIT physical interaction** through `ControlSurfaceManager`.  
(`ButtonInput` / `FaderInput` / `EncoderInput` names are acceptable; revisit `*Controller` only if gesture interpretation grows.)

---

## Phases

### Phase 0 — Design lock (doc-only)

- [x] Inventory every `noteEditManager.*` call site; classify methods: **surface | edit operation | edit read | loop | mixed**
- [x] Mixed-method split table (minimum: `deleteSelectedNote`, `toggleLengthEditingMode`, `handleCoarseFaderInput`, `handleFineFaderInput`, `syncSelectionFromGeometryEdit`)
- [x] Architecture gate posted (see below)
- [x] User sign-off (2026-08-03)

| Question | Answer |
|----------|--------|
| Ownership change? | **YES** |
| State transition change? | **NO** |
| Behavior-preserving? | **YES** per phase |

**Production call sites:** 52 `noteEditManager.*` uses across 11 files (`EditManager.cpp` 28, fader/button/GPIO handlers 10, `TrackUndo`/`Track` 6, `main` 3, `EditSelectNoteState` 2).

**Highest-risk mixed methods:** `toggleLengthEditingMode`, `applyNoteSelectFromFader1Pitchbend`, `handleCoarseFaderInput` (Phase 3–4).

---

### Feedback gate during refactor

`kNoteEditFaderFeedbackEnabled` and `kEditedNoteAuditionEnabled` default **`false`** ([`FADER_STATE_SYSTEM.md`](../Guides/FADER_STATE_SYSTEM.md)).

| Rule | Detail |
|------|--------|
| Phases 1–3 | Inbound geometry works; outbound motor paths are no-ops behind `if constexpr` |
| Phase 4 | **Move** all outbound/motor/ignore-window code to `ControlSurfaceManager`; **keep** compile-time gates |
| Re-enable | Flipping flag to `true` + motor HITL is **separate** from structural refactor |
| Utils | `NoteEditFaderOutboundPlan`, `NoteEditFaderSelectSync`, `NoteEditDependentFaderSnapshot` stay; orchestration moves |

`applyFaderOutboundDisabledSideEffects` moves with `requestFaderOutbound` (session-open / length-mode flag side effects when feedback off).

---

### Phase 0 — Design lock (original checklist — superseded by completed block above)

### Phase 1 — Split mixed methods; move edit operations to `EditManager`

**Status:** Done (2026-08-03) — `671` native tests pass

**Pattern:** extract edit body → `EditManager`; surface keeps **transitional wrappers** only.

| Move to `EditManager` | Notes |
|-----------------------|-------|
| `deleteSelectedNote` | edit operation — **done** |
| `moveNoteToPosition`, `changeNoteEndWithOverlapHandling` | still use `NoteMovementUtils` — **done** |
| `cycleEditSession` routing | thin wrapper + audition on `NoteEditManager` — **done** |

> **Transitional wrappers:** wrapper functions in `NoteEditManager` exist only as migration aids and **must be removed in Phase 4** once all callers route through `EditManager` and the `EditEvent` interface.

Update: `MidiButtonActions`, `BarStepButtonHandler`, fader handlers.

**Tests:** `pio test -e native`

---

### Phase 2 — Edit read ownership on `EditManager`

Move **ownership** only (may call existing `SelectNavigation` helpers):

- `buildSelectNavigationSlots`, `selectableDisplayNotesForEditUi`, `syncReferenceStepFromSelectedTick`

Update: `EditManager`, `EditSelectNoteState`.

**Tests:** `pio test -e native`

---

### Phase 3 — Length mode single owner + edit events

- Consolidate `lengthEditingMode` into `EditManager` / `NoteEditSessionState`
- `toggleLengthEditMode(Track&)` on `EditManager`; surface handles button debounce ingress
- Emit `LengthModeChanged` / `SelectionChanged`
- Remove `EditManager` → `noteEditManager.isLengthEditingMode()` round-trips

**Tests:** `pio test -e native`

---

### Phase 4 — Control-surface extraction + `EditEvent` port

- Introduce `EditEvent` / `EditEventListener`; wire listener on interim type
- Move ingress/egress: fader handlers, outbound pipeline, ignore windows, `update()` tick
- **Remove Phase 1 transitional wrappers**
- Move `sendEditSessionChange` MIDI egress to surface listener for `SessionOpened` / `SessionClosed`

**Tests:** `pio test -e native` + HITL `edit_minimal`, `note_edit_select_dependent_faders`

---

### Phase 5 — Unnest `LoopEditManager`

**Status:** Done (2026-08-03) — `671` native tests pass; `teensy41-capture-serial` build OK

- Peer global `loopEditManager` (match `editManager` pattern)
- Update: `EditManager`, `TrackUndo`, `Track`

**Tests:** `pio test -e native`

---

### Phase 6 — Rename (mechanical)

**Status:** Done (2026-08-03) — `671` native tests pass; `teensy41-capture-serial` build OK

- `NoteEditManager` → **`ControlSurfaceManager`**
- Files: `include/ControlSurfaceManager.h`, `src/ControlSurfaceManager.cpp`
- Global: `controlSurfaceManager`
- Update live Guides: `CODE_STRUCTURE`, `FADER_STATE_SYSTEM`, `AGENT_CONTEXT_MAP`

**Tests:** `pio test -e native`; `pio run -e teensy41-capture-serial` (ask before upload)

---

### Phase 7 — Documentation + hygiene closeout

- Update [`codebase_hygiene_technical_debt_review.md`](codebase_hygiene_technical_debt_review.md) item 3
- Update [`runtime_process_building_blocks_overview.md`](runtime_process_building_blocks_overview.md)
- `CURRENT_WORK` / `PROJECT_STATE`

**Refactor complete after Phase 7.**

---

### Optional Phase 8 — Architectural north star (future extension)

Not required to close Phases 1–7. Implement when NOTE_EDIT physical ingress consolidation is the active slice.

**North-star goal:** route **all NOTE_EDIT physical interaction** through `ControlSurfaceManager`.

Examples (non-exhaustive): button presses, encoder movement, MIDI faders, future hardware controls.

- `MidiButtonActions`, `MidiFaderActions`, `GpioButtonManager` NOTE_EDIT paths delegate through `ControlSurfaceManager`
- Internal `ButtonInput` / `FaderInput` / `EncoderInput` modules (or `src/ControlSurface/` folder)
- **Out of scope:** record/overdub/transport button semantics; LED manager consolidation

**Tests (when implemented):** native + HITL `edit_minimal` + motor probe; GPIO encoder smoke

---

## Call-site map (production — starting inventory)

| Caller | Uses |
|--------|------|
| `main.cpp` | setup, `update` |
| `MidiHandler` | CC, pitchbend |
| `MidiFaderActions` | four fader inputs |
| `MidiButtonActions` | delete, length toggle |
| `EditManager` | reads, fader feedback, length mode, loop edit session |
| `EditSelectNoteState` | nav slots, display notes |
| `GpioButtonManager` | encoder, fader sync |
| `BarStepButtonHandler` | fader sync, delete |
| `Track` / `TrackUndo` | `loopEditManager` geometry restore |

---

## Verification matrix

| Phase | Architectural success | Automated gate |
|-------|----------------------|----------------|
| 1 | No edit mutations in surface type | `pio test -e native` |
| 2 | `EditManager` owns all edit reads | native |
| 3 | Single length-mode owner | native |
| 4 | No edit algorithms in surface; wrappers gone; events only | native + HITL edit |
| 5 | `LoopEditManager` unnested | native |
| 6 | Rename complete | native + capture-serial build |
| 7 | Docs synced; refactor **complete** | review checklist |
| 8 (optional) | All NOTE_EDIT physical ingress via `ControlSurfaceManager` | native + HITL + encoder smoke (when scheduled) |

**Risk:** Phase 4 fader feedback — archived OpenSpec `note-edit-fader-feedback-regression` BUG as checklist.

**Session rule:** one phase per implementation session.

**Final review (2026-08-03):** Architecturally mature; no further structural redesign required before Phase 0 inventory and implementation.
