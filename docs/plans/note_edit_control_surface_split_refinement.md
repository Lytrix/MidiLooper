# Note edit control-surface split

**Status:** Draft — design lock before coding  
**Branch:** `chore/note-edit-control-surface-split`  
**Kind:** Structural refinement (behavior-preserving per phase)  
**Supersedes:** rename-only [`note_edit_manager_rename_refinement.md`](note_edit_manager_rename_refinement.md) (removed)

---

## Problem

`NoteEditManager` is misnamed **and** overloaded. It is not the note-edit session owner; it mixes at least four concerns in one ~2.5k-line type:

| Concern | Examples today | Intended owner |
|---------|----------------|----------------|
| **MIDI control surface** | `handleMidiCC/Pitchbend`, fader ignore windows, outbound pipeline, motor sync | Thin adapter (new name in Phase 5) |
| **Edit mutations** | `deleteSelectedNote`, `moveNoteToPosition`, `changeNoteEndWithOverlapHandling` | `EditManager` (+ existing `NoteMovementUtils` / `EditApply`) |
| **Edit UI reads** | `buildSelectNavigationSlots`, `selectableDisplayNotesForEditUi` | `EditManager` or `SelectNavigation` helpers |
| **Loop geometry edit** | `loopEditManager` nested member | `LoopEditManager` at peer level (not inside control surface) |

`EditManager` already owns **EditSession**, focus, undo, and **EditNoteState** FSM. `EditManager` also **calls back into** `noteEditManager` for fader feedback and length-mode flags — bidirectional coupling.

Pure helpers already exist (`NoteEditFaderOutboundPlan`, `NoteEditFaderSelectSync`, `NoteMovementUtils`, …). The gap is **type boundaries**, not missing math.

---

## Goal

Draw a stable boundary:

```text
MidiHandler / MidiFaderActions / GpioButtonManager
        ↓
Control-surface adapter  (ingress + egress + motor timing only)
        ↓ calls
EditManager  (session, FSM, mutations, select/nav reads)
        ↓ uses
NoteMovementUtils / EditApply / Loop (storage)
```

**Rename** `NoteEditManager` → approved control-surface name **after** extraction (Phase 5 sub-step), not before.

---

## Non-goals

- Changing NOTE_EDIT fader timing contracts or HITL pass criteria without explicit approval
- Renaming `EditManager`, `NoteEditSession*`, `NoteEditKind`, or CAP/HITL strings that mean the **feature**
- New top-level domain nouns without user approval (see naming rules)
- Moving capture/stop/persistence ownership

---

## Pre-implementation review

### Ready

- `EditManager` is documented session owner (`EditManager.h`)
- Fader outbound math largely in `Utils/NoteEditFader*`
- Geometry apply in `NoteMovementUtils`
- HITL scenarios: `edit_minimal`, `note_edit_select_dependent_faders`, fader motor probe

### Resolved (proposed — confirm before Phase 1)

| Topic | Decision |
|-------|----------|
| Split before rename | **YES** — rename is Phase 5 sub-step only |
| Session owner | **`EditManager`** unchanged |
| Control surface scope | Ingress/egress + motor pipelines; **no** edit store mutations |
| `LoopEditManager` nesting | **Remove** from control-surface type; peer/global like today’s `editManager` |

### Open before coding

1. **Control-surface type name** — candidates below; lock at Phase 5
2. **`EditManager` ↔ control surface link** — inject adapter reference vs narrow callback interface (avoid global if possible)
3. **`lengthEditingMode`** — session flag on `EditManager` vs `NoteEditSessionState` (today split across both)
4. **Archive doc policy** — mechanical replace in `docs/plans/*` vs leave historical

### Proceed?

**NO** until Phase 0 boundary table is user-approved.

---

## Phase 0 — Boundary design (doc-only)

- [ ] Inventory every `noteEditManager.*` call site (`rg noteEditManager` — ~109 files mention type; ~20 production call sites)
- [ ] Classify each method: **surface** | **edit action** | **edit read** | **loop edit** | **mixed**
- [ ] Post architecture gate (ownership + transitions per phase)
- [ ] User approves boundary table + phase order

**Architecture gate (whole change):**

| Question | Answer |
|----------|--------|
| Ownership change? | **YES** — edit actions/reads move to `EditManager`; surface owns motor I/O only |
| State transition change? | **NO** — same user-visible NOTE_EDIT transitions |
| Behavior-preserving? | **YES** per phase |

---

## Phase 1 — Edit mutations on `EditManager`

Move methods that mutate session store / focus; keep thin delegate at old call sites temporarily if needed.

| Move from `NoteEditManager` | To |
|-----------------------------|-----|
| `deleteSelectedNote` | `EditManager` |
| `moveNoteToPosition` | `EditManager` (still delegates `NoteMovementUtils`) |
| `changeNoteEndWithOverlapHandling` | `EditManager` |
| `cycleEditMode` / `cycleEditSession` | `EditManager` or `EditNoteState` routing |

Update: `MidiButtonActions`, `BarStepButtonHandler`, `MidiFaderActions` handlers to call `editManager` where appropriate.

**Tests:** `pio test -e native` (`test_edit_apply`, `test_note_edit_session_undo`, …)

---

## Phase 2 — Edit UI reads on `EditManager` / `SelectNavigation`

| Move from `NoteEditManager` | To |
|-----------------------------|-----|
| `buildSelectNavigationSlots` | `EditManager` or `SelectNavigation` free helpers on `EditManager` |
| `selectableDisplayNotesForEditUi` | `EditManager` |
| `syncReferenceStepFromSelectedTick` | `EditManager` session helper |

Update: `EditManager`, `EditSelectNoteState` — remove read-path dependency on control surface.

---

## Phase 3 — Length-mode + encoder routing

| Today | Target |
|-------|--------|
| `lengthEditingMode` on `NoteEditManager` | `EditManager` or `NoteEditSessionState` (single source) |
| `toggleLengthEditingMode`, `resetLengthEditingModeOn*` | `EditManager` |
| `processEncoderMovement` | Route through `EditManager::onEncoderTurn` / states |

Eliminate `EditManager` → `noteEditManager.isLengthEditingMode()` round-trips.

---

## Phase 4 — Control-surface extraction

Extract remaining **ingress/egress** into a dedicated type (working name: control-surface adapter):

- MIDI note/CC/pitchbend routing to fader handlers
- `handle*FaderInput`, ignore windows, outbound pipeline (`requestFaderOutbound`, `processFaderOutbound`, dependent snapshots)
- `prepareNoteEditSessionOpen`, `sendNoteEditSessionFaderFeedback`, `scheduleNoteSelectFaderSync`, `scheduleSelectDependentMotorSync`
- `update()` tick for outbound queues
- Wiring: `main.cpp` `setFaderProcessor` / `setDisplayManager`

**Does not own:** `MidiButtonManager` / `MidiFaderManager` (already separate) unless consolidation is clearly simpler.

Introduce narrow **`EditManager` → surface** port for “refresh motors after selection/geometry” instead of scattered global calls.

**Tests:** `pio test -e native`; HITL `edit_minimal`, `note_edit_select_dependent_faders`

---

## Phase 5 — `LoopEditManager` unnest + rename (sub-step)

### 5a — Unnest `LoopEditManager`

- Move `loopEditManager` out of control-surface type to peer global or `TrackManager`-adjacent owner (match `editManager` pattern)
- Update: `EditManager`, `TrackUndo`, `Track`

### 5b — Rename control-surface type (hygiene sub-step)

Lock **one** name before mechanical rename:

| Candidate | Global instance |
|-----------|-----------------|
| `MidiControlSurfaceManager` | `midiControlSurfaceManager` |
| `ControlSurfaceManager` | `controlSurfaceManager` |
| `MidiSurfaceManager` | `midiSurfaceManager` |

- Rename files, class, include guard, `extern` global
- Update live Guides (`CODE_STRUCTURE`, `FADER_STATE_SYSTEM`, `AGENT_CONTEXT_MAP`)
- Leave `openspec/changes/archive/*` historical unless links break

**Tests:** `pio test -e native`; `pio run -e teensy41-capture-serial`; optional HITL fader motor probe

---

## Phase 6 — Doc + hygiene closeout

- [ ] Update [`codebase_hygiene_technical_debt_review.md`](codebase_hygiene_technical_debt_review.md) item 3 → Done
- [ ] Update [`runtime_process_building_blocks_overview.md`](runtime_process_building_blocks_overview.md) Input + Note edit sections
- [ ] `CURRENT_WORK` / `PROJECT_STATE`

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

## Verification gates

| Phase | Gate |
|-------|------|
| 1–3 | `pio test -e native` |
| 4 | + HITL `edit_minimal` |
| 5 | + `note_edit_select_dependent_faders` or motor probe |
| 6 | Docs + hygiene checklist |

---

## Risk notes

- **Fader feedback regressions** — highest risk in Phase 4; use archived OpenSpec `note-edit-fader-feedback-regression` BUG as checklist
- **Bidirectional coupling** — `EditManager` ↔ surface must be one-directional after Phase 4
- **Large diff** — one phase per session preferred (OpenSpec phase-gate style)
