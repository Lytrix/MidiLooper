# Naming authority

Architectural vocabulary for MidiLooper — a **ubiquitous language** that documentation and code converge toward over time.

**Authority:** subordinate to [PROJECT_INTENT.md](PROJECT_INTENT.md) and [ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md); overrides guides, plans, and ad-hoc code patterns for terminology.

**Investigation and naming debt:** one-time audit and migration roadmap live in [`docs/plans/architecture_naming_authority_refinement.md`](../plans/architecture_naming_authority_refinement.md). This document stays timeless — no usage counts or time-stamped debt tables here.

---

## Naming philosophy

1. **Naming is architecture** — vocabulary communicates responsibilities, not implementation mechanics.
2. **One concept, one term** — avoid synonyms for the same architectural idea.
3. **Action + scope** — identifiers name what code does and what it applies to (`commitCapturePass`, `filterSelectableDisplayNotes`).
4. **Responsibility over implementation** — files and modules describe *what they own*, not *how they work internally*.
5. **Predictable over clever** — reuse established repo nouns before inventing new ones.
6. **Incremental convergence** — adopt preferred terms when touching a subsystem; no repository-wide rename-only commits.

Global identifier shape rules (no abbreviations, plans/docs naming): user-level `naming-and-terminology` Cursor rule. This document owns **domain vocabulary** and **architectural concept boundaries**.

---

## Concept boundaries

Each term below defines **architectural meaning**. Preferred words are consequences of the meaning, not arbitrary style choices.

| Term | Architectural meaning | Not this |
|------|----------------------|----------|
| **Playback** | Runtime playback processing — cursor, ledger, merged MIDI events, window materialization | Transport boolean or slot FSM state |
| **Playing** | Transport or slot state — `isPlaying()`, `LOOPER_PLAYING` | Runtime playback machinery |
| **Selection** | Logical note selection — `EditorSelection`, bracket target, selected note set | Navigation between notes; edit-mode enum |
| **SelectNavigation** | Navigation between selectable notes — `buildSelectNavigationSlots`, tick mapping | The selection model itself |
| **Select** (edit kind) | Edit-mode enum value — `NoteEditKind::Select` | Selection model or navigation |
| **Geometry** | Note timing, length, pitch, and overlap relationships during edit — `EditedGeometry`, constrained resolution | Motor or fader position; MIDI song position |
| **Position** | Hardware or protocol position — fader motor, `sendSongPosition`, GPIO position buttons | Note-edit geometry |
| **State** | Mutable ownership — live session, transport mode, persistence queue fields | Immutable captured copies |
| **Snapshot** | Immutable captured state — undo clones, display probes, activity snapshots | Live mutable owner |
| **Deferred** | Work intentionally postponed to idle or budget drain — `processDeferredSaveState` | Queued user intent; synchronous algorithms |
| **Pending** | Queued intent not yet committed — `hasPendingCapturePass`, `pendingOutboundTrigger_` | Idle-deferred maintenance work |
| **Scheduled** | Work placed on an explicit schedule or queue for later execution — `PersistenceWorkQueue` | Synchronous resolution; generic “later” |
| **Queue** | Actual pending work held for asynchronous processing | Synchronous multi-step algorithms |
| **Resolution** | Deterministic synchronous conflict solving — geometry constraint analysis → actions | Runtime scheduling; pipeline or queue metaphors |
| **Pipeline** | Formal multi-stage processing where **each stage has independent responsibility** and stages may be async or budget-sliced | Sequential synchronous algorithms (use **Resolution**) |
| **Outbound** | Note-edit fader motor and Droid feedback path — distinct from MIDI Output | Generic MIDI egress |
| **Input** / **Output** | MIDI and USB routing — `MidiHandler` ingress and egress | Control-surface motor path (Outbound) |
| **Manager** | Domain owner coordinating lifecycle over a scope | Controller (not used); thin routing (Handler) |
| **Capture** | Live record or overdub append buffer until stop | Committed timeline rows |
| **pass** / **passes** | Bounded capture or edit stretch ending in commit; canonical timeline on `LoopPasses` | Moving note range; single-note focus lifecycle |
| **editPass** | One `saveNoteEditPass()` row in `passes.editPasses[]` | Live edit session store |
| **merge** | Replay active capture passes into a working view | Full timeline replay (use **materialize**) |
| **materialize** | Full pass replay to a MIDI event vector | Active capture merge (use **merge**) |

---

## Legitimate architectural terms

Some words recur because they name real concepts. **Do not eliminate them** — use them only when the meaning matches.

### Pipeline

Use when describing a formal multi-stage processing algorithm where each stage has an **independent responsibility** and stages may run asynchronously or across budget slices.

Do **not** use because several functions execute sequentially in one call stack. Synchronous deterministic algorithms are **Resolution**, not Pipeline.

### Snapshot

Use for **immutable captured state** — undo clones (`shareForSnapshot`), display probes, activity snapshots.

Prefer **State** for the **mutable owner** of live data.

### Queue

Reserve for **actual pending asynchronous work** — items waiting to be drained on a later tick or idle slice.

Do not use for synchronous algorithms that run to completion in one call.

### Deferred, Pending, Scheduled

These are **intentionally distinct** (see concept boundaries). Do not collapse into one umbrella term.

---

## Identifier rules

1. **Action + scope** — at least one verb or operation word and one domain object (`saveNoteEditPass`, `rebuildNoteEditFocusFromStore`).
2. **Reuse domain nouns** from this document — Capture, passes, editPass, NoteEditSession, DisplayNote, NoteRef, overlap note, SelectNavigation, focus, moving note.
3. **No new top-level domain nouns** without explicit user approval — propose term, why existing words fail, one-sentence rationale; record in [DECISION_LOG.md](../DECISION_LOG.md) as `DEC-###`.
4. **No abbreviations** in new identifiers.
5. **No metaphor imports** — avoid audible, view, inventory, wire, golden in new names unless already established in this document.
6. **Describe, don’t invent** — if the object already has a name, use it in prose instead of coining a synonym type.

Prefer **Type** over **Kind** for new enums when the repo already uses **Type** elsewhere.

---

## Module suffix vocabulary

Use these suffixes consistently. Do not invent parallel nouns (`Service`, `Facade`, `Helper`) without architecture review.

| Suffix | Meaning in this repo | Examples |
|--------|----------------------|----------|
| **Manager** | Owns a domain or coordinates subcomponents over a lifecycle | `TrackManager`, `ClockManager`, `DisplayManager`, `StorageManager` |
| **Controller** | *Not used as a class suffix.* Prefer **Manager** or **Handler**. | — |
| **Handler** | Receives events and routes or processes them (often single-purpose) | `MidiHandler`, `BarStepButtonHandler` |
| **Processor** | Transforms raw input into detected events (stateless or thin state) | `MidiButtonProcessor`, `MidiFaderProcessor` |
| **Action** / **Actions** | Executes a domain operation in response to a detected gesture or fader move | `MidiButtonActions`, `MidiFaderActions` |
| **State** | FSM state object or small state machine (UI/edit), not global app mode | `EditNoteState`, `SlotStateMachine`, `ClockSourceStateMachine` |
| **Repository** | *Not used.* Persistence is `StorageManager` + `StorageLoopIo`; in-memory timeline is `Loop` / `LoopPasses`. | Do not introduce `*Repository` without review. |

Global app mode uses the enum **`LooperState`**; the manager class is **`LooperStateManager`** — do not conflate them.

Module layout reference: [Guides/CODE_STRUCTURE.md](../Guides/CODE_STRUCTURE.md).

---

## Verb conventions

| Verb prefix | Architectural meaning | Examples |
|-------------|----------------------|----------|
| **handle*** | Hardware or MIDI ingress routing | `handleMidiNote`, `handleCoarseFaderInput` |
| **process*** | Poll, defer, or drain work queues | `processDeferredSaveState`, `processPendingPresses` |
| **apply*** | Commit domain mutations to live state | `applyEditSessionActions`, `applyGeometryKindFromControl` |
| **resolve*** | Synchronous constraint solving | `resolveAllConstrainedGeometry`, `resolveConstrainedGeometry` |
| **update*** | Refresh derived views or per-frame tick | `DisplayManager::update`, `updateAllTracks` |
| **commit*** | Finalize a bounded pass or edit batch | `commitCapturePass`, `commitEditAction` |
| **request*** / **admit*** | Queue persistence or admission intent | `requestDeferredSaveState`, `admitLoopPersist` |
| **invalidate*** / **rebuild*** | Cache coherence | `invalidatePlaybackMergedMidiEvents`, `rebuildNoteEditFocusFromStore` |
| **run*** | Orchestrate a multi-step synchronous algorithm | Prefer `run*Resolution` over `run*Pipeline` for sync conflict solving |

---

## Domain vocabulary (Loop slot)

### Scope words

| Scope | Meaning |
|------|---------|
| **Loop** | Slot container |
| **Capture** | Live record/overdub append buffer |
| **passes** | **LoopPasses** — **recordPass**, **overdubPasses[]**, **editPasses[]** |
| **editPass** | One **saveNoteEditPass** row in **passes.editPasses[]** |
| **EditChange** | One atomic change inside an **editPass** |
| **pass** | One bounded capture or edit stretch ending in commit |

**Do not** use **pass** for: moving note tick range (**moving note range** on focus), or single-note **focus** lifecycle.

### Pass types (audio/MIDI time)

| Pass | Ends with | Global undo (when applicable) |
|------|-----------|-------------------------------|
| **recordPass** | `commitCapturePass()` → **recordPass** in **passes** | **RecordPassAdded** |
| **overdubPass** | `commitCapturePass()` → **overdubPass** in **passes** | **OverdubPassAdded** |
| **noteEditPass** | `closeNoteEditPass()` (boundary flush) | **NoteEditPassClosed** |
| **controlChangeEditPass** | (future) | **ControlChangeEditPassClosed** (stub) |

A **noteEditPass** batch may contain multiple **editPass** rows sharing **noteEditPassIndex**.

### Types and ids

| Name | Role |
|------|------|
| **EditSessionType** | Live edit session on **EditSession** (`Loop` \| `Note` \| `ControlChange`) |
| **EditPassType** | Stored domain on **editPass** (`Note` \| `ControlChange` \| `Audio`) |
| **EditActionType** | Stored action on **editPass** (`Create` \| `Update` \| `Delete`) |
| **EditPropertyType** | Updated field on **editPass** (`Pitch`, `Length`, `StartTick`, `EndTick`, `Tick`, `Value`, `None`) |
| **EditPassId** / **PassId** | Stable id per pass row (unified **nextPassId_**) |
| **NoteRef** | Stable note target in an **EditChange** |

### Edit sessions (RAM scope)

| Scope | UI domain |
|------|--------|
| **EditSession** | Live edit RAM owner on **EditManager** (`sessionType`, `store`, focus, undo stack) |
| **LoopEditSession** | Loop length edit — future |
| **ControlChangeEditSession** | CC edit UI — future |
| **JamSession** / **PerformanceSession** | Playback/jam UI — **TBD** |

### Action + scope examples

| Name | Action | Scope |
|------|--------|-------|
| `commitCapturePass()` | commit | pending capture pass → **passes** |
| `saveNoteEditPass()` | save | **editPass** row |
| `closeNoteEditPass()` | close | note edit pass batch |
| `LoopPasses::materialize()` | materialize | **passes** |
| **RecordPassAdded** | undo | **recordPass** |
| **OverdubPassAdded** | undo | **overdubPass** |
| **NoteEditPassClosed** | undo | closed **noteEditPass** batch |

### Merge, materialize, and memory tiers

| Concept | Use | Avoid |
|------|-----|-------|
| Active capture pass replay | **merge** (`mergeActiveCapturePasses`, `mergeMaterializedPassesWithCapture`) | flatten |
| Full pass replay to MIDI vector | **materialize** (`materializeToEventVector`) | flat / flatten |
| Internal heap (malloc/new) | **internal heap** (`InternalHeapFirstAllocator`, `getInternalHeapFreeBytes`) | RAM2 in new identifiers |
| External memory pool (EXTMEM/PSRAM) | **external memory pool** (`ExternalMemoryFirstAllocator`, `isInExternalMemoryPool`) | PSRAM in new identifiers |

Platform/SDK symbols that must stay unchanged behind shims are allowed (for example: `extmem_malloc`, `extmem_free`, `external_psram_size`).

### Note edit overlap (moving note + overlap notes)

| Term | Role |
|------|------|
| **moving note** | Note being edited (`MovingNoteIdentity`; fader-1 selection) |
| **overlap note** | Any other note the moving note edit affects (hide, shorten, restore) |
| **OverlapNote** | One tracked overlap note on **focus** |
| **overlapNotes** | Collection on **focus** — impacted overlap notes only (not `baselineMap`) |
| **OverlapNoteStoreState** | **Visible** \| **Hidden** \| **Shortened** on each **OverlapNote** |

**Relationship qualifiers:** **overlapping**, **inner**, **adjacent**, **contained** — see [MOVE_NOTE_LOGIC.md](../Guides/MOVE_NOTE_LOGIC.md).

### Geometry resolution (preferred term)

Note-edit geometry changes run a **deterministic synchronous resolution algorithm**:

1. **Validation** — session state, focus, loop length, note ID preparation
2. **Preparation** — changed notes, evaluation scope, baseline projection
3. **Analysis** — eligible pairs, interaction analysis, grouping
4. **Resolution** — constraint solving, action generation
5. **Commit** — apply actions, refresh caches

Preferred orchestration name: **`runEditSessionGeometryResolution`**. The legacy name `runEditSessionGeometryPipeline` is naming debt — migrate on next note-edit geometry refactor. See investigation doc § Geometry resolution.

### Note edit read path

| Need | Use | Avoid |
|------|-----|-------|
| MIDI pairs during edit | **NoteEditSession.store** / `editAwareMidiEvents()` | “audible”, “live layer” |
| Reconstructed notes | **`NoteUtils::reconstructNotes`** → **`DisplayNote`** | `audibleNotes`, `sessionNotes` |
| Fader-1 + display list excluding Hidden overlap | **`filterSelectableDisplayNotes`** | `NoteEditSessionView`, `selectableNotes()` as a type |
| Bracket / delete target | **`NoteRef`** + select navigation index | cache index without **NoteRef** |

Prefer helpers on **`NoteEditFocus`** or **`SelectNavigation`** — not new `*View` / `*Inventory` types without approval.

### UI rename (shipped)

- `class EditState` → **`EditNoteState`** (note-edit FSM base)
- **`EditState`** on **EditPass** = Active \| Disabled (parallel **CapturePassState**)

### Words to avoid in new names

**segment**, **gesture**, **working**, **EditOp**, **published** (use **committed**), **victim**, **covered** (use **hidden**), **neighbor** / **neighbor note** (use **overlap note**), **ledger** for overlap scratch (use **overlapNotes**), **flat** in new identifiers (use **loop MIDI events**, **session store events**, or **Takes + Edits replay**), **footprint** (use **moving note range** on **NoteEditFocus**), **span** for edit batches (use **edit pass**), **layer** for edit batches (use **edit pass**; **layer** = Take/overdub performance), new abbreviations.

**Flow**, **Dispatch**, and **Pipeline** suggest scheduling or runtime execution — do not use for synchronous conflict resolution.

---

## File and folder naming

| Pattern | Example |
|---------|---------|
| `{Domain}{Role}.cpp` | `TrackManager.cpp`, `MidiHandler.cpp` |
| `{Action}{Scope}.cpp` | `ApplyEditSessionActions.cpp` |
| `{Scope}{Policy}.cpp` | `BootRecoveryPolicy.cpp` |
| Subfolder = TU extraction | `DisplayManager/PianoRollDraw.cpp`, `StorageManager/RevisionLoad.cpp` |

Headers live in `include/`; implementations in `src/`. Filenames describe **responsibility**, not implementation detail.

---

## Introducing new terminology

Before adding a new architectural term:

1. Check this document — concept boundaries and domain vocabulary.
2. Reuse an existing architectural term.
3. Avoid introducing synonyms for concepts already named.
4. Introduce a new top-level vocabulary word only when genuinely required (no existing term fits).
5. Document approved additions here and append **`DEC-###`** to [DECISION_LOG.md](../DECISION_LOG.md).

---

## Migration policy

| Rule | Detail |
|------|--------|
| No rename-only PRs | Never repository-wide symbol renames without accompanying functional change |
| New modules follow this document | All new code and docs use preferred terms immediately |
| Touch-and-rename | Adopt preferred names when a subsystem undergoes significant refactor |
| Serial capture tokens frozen | `#CAP`, `PERS`, `REVT` — never rename |
| Plan glossary promotion | When a plan glossary term stabilizes, promote it here; mark plan section superseded |
| Investigation doc for debt | Current-vs-preferred mapping and timing live in the investigation doc, not here |

---

## Architecture review checklist

Use during code review, OpenSpec implementation review, and before merge:

- [ ] No unnecessary new terminology introduced
- [ ] Existing architectural vocabulary reused (check concept boundaries above)
- [ ] File names describe responsibilities, not implementation mechanics
- [ ] Public APIs follow established verb conventions
- [ ] This document updated if a new architectural concept is approved
- [ ] Pipeline / Queue / Flow not used for synchronous deterministic algorithms (use **Resolution**)
- [ ] New top-level domain nouns have user approval and `DEC-###` entry

Referenced from [docs/agents/reviewer.md](../agents/reviewer.md).

---

## Conflict resolution

| Conflict | Resolution |
|----------|------------|
| Plan glossary vs this document | **NAMING.md wins** — update plan or run reassessment |
| Guide vs this document | **NAMING.md wins** for vocabulary; update guide after deliberate term change |
| Code vs this document | Fix code on next subsystem refactor, or update this document via `DEC-###` if term was wrong |

---

## Related documents

| Document | Role |
|----------|------|
| [ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md) | Ownership, forbidden patterns, extension rules |
| [Guides/CODE_STRUCTURE.md](../Guides/CODE_STRUCTURE.md) | Module map and input stack layout |
| [Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) | Capture, passes, stop path, undo |
| [architecture_naming_authority_refinement.md](../plans/architecture_naming_authority_refinement.md) | Investigation audit, naming debt roadmap |
| [refactor_priority_backlog.md](../plans/refactor_priority_backlog.md) | Cross-cutting refactor priority index (P1/P2/P3) |
| Archived OpenSpec baselines | `m8-rename`, `m8-edit`, `pool-budget` in `openspec/specs/` |
