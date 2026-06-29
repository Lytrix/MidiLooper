# Architecture rules

Contract for agents and humans: who owns what, how modules are named, what is forbidden, and when a new abstraction is justified.

Authority: subordinate to [PROJECT_INTENT.md](PROJECT_INTENT.md); overrides guides, plans, and ad-hoc code patterns.

Module naming reference: [Guides/CODE_STRUCTURE.md](../Guides/CODE_STRUCTURE.md). Domain vocabulary: `.cursor/rules/Naming-Vocabulary-Teensy-Looper.mdc`.

**Default:** [continue implementation](#progress-bias-and-decision-ladder). Governance detects **structural** change — not approval for normal extension work.

---

## Progress bias and decision ladder

### Principle

**Continue implementation** unless a [formal reassessment trigger](../ARCHITECTURE_REASSESSMENT.md#formal-triggers) fires. Assume **extension before redesign**.

Do **not** stop merely because architecture feels unclear, multiple solutions exist, code seems old, or helper duplication is *suspected*. Instead: propose the preferred path, state confidence, implement.

### Decision ladder (every task)

| Step | Question | Action |
|------|----------|--------|
| 1 | **Existing owner?** | **Extend** — add method/state on owner |
| 2 | **Existing extension point?** | **Reuse** — Actions, EditState, policy, spec’d API |
| 3 | **Historical decision applies?** | **Follow** — [DECISION_LOG.md](../DECISION_LOG.md); supersede only via reassessment |
| 4 | **[Formal trigger](../ARCHITECTURE_REASSESSMENT.md#formal-triggers) fired?** | **Reassess** — pause; max [one reassessment per session](../ARCHITECTURE_REASSESSMENT.md#session-limit) |
| 5 | Otherwise | **Implement** |

Lightweight checks (PROJECT_STATE, CURRENT_WORK, skim DECISION_LOG) are **not** stops — they inform the ladder.

---

## Ownership rules

Each row is the **single owner** for lifecycle and mutation of that scope. Callers request operations through the owner; they do not reach into peer internals.

| Scope | Owner | Owns | Does not own |
|-------|-------|------|--------------|
| **Track** | `Track` | 8 loop slots, per-slot MIDI (`Loop`, `LoopPasses`, `LoopEventStore`), jam region ticks, slot playback/record state, note cache invalidation | Global transport mode, SD I/O, display pixels, button gesture detection |
| **TrackManager** | `TrackManager` | Track array, selected track, record/play/overdub orchestration, quantized slot switching, `updateAllTracks(currentTick)` | Loop MIDI storage schema, undo snapshot format, overlay UI layout |
| **StorageManager** | `StorageManager` | SD serialization, deferred save FSM, Current workspace, Set revision catalog, load/save overlay **policy hooks** (`SetBrowserOverlayPolicy`) | Live loop mutation during capture, transport timing, piano-roll rendering |
| **ClockManager** | `ClockManager` | 192 PPQN tick, MIDI clock in/out, internal fallback, bar/beat derivation | Track record arm logic, edit session state |
| **Display** | `DisplayManager` | OLED/LCD draw calls, piano roll layout, track strip, load/save overlay **presentation** | Business decisions (what to save, when to commit a pass); reads state via getters |
| **UI state (global mode)** | `LooperStateManager` | `LooperState` enum (idle/record/play/overdub/edit/settings), edit overlay context, load/save overlay active flag, quantized transition queue | Per-track slot data, MIDI event storage |
| **Note edit session** | `EditManager` + `NoteEditManager` | Live `EditSession`, `NoteEditSession.store`, focus/overlap notes, `EditStates/*` FSM | Committed `editPass` rows (owned by `Loop` until `saveNoteEditPass` / `closeNoteEditPass`) |
| **Undo (global)** | `TrackUndo` on each `Track` | Capture-pass undo, note-edit-pass undo, clear/loop-start snapshots | In-session edit undo stack inside `EditManager` |
| **Persistence admission** | `StorageManager::requestDeferredSaveState` + idle drain in `main.cpp` | When and how Current workspace hits SD | Hot-path record/overdub stop (no full validate on stop) |
| **Input (MIDI buttons)** | `MidiButtonManager` → `MidiButtonProcessor` + `MidiButtonActions` | Gesture detection and action dispatch | Direct `Track` field mutation outside action paths |
| **Input (GPIO)** | `GpioButtonManager` (when wired) | Same action vocabulary as MIDI buttons | Parallel duplicate action implementations |
| **LED feedback** | `MidiLedManager` | Controller LED updates from track/slot/transport state | Display rendering |

**Cross-cutting:** `MidiHandler` routes MIDI I/O; it does not own domain state. `main.cpp` wires update order (clock → tracks → display → deferred save).

Ownership is **not frozen** — it may evolve via the [Ownership transfer protocol](#ownership-transfer-protocol) below. Adapters and shadow owners are not a shortcut around that protocol.

---

## Ownership transfer protocol

Strict ownership prevents drift; **controlled transfer** prevents freeze-by-workaround (permanent adapters, duplicated state, shadow Managers).

### When transfer is allowed

Ownership changes **only** through all of:

| Gate | Artifact |
|------|----------|
| **Architecture reassessment** | [ARCHITECTURE_REASSESSMENT.md](../ARCHITECTURE_REASSESSMENT.md) — problem, alternatives, migration, recommendation |
| **Migration plan** | OpenSpec `design.md` and/or `tasks.md` slice, or handoff section with explicit steps |
| **Compatibility layer** | Documented temporary delegation or adapter (see [Compatibility rules](#compatibility-rules)) |
| **Removal schedule** | Defined removal trigger — date, milestone, or task ID when compat code goes away |
| **User approval** | Human confirms before production implementation |
| **DECISION_LOG** | Structured entry (`DEC-###`); supersede old owner row in prose if scope moved |

**No transfer** from: OpenSpec `tasks.md` alone, chat agreement, or “temporary” stubs without removal trigger.

### Required proposal

When changing ownership, complete [OWNERSHIP_TRANSFER.md](../templates/OWNERSHIP_TRANSFER.md) or include in reassessment output:

| Field | Content |
|-------|---------|
| **Current owner** | Module/class today |
| **Target owner** | Module/class after transfer |
| **Reason** | Why extension on current owner failed |
| **Migration strategy** | Phased steps; callers updated in which order |
| **Temporary compatibility** | Adapter/delegation/shim — or “none” |
| **Removal trigger** | When compat is deleted (task, milestone, max lifetime) |
| **Validation approach** | Native tests, HITL, overlay manual — per slice |

### Migration rules

| Allowed | Forbidden |
|---------|-----------|
| Temporary delegation (target calls owner until callers migrated) | Parallel **permanent** ownership of same mutable state |
| Temporary adapters at module boundaries | Duplicated state (second dirty flag, second overlay index, second pass timeline) |
| Versioned migration (SD format bump with read-old/write-new) | Silent reassignment (code moves without doc + log) |
| Feature flag or compat API with **removal trigger** | “Temporary” Manager that becomes de facto owner |

### Compatibility rules

Any temporary compatibility layer **must** document:

| Field | Example |
|-------|---------|
| **Creation date** | 2026-06-29 |
| **Removal condition** | “Delete when `set-revision-persistence` task 4.2 ships” |
| **Maximum lifetime** | “2 sessions” or “one OpenSpec archive” — not open-ended |

Compat code lives in the **target owner** or a single named `*Compat` type in the same PR scope — not scattered helpers.

### Agent rule (before introducing ownership)

1. **Search existing owner** — [ownership table](#ownership-rules), `rg` for mutations on the scope  
2. **Search extension points** — Actions, policy, EditState, public method on owner  
3. **Search migration options** — delegation vs move vs versioned SD  
4. If transfer needed → reassessment + [OWNERSHIP_TRANSFER.md](../templates/OWNERSHIP_TRANSFER.md) → approval → implement  
5. If extending current owner suffices → document in PREFLIGHT; no transfer proposal  

Do **not** add permanent adapters, shadow Managers, or duplicate fields to avoid reassessment.

---

## Naming rules (suffix meaning)

Use these suffixes consistently. Do not invent parallel nouns (`Service`, `Facade`, `Helper`) without architecture review.

| Suffix | Meaning in this repo | Examples |
|--------|----------------------|----------|
| **Manager** | Owns a domain or coordinates subcomponents over a lifecycle | `TrackManager`, `ClockManager`, `DisplayManager`, `StorageManager` |
| **Controller** | *Not used as a class suffix today.* Prefer **Manager** or **Handler**. | — |
| **Handler** | Receives events and routes or processes them (often single-purpose) | `MidiHandler`, `BarStepButtonHandler` |
| **Processor** | Transforms raw input into detected events (stateless or thin state) | `MidiButtonProcessor`, `MidiFaderProcessor` |
| **Action** / **Actions** | Executes a domain operation in response to a detected gesture or fader move | `MidiButtonActions`, `MidiFaderActions` |
| **State** | FSM state object or small state machine (UI/edit), not global app mode | `EditNoteState`, `SlotStateMachine`, `ClockSourceStateMachine` |
| **Repository** | *Not used.* Persistence is `StorageManager` + `StorageLoopIo`; in-memory timeline is `Loop` / `LoopPasses`. | Do not introduce `*Repository` without review. |

Global app mode uses the enum name **`LooperState`**; the manager class is **`LooperStateManager`** — do not conflate them.

---

## Forbidden patterns

| Pattern | Why forbidden | Instead |
|---------|---------------|---------|
| **Duplicated ownership** | Two modules mutating the same slot/loop/pass field | Route through owner, or [ownership transfer protocol](#ownership-transfer-protocol) |
| **Permanent adapter / shadow owner** | “Temporary” layer that never leaves | Transfer protocol + removal trigger, or extend existing owner |
| **Helper extraction without justification** | Sprawl of `*Utils` that hide ownership moves | Extend the owner or use an existing util (`NoteUtils`, `LoopStopFinalize`) |
| **Persistence side effects on hot paths** | Record/overdub stop must stay bounded | `requestDeferredSaveState`; drain in idle — see [LOOP_MIDI_STORAGE_AND_VALIDATION.md](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) |
| **Display writes from business logic** | Couples timing to SSD1322 | Set flags/getters; `DisplayManager` reads in `update()` |
| **Direct cross-manager mutation** | e.g. `MidiButtonActions` writing `StorageManager` private statics without API | Call public `StorageManager` / policy methods |
| **Full flatten on stop** | Breaks pool-budget and stop-path latency | `finalizeLoopAtStop`, `mergeActiveCapturePasses`, `LoopPasses::materialize` for read paths |
| **Shallow undo snapshots** | Corrupts chunk-backed storage | `shareForSnapshot()` + `restoreFromSnapshot` always `cloneShared()` |
| **New top-level domain nouns** | Naming drift | Reuse vocabulary in Naming-Vocabulary rule; ask user if no fit |

---

## New abstraction checklist

Before adding a class, module, or free function file, answer:

| Question | Pass criteria |
|----------|---------------|
| **Existing owner?** | Identified; change is a method or nested type on that owner |
| **Extension point available?** | `Actions`, `EditStates`, policy class, or OpenSpec spec delta — not a parallel pipeline |
| **State duplicated?** | No second copy of tick, slot index, overlay mode, or pass timeline |
| **Lifecycle impact?** | Entry/exit documented; no hidden transition on unrelated call paths |
| **Test impact?** | Native test or HITL scenario identified; `pio test -e native` for logic |

If answers are clear and **no** [formal trigger](../ARCHITECTURE_REASSESSMENT.md#formal-triggers) fired → implement (brief preflight inline is enough).

If a formal trigger fired or ownership is ambiguous → [PREFLIGHT.md](../templates/PREFLIGHT.md), [DECISION_REVIEW.md](../templates/DECISION_REVIEW.md), [ARCHITECTURE_REASSESSMENT.md](../ARCHITECTURE_REASSESSMENT.md).

New architecture decisions **must** be recorded in [DECISION_LOG.md](../DECISION_LOG.md) (append; supersede — never delete).

---

## Storage and undo (non-negotiable)

Read before touching loop storage, stop paths, or undo: [Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md).

Summary:

- **Stop path:** `finalizeLoopAtStop` + wrap window only — not full `validateAndCleanupMidiEvents`.
- **Undo routing:** NoteEditSession undo before global stack; capture-pass undo via `RecordPassAdded` / `OverdubPassAdded`; `NoteEditPassClosed` before clear-slot undo when applicable.
- **After flat edits:** `invalidateCaches()` on the owning `Track`.
