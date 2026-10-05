# Naming authority

Architectural vocabulary for MidiLooper — a **ubiquitous language** that documentation and code converge toward over time.

**Authority:** subordinate to [PROJECT_INTENT.md](PROJECT_INTENT.md) and [ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md); overrides guides, plans, and ad-hoc code patterns for terminology.

**Investigation and naming debt:** one-time audit and migration roadmap live in [`docs/Plans/architecture_naming_authority_refinement.md`](../Plans/architecture_naming_authority_refinement.md). This document stays timeless — no usage counts or time-stamped debt tables here.

---

## Short names beside IDs in chat

In every user-facing commentary and final reply, pair each stage, task, packet, gate or decision ID
with a short descriptive name: `S2b — boot/load admission`, `P1 — boot/admission slice`,
`G2 — implementation contract`, `DEC-046 — round-robin playback admission`.
Apply this on every mention, including repeats, headings, bullets and link labels; never assume the user
remembers an ID from an earlier message. Names should usually be two to six words, drawn from the current
stage/task heading or decision title. Keep the canonical ID intact and use the same short name consistently.
For grouped IDs, label each one or give a shared description only when it accurately covers the whole range.
If the title is not known, look it up (search DECISION_LOG rather than reading the whole file); do not invent it.

This is a chat presentation rule, not an identifier rename. Preserve exact commands, paths, code, diagnostic
output and literal quotations; explain their IDs with short names in the adjacent prose when discussing them.

---

## Naming philosophy

1. **Naming is architecture** — vocabulary communicates responsibilities, not implementation mechanics.
2. **One concept, one term** — avoid synonyms for the same architectural idea.
3. **Action + scope** — identifiers name what code does and what it applies to (`commitCapturePass`, `filterSelectableDisplayNotes`, `selectableDisplayNotesForEditUi`).
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
| **Snapshot** | The act or role of capturing immutable state of some owner (undo snapshot, display snapshot, share-for-snapshot) | The Loop’s published complete state (that object is **LoopSnapshot**); live mutable owner |
| **LoopSnapshot** | Complete immutable effective-state object: LoopGeometry, allocator counters, a **span index** (pitch → canonical 768-tick bar → membership lists of **NoteRef**s for complete notes intersecting that bar), and an **event index** (bar → membership lists of **EventRef**s, an indexed view of existing event-stream order). `loopLengthTicks` on the snapshot root is the sole loop-boundary authority; note objects store identity and start/end geometry only. Logical content is completely determined by its own reference root — resolving a reference or answering a content query never consults a parent snapshot. Unchanged objects keep the same references; changed content allocates new immutable objects. Unchanged pitch/bar membership remains shared with the parent; only dirty membership and the minimum directory path to it are newly constructed. Absent until the first published snapshot. | A `LoopPass`; `PersistedLoopSnapshot`; a materialized MIDI vector; live Capture; raw C++ pointers; Tile / Lane / Bucket / Grid; a parent-delta chain |
| **Effective content** | The loop's canonical committed musical content — what the published **`LoopSnapshot`** represents, read through `Loop::copyEffectiveCommittedEvents*`. `LoopPasses::materializeToEventVector` is a test/oracle path only. | Compatibility `LoopPass` rows; a consumer's derived representation; a raw MIDI vector |
| **Published root** | The selected `LoopSnapshot` at its `playbackRevision` — `publishedSnapshot`, `publishedSnapshotIsCurrent`, `publishedSnapshotIdentity`. The previous root stays selected until a complete candidate is frozen and stored. | An unpublished candidate; a derived representation; transport state |
| **Compatibility gather** | Reconstructing content from `LoopPass` rows when no current published root exists. A fallback, never the normal source. | The snapshot source; a second content authority |
| **Derived representation** | A consumer-owned structure built from a selected `LoopSnapshot`, each with its own storage, revision, invalidation, and build policy (playback window/order, `visualCache`, analyze indexes, LED pads). See [DerivedViews.md](Architecture/DerivedViews.md). | The `LoopSnapshot` itself; a window or interval; a second content authority |
| **Bridge** | A compiled transition overlay between two coverage windows/cycles (`RuntimePlaybackBridge`, `FixedPlaybackBridgeStorage`): base order, insertions, and inherited held-note closures. Retains an original Off across a wrap and admits each changed pair at its first safe On. | A second content authority; a `LoopSnapshot`; a whole-loop rebuild |
| **Emission cursor** | Identity of an event **instance** emitted from a projection cycle — `EmissionCursor` (`projectionCycle`, `phase`, `samePhaseOrdinal`, `eventProvenance`, `effectiveSourceGeneration`). A source event alone is insufficient because it may fire again after a loop wrap. | `currentTick` (transport clock); an event scan index; a snapshot index |
| **Overlay** (playback) | A stale published root plus unpublished session / pending-publish passes applied on top for a consumer window (`tryGatherPublishedSnapshotEventsInWindow` Overlay path). Valid for playback and windowed paint fallback; rejected by idle visual-cache fill. | Publishing a prefix; a new snapshot; the current-root path |
| **Deferred** | Work intentionally postponed to idle or budget drain — `processDeferredSaveState` | Queued user intent; synchronous algorithms |
| **Pending** | Queued intent not yet committed — `hasPendingCapturePass`, `pendingOutboundTrigger_` | Idle-deferred maintenance work |
| **Scheduled** | Work placed on an explicit schedule or queue for later execution — `PersistenceWorkQueue` | Synchronous resolution; generic “later” |
| **Queue** | Actual pending work held for asynchronous processing | Synchronous multi-step algorithms |
| **Resolution** | Deterministic synchronous conflict solving | Runtime scheduling; pipeline or queue metaphors; a generic `Resolver` type |
| **NoteGeometryResolver** | Live NOTE_EDIT overlap Resolution (geometry → actions) | Loop history / playback effective-state queries |
| **LoopContentResolution** | Historical DEC-037 prototype name. New docs do not use it. Callers are **LoopSnapshot** consumers. | A current effective-state owner; `NoteGeometryResolver` |
| **PresentNote** | LCR query snapshot of notes whose span contains tick S (`resolveState` / `StateCheckpoints::presentAt`). Keyed by `NoteId`; many same-pitch rows allowed. Emitted type has no `endTick` | `ActiveNoteLedger::Entry` (one per `(channel, pitch)` at `currentTick`); `midiHandler.sendMidiEvent` |
| **ActiveNote** | Runtime slot at `currentTick` until NoteOff — `ActiveNoteLedger::Entry`, one per `(channel, pitch)`. No `length` / `endTick`. Written from **committed** playback (`playbackCursorAdvanceSend` → `applyPlaybackLedgerEvent`). Capture overdub emit (`playbackCursorAdvanceSendCapture`) does not write it. `sendMidiEvent` and `collectOverdubNoteOnParticipantIds` read it. Mute still writes the ledger. Does not mean stored `LoopPasses` are already one-owner | Geometry (`NoteSpan`); LCR `PresentNoteVec`; `midiHandler.sendMidiEvent` (only if `playbackEmitMidiOutput_`) |
| **Analyze** (consumer role) | Select / overlap / present-at queries against the selected **LoopSnapshot** around a tick | A new owner type; piano-roll `visualCache`; playback `mergedEvents` |
| **Pipeline** | Formal multi-stage processing where **each stage has independent responsibility** and stages may be async or budget-sliced | Sequential synchronous algorithms (use **Resolution**) |
| **Outbound** | Note-edit fader motor and Droid feedback path — distinct from MIDI Output | Generic MIDI egress |
| **Input** / **Output** | MIDI and USB routing — `MidiHandler` ingress and egress (`handleMidiInput`) | Control-surface motor path (Outbound); **MIDI service** (not a domain noun or owner) |
| **Interval path** | Work that executes between consecutive `handleMidiInput()` entries (the MIDI Input Gap), including clock dispatch that consumes that interval | One `loop()` iteration; persist `admit*` |
| **Structurally bounded** | Code enforces a fixed maximum amount of work and storage | A measured typical cost; a µs budget checked only *between* steps |
| **Empirically bounded** | Measurements provide an engineering limit, but code does not enforce it | A structural cap; a reservation guarantee without an overrun policy |
| **Unbounded owner** | Owner that scans, drains, allocates, sorts, or loops on a content-dependent condition without an enforced cap | A structurally bounded slice; an empirically bounded unit with an explicit overrun policy |
| **Manager** | Domain owner coordinating lifecycle over a scope | Controller (not used); thin routing (Handler) |
| **Capture** | Live record or overdub append buffer until stop | Committed timeline rows |
| **pass** / **passes** | One committed content row on `LoopPasses`: a first **recordPass** at record stop, an **overdubPass** at the LoopGeometry wrap in force at that time, or an **editPass** for one note change. | A LoopSnapshot; a session; moving note range; single-note focus lifecycle |
| **editPass** | One `saveNoteEditPass()` row in `passes.editPasses[]` | Live edit session store |
| **merge** | Replay active capture passes into a working view | Full timeline replay (use **materialize**) |
| **materialize** | Full pass replay to a MIDI event vector | Active capture merge (use **merge**) |
| **content record** | Durable Loop history row. After migration a published **LoopSnapshot** is a content record. Capture and edit **pass** rows remain compatibility content until retirement. | Persisted undo entry; editor history; `stateRaw`; undo/redo **Active** / **Disabled** |
| **content history** | Ordered content records on disk | `GlobalUndoStack`; `LoopUndoHistory`; a Set relink stack |
| **replay** | Reconstruct current Loop state from content records | Undo/redo derivation (use **editing derivation**) |
| **editing derivation** | Runtime undo/redo semantics from ordered content plus grouping rules | Persisted undo payload; current notes alone |
| **Active** / **Disabled** | Current code names (`CapturePassState`, `EditPassState`) for whether a committed pass participates at the current undo/redo position. **Active** = included in derived content; **Disabled** = still stored, skipped by `materialize` / merge. Set by undo/redo traversal (`setCapturePassState` / `setEditPassState`), not by sealing store or Capture. When that pass-state / undo logic is refactored, rename to **Included** / **Excluded**. Do not rename earlier. | `ActiveNote`; active loop; Capture live; a broken or invalid pass |

---

## Legitimate architectural terms

Some words recur because they name real concepts. **Do not eliminate them** — use them only when the meaning matches.

### Pipeline

Use when describing a formal multi-stage processing algorithm where each stage has an **independent responsibility** and stages may run asynchronously or across budget slices.

A **Pipeline of role-owned stages** is the shape used for resumable work: each stage is owned by a **role**, declares its **cost** and its **deadline**, and reports a shared terminal result — **Pending**, **Complete**, **Denied** — under **non-destructive failure** (a denial leaves no partial authority and preserves the owner's state). Do **not** reach for a framework, virtual dispatch or message passing to get it: indirection costs ITCM/RAM1 on this target, where a single non-trivial constructor has already cost a 32 KB FlexRAM bank. The owners keep concrete types and preallocated state, and are aligned only when next touched.

Do **not** use because several functions execute sequentially in one call stack. Synchronous deterministic algorithms are **Resolution**, not Pipeline.

### Playhead-scheduled MVCC

Use **Playhead-scheduled MVCC** for the whole runtime architecture that serves musical content to playback from multi-version immutable state:

> **Playhead-scheduled MVCC** — multi-version immutable state, resolved by interval projection, materialized incrementally against a **playhead deadline**, with **epoch-leased working sets**.

Every part of that sentence is already a governed term; compose them rather than inventing synonyms.

| Part | Governed term |
|------|---------------|
| Immutable version of the effective content | **Snapshot** / **LoopSnapshot** |
| Which part of the timeline is relevant | **interval projection** / **playback window** ([IntervalProjection.md](Architecture/IntervalProjection.md)) |
| Incremental production, sliced inside MIDI boundaries | **Pipeline** (of role-owned stages), **Deferred** / **Pending** / **Scheduled**, **Queue** |
| What orders materialization | the **playhead** — the next arrival of an affected tick *is* the deadline (**playhead deadline**) |
| Bounded resident state | **working set**, leased per **role** and reclaimed by **epoch** (generation) |
| Where the bytes live | **internal heap** / **external memory pool** |

**Readiness is the contract.** After a recording, overdub or edit changes content at a tick, an updated immutable state must be ready by the time the playhead moves over that tick again. A missed deadline keeps the **previous** version authoritative for that pass: a read is never torn, never held silent, and never deferred in a way that disturbs timing. Playback consumes immutable versions only — that is why the versions exist.

Use **epoch-leased working set** for bounded resident state held by a **role** and reclaimed when its generation is unreachable. Identity is `owner + generation + role`; release is by identity, and a generation-mismatched release is a **rejected, reported no-op** so a late cleanup cannot touch recycled storage. Capacity is **derived** from measurement, never asserted, and every resident store declares its class, cardinality, cap and eviction policy.

Do **not** use **service**, **microservice**, **module**, or **API** as an architectural noun for these owners: there is no message passing, no serialization and no dynamic dispatch between them. They are preallocated, role-owned stages invoked cooperatively inside budget slices. Do not use **cache** unqualified for a working set — name the tier (**standby**, **window**, **envelope**) and its cap. Do not use **lease** for anything that is not role-owned and generation-qualified.

Owning design: `openspec/changes/runtime-working-set-ownership/`.

### Snapshot

Use **Snapshot** for the *act or role* of capturing immutable state (`shareForSnapshot`, undo snapshot, display snapshot).

Use **LoopSnapshot** for the Loop’s complete immutable effective-state object: LoopGeometry plus a **span index** (pitch → bar → membership lists of **NoteRef**s) and an **event index** (bar → membership lists of **EventRef**s). Event-list order is an indexed view of existing event-stream semantics, not a second authority. `Loop` publishes a global snapshot at session exit (record stop, overdub stop, note-edit close) by reading the previous LoopSnapshot as const and writing a new root. A session may also hold playable **provisional** snapshots that are not global undo entries. There is no mutable LoopSnapshot during a session. It is not a `LoopPass` and not a flattened `materialize` vector. Do not name the bar granule a tile, lane, bucket, or grid.

Spelling is **LoopSnapshot** (one word Snapshot), matching `PersistedLoopSnapshot` and `LoopSnapshotRef`. `PersistedLoopSnapshot` remains the transitional file/undo DTO of geometry plus deep-cloned `LoopPasses`. Do not store raw C++ pointers. The verb prefix `handle*` stays hardware/MIDI ingress.

This accepted Phase 0 boundary names the logical indexes only. The physical
directory that implements their structural sharing remains a later
measurement decision.

Prefer **State** for the **mutable owner** of live data.

### Queue

Reserve for **actual pending asynchronous work** — items waiting to be drained on a later tick or idle slice.

Do not use for synchronous algorithms that run to completion in one call.

### Deferred, Pending, Scheduled

These are **intentionally distinct** (see concept boundaries). Do not collapse into one umbrella term.

### Timing-critical MIDI Input

MIDI Input is timing-critical. The owner is `MidiHandler`; the operation is `handleMidiInput()`. Do **not** introduce **MIDI service** as an architectural noun or owner.

`main.cpp::loop()` owns runtime service ordering / scheduling integration. Domain work stays with Track / Loop / StorageManager / DisplayManager.

| Term | Meaning |
|------|---------|
| **MIDI Input handling duration** | Time spent inside `handleMidiInput()` (`DIAG,midi_input`) |
| **MIDI Input Gap (MIG)** | Time between consecutive `handleMidiInput()` entries (`DIAG,midi_gap`) |
| **MIDI deadline lateness** | `lateness_us = sendMidiEvent() − scheduled_deadline` for each required note-on, note-off, and MIDI clock (scored independently). Equality is on time. This is **product correctness**, not a synonym for MIDI Input Gap |
| **T_RT_reserve** | Snapshot-idle construction contract: maximum construction-attributable time from idle-turn start to the next timing-critical `handleMidiInput()` entry while `RECORDING` / `PLAYING` / `OVERDUBBING`. [DEC-053](../DECISION_LOG.md#dec-053--t_rt_reserve-is-ceiling-plus-per-turn-midi-boundary). Not `60% × T_clock`. **STOPPED is excluded.** |
| **T_RT_reserve_ceiling** | Fixed policy cap on construction-attributable work per snapshot-idle turn. Initial value **10,000 µs**. Independent of BPM. Not an observed unit. |
| **reserve_us** | Per-turn measured availability on `DIAG,snapshot_idle,child`: idle start to the next timing-critical `handleMidiInput()` enter. Authoritative per-turn admission bound. Not `DIAG,midi_gap`. |
| **T_RT_safety_margin** | Margin subtracted from `reserve_us` before comparing to the ceiling. Not frozen; 1,000 µs is a working hypothesis only. |
| **RuntimeTimingTelemetry** | Instrumentation for MIDI Input Gap, `handleMidiInput()` duration, and (when hooked) MIDI deadline lateness — not a MIDI owner |
| **Runtime admission** | Future coordinator of which bounded runtime units may execute between MIDI Input handling opportunities |
| **Interval path** | Work between consecutive `handleMidiInput()` entries, including clock dispatch that runs inside that gap |
| **Structurally bounded** | Code enforces a fixed maximum amount of work and storage |
| **Empirically bounded** | Measurements provide an engineering limit, but code does not enforce it |
| **Unbounded owner** | Scans, drains, allocates, sorts, or loops on a content-dependent condition without an enforced cap |

Maintained owner inventory and gate: [Architecture/ExecutionBounds.md](Architecture/ExecutionBounds.md), [ARCHITECTURE_RULES.md](ARCHITECTURE_RULES.md) § Execution bound rules. Do not use **scheduling class** or `admit(WorkClass)` as a substitute for a declared bound class.

Historical captures (2026-08-12) emit `DIAG,msi` / `DIAG,midisvc` for the same two measurements. Do not rename `MidiHandler` or `handleMidiInput()` to fit scheduling prose.

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
| **Resolver** | Synchronous multi-phase domain algorithm with a single public entry (stateless static methods until state is needed) | `NoteGeometryResolver` |
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
| **handle*** | Hardware or MIDI ingress routing | `handleMidiInput`, `handleMidiNote`, `handleCoarseFaderInput` |
| **process*** | Poll, defer, or drain work queues | `processDeferredSaveState`, `processPendingPresses` |
| **apply*** | Commit domain mutations to live state | `applyEditSessionActions`, `applyGeometryKindFromControl` |
| **resolve*** | Synchronous constraint solving | `resolveAllConstrainedGeometry`, `resolveConstrainedGeometry` |
| **update*** | Refresh derived views or per-frame tick | `DisplayManager::update`, `updateAllTracks` |
| **commit*** | Finalize a bounded pass or edit batch | `commitCapturePass`, `commitEditAction` |
| **request*** / **admit*** | Queue persistence or admission intent | `requestDeferredSaveState`, `admitLoopPersist` |
| **invalidate*** / **rebuild*** | Cache coherence | `invalidatePlaybackMergedMidiEvents`, `rebuildNoteEditFocusFromStore` |
| **run*** | Orchestrate a multi-step synchronous algorithm (legacy free-function style) | Prefer **`NoteGeometryResolver::resolve`** for note geometry; see § Geometry resolution |

---

## Domain vocabulary (Loop slot)

### Scope words

| Scope | Meaning |
|------|---------|
| **Loop** | Slot container |
| **Capture** | Live record/overdub append buffer |
| **Snapshot** | Act or role of capturing immutable state of some owner |
| **LoopSnapshot** | Complete published effective-state object (LoopGeometry, allocator counters, immutable NoteRef / EventRef root) |
| **passes** | **LoopPasses** — **recordPass**, **overdubPasses[]**, **editPasses[]** |
| **editPass** | One **saveNoteEditPass** row in **passes.editPasses[]** |
| **EditChange** | One atomic change inside an **editPass** |
| **pass** | One committed content row — record stop, overdub wrap (LoopGeometry), or one note-edit change |

**Do not** use **pass** for: moving note tick range (**moving note range** on focus), or single-note **focus** lifecycle.

### Pass types (audio/MIDI time)

| Pass | Ends with | Global undo (when applicable) |
|------|-----------|-------------------------------|
| **recordPass** | `commitCapturePass()` at **record stop** → one **recordPass**. Defines **LoopGeometry** and seals recorded notes as immutable. After that stop, the loop does not enter record again; further notes are **overdubPass** or edit. | **RecordPassAdded** |
| **overdubPass** | `commitCapturePass()` at the **LoopGeometry** wrap in force at that time → **overdubPass** in **passes** | **OverdubPassAdded** |
| **noteEditPass** | `closeNoteEditPass()` (boundary flush of one or more **editPass** note changes) | **NoteEditPassClosed** |
| **controlChangeEditPass** | (future) | **ControlChangeEditPassClosed** (stub) |

A **noteEditPass** batch may contain multiple **editPass** rows sharing **noteEditPassIndex**. Session `editPassIds` are in-session `E:` undo only; commit (or reboot-during-edit durable checkpoint) is one noteEditPass.

Wraps from one overdub session share **overdubSessionIndex** on each `OverdubPass` (same grouping role as **editPassIndex**). Index `0` is ungrouped (legacy cards). Do not call this field session-id.

**`LoopPasses` holds committed pass rows.** Their content is sealed from `NoteEditSession.store` or **Capture** at commit. Those two stay mutable until that commit. **LoopGeometry** is not from store or Capture. **Active** / **Disabled** on a pass is undo/redo traversal, not sealed content: undo sets **Disabled**, redo sets **Active**; the row stays.

### Loop content history (persistence)

[DEC-035](../DECISION_LOG.md#dec-035-loop-persists-content-only): the Loop persists content records only. Undo/redo is derived at runtime.

| Term | Use | Not this |
|------|-----|----------|
| **content record** / **content layer** | Durable Loop history row — published **LoopSnapshot** after migration; pass rows while compatibility remains | `UndoEntry`, `PassStateChange` |
| **editing derivation** | Group content records into undo units | Persist those units |
| **AvailableData** / **isRangeAvailable** | Runtime predicate over buffered ranges | Stored slot flag; `WINDOW_READY` |
| **BufferedData** | Materialized runtime subset of Loop content (set of ranges) | The entire Loop content in RAM |
| **lastUnlinkedSlotLink** | Set last-state for one-step clear relink (Layer B) | Loop undo history |
| **ContentUndoUnit** | Runtime derivation result from `deriveContentUndoUnits` — not persisted | `UndoEntry`; a persist record |
| **LoopGeometry** | Loop length/start metadata (`LoopGeometry.id`, before and after ticks). Length may be shorter than note spans in capture or edit passes. | A LoopSnapshot; note geometry; `UndoLoopGeometry` (in-session GUS payload until Stage 3b) |

**Do not** use **Source** as a domain noun in docs, comments, or types (`ClockSource` already exists). Say **Loop content**.

### Loop content resolution (DEC-037, historical)

[DEC-037](../DECISION_LOG.md#dec-037-loop-content-resolution-parallel-prototype) named a parallel prototype. New docs and new types do not use **LoopContentResolution**. Committed effective state is the selected **LoopSnapshot**. Do not name a query owner `Resolver`.

| Term | Use | Not this |
|------|-----|----------|
| **RawMidiEvent** | Authoritative recorded MIDI in a pass (`MidiEvent` shape) | Edit rows; LoopSnapshot note objects |
| **EditAction** | `EditPass` row (Create / Delete / ChangeLength / ChangeTick / ChangePitch / …) | Raw MIDI; a LoopSnapshot NoteRef |

Physical chunks (`LoopEventStore`) are packing, not resolution boundaries. Pass-content immutability is defined under **Pass types** above.

**Do not** persist undo/redo records, a history cursor, or editor history. NOTE_EDIT `E:` session undo is pre-commit and outside this persist model.

### Types and ids

| Name | Role |
|------|------|
| **EditSessionType** | Live edit session on **EditSession** (`Loop` \| `Note` \| `ControlChange`) |
| **EditPassType** | Stored domain on **editPass** (`Note` \| `ControlChange` \| `Audio`) |
| **EditActionType** | Stored action on **editPass** (`Create` \| `Update` \| `Delete`) |
| **EditPropertyType** | Updated field on **editPass** (`Pitch`, `Length`, `StartTick`, `EndTick`, `Tick`, `Value`, `None`) |
| **EditPassId** / **PassId** | Stable id per pass row (unified **nextPassId_**) |
| **LoopGeometry.id** | Stable id per committed loop-geometry content record (same **nextPassId_**) |
| **NoteRef** | Loop-scoped reference to an immutable note object — LoopSnapshot span-index membership and **EditChange** targets. The same `NoteId` / version MAY appear in multiple bars; candidate sets deduplicate by `NoteId` |
| **EventRef** | Loop-scoped reference to an immutable MIDI event object whose tick is in a bar — LoopSnapshot event-index membership (note and non-note). The bar list is an indexed view of existing event-stream order, not a second authority |
| **span index** | LoopSnapshot role: pitch → bar → membership lists of `NoteRef`s for complete notes intersecting that bar. Not a new owner. Not the event index, open-at-S / ledger, or a tile / lane / bucket / grid |
| **event index** | LoopSnapshot role: bar → membership lists of `EventRef`s whose tick is in that bar. Indexed view of existing event-stream order, not a second ordering authority |
| **bar membership list** | The `NoteRef` or `EventRef` membership in one bar (and, for notes, one pitch) of a LoopSnapshot index. Multiple bars may hold the same `NoteId`; that is one note, not one note per bar |

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
| **LoopGeometry** | content record | committed loop length/start revision (`LoopGeometry.id`) |

### Merge, materialize, and memory tiers

| Concept | Use | Avoid |
|------|-----|-------|
| Active capture pass replay | **merge** (`mergeActiveCapturePasses`, `mergeMaterializedPassesWithCapture`) | flatten |
| Full pass replay to MIDI vector | **materialize** (`materializeToEventVector`) | flat / flatten |
| Query-time effective MIDI/state | Selected **LoopSnapshot** (NoteRef / EventRef) | `materialize` as the normal-path owner; **LoopContentResolution** |
| Note projection of a resolved window | **resolveNotes** (derived consumer) | treating Notes as playback authority |
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

### Note geometry

**Note geometry** — The spatial properties of a note within a loop, including start tick, end tick, duration, projection, overlap relationships, and constraint-derived adjustments. Geometry is the canonical representation used by the note editing subsystem before mutations are applied.

**First-class domain concept:** **Note geometry** is the architectural name for this subsystem. Generic **Geometry** alone is ambiguous at boundaries (UI layout, piano-roll drawing, loop length, display coordinates). Promote **NoteGeometry** in public APIs and cross-module types; keep **Geometry** concise inside the note-geometry subsystem when context is already obvious.

#### Guiding principle

> Broader scope → more explicit names.

| Scope | Naming |
|-------|--------|
| **Subsystem boundary** (headers, public types, TUs, folders) | **NoteGeometry** prefix or suffix — e.g. `NoteGeometryResolver`, `NoteGeometryDriver`, `EditedNoteGeometry` |
| **Inside note-geometry module** (private methods, locals) | **Geometry** without repetition — e.g. `projectBaseline()`, `constraints`, `baseline` |
| **Different domain** (loop length, undo snapshots, display draw) | Do **not** force **NoteGeometry** — use domain-specific names (`UndoLoopGeometry`, display projection) |

**Namespace compression:** context in the name should decrease as scope narrows.

```text
NoteGeometryResolver::resolve()
    → projectBaseline()
    → analyzeInteractions()
    → resolveConstraints()
    → buildActions()
```

Avoid `projectNoteGeometryBaseline()` inside `NoteGeometryResolver`; avoid `noteGeometryConstraints` as a local when `constraints` is clear.

#### Review strategy (incremental — not mechanical)

1. Identify architectural uses of generic **Geometry** (`rg Geometry` on `include/`, public headers).
2. Ask: is the name ambiguous outside the note-edit subsystem?
3. Promote to **NoteGeometry** at boundaries when yes.
4. Leave implementation details concise when the enclosing type or file already scopes the domain.
5. Apply on **touch-and-rename** during refactors — no repository-wide rename-only pass.

**Future folder** (when subsystem grows): `src/NoteGeometry/` — `NoteGeometryResolver`, `NoteGeometryDriver`, `NoteGeometryAnalysis`, `NoteGeometryConstraints`, `NoteGeometryActions`. Public entry remains `NoteGeometryResolver::resolve`.

#### Public symbol review (evaluate on touch)

| Current | Recommendation | Timing / notes |
|---------|----------------|--------------|
| `runEditSessionGeometryPipeline` | `NoteGeometryResolver::resolve` | EditManager TU split **Phase 7a** |
| `RunEditSessionGeometryPipeline.*` | `NoteGeometryResolver.*` | Phase 7a |
| `ResolveConstrainedGeometry.h/.cpp` | Evaluate `ResolveConstrainedNoteGeometry` or move under `NoteGeometry/` | Phase 7 or 7c |
| `resolveConstrainedGeometry` / `resolveAllConstrainedGeometry` | Evaluate `resolveConstrainedNoteGeometry` / `resolveAllConstrainedNoteGeometry` | With resolver TU work |
| `EditedGeometry` | Evaluate `EditedNoteGeometry` | Phase 7+ when `EditSessionAction.h` touched |
| `ConstrainedNoteGeometry` | **Keep** — already explicit | — |
| `GeometryFaderInput.cpp` | Evaluate `NoteGeometryFaderInput.cpp` | Next ControlSurface note-edit fader work |
| `PlayingEditGeometryDefer.cpp` | **Done** — was `PlayingGeometryDefer.cpp` | Playing-transport note-edit geometry defer |
| `isGeometryDriverActive`, `finishGeometryDriverSideEffects`, … | Evaluate **NoteGeometryDriver** vocabulary | ControlSurface boundary — disambiguate from UI/display geometry |
| `EditEvent::GeometryChanged` | Evaluate `NoteGeometryChanged` | When `EditEvent` consumers refactored |
| `beginGeometryMutation`, `isGeometryEditKind`, … | **Keep** concise — `EditManager` / `NoteEditKind` context is sufficient | — |
| `UndoLoopGeometry`, `LoopEditDepartGeometry` | **Keep** — loop-length domain, not note geometry | — |
| Display / piano-roll “geometry” in plans | **Not** note geometry — do not conflate | — |

Related types today: `EditedGeometry`, `ConstrainedNoteGeometry`, geometry driver, **`NoteGeometryResolver`**.

### Geometry resolution (preferred term)

Note-edit geometry changes run a **deterministic synchronous resolution algorithm**:

1. **Validation** — session state, focus, loop length, note ID preparation
2. **Preparation** — changed notes, evaluation scope, baseline projection
3. **Analysis** — eligible pairs, interaction analysis, grouping
4. **Resolution** — constraint solving, action generation
5. **Commit** — apply actions, refresh caches

**Domain vocabulary:** **note geometry** — see § Note geometry above (`EditedGeometry`, `ConstrainedNoteGeometry`, geometry driver). Do not rename the domain noun; replace implementation-oriented **Pipeline** naming.

**Preferred public owner:** **`NoteGeometryResolver`** — responsibility-oriented type for “resolve note geometry into valid note mutations.” Callers (today: `EditManager`, `NoteMovementUtils`) own edit-session context; the resolver owns note-geometry resolution.

```cpp
class NoteGeometryResolver {
public:
    static bool resolve(...);              // was runEditSessionGeometryPipeline
    static bool resolveForCausingNote(...); // was runEditSessionGeometryPipelineForCausingNote
};
```

**TU:** [`src/EditManager/NoteGeometryResolver.cpp`](../../src/EditManager/NoteGeometryResolver.cpp), [`include/NoteGeometryResolver.h`](../../include/NoteGeometryResolver.h).

**Legacy debt:** `runEditSessionGeometryPipeline` / `RunEditSessionGeometryPipeline.*` — migrated to `NoteGeometryResolver` (EditManager TU split Phase 7). Do not introduce new `*Pipeline*` identifiers for synchronous geometry resolution. **Capture telemetry:** `#CAP,…,GEOM_APPLY,resolve,…` (was `pipeline`); helper `logGeomApplyResolve`.

**Inner steps** (already named): `resolveConstrainedGeometry`, `resolveAllConstrainedGeometry`, `applyEditSessionActions` — evaluate **NoteGeometry** promotion per § Note geometry public symbol review. **Future** (optional): private phase methods on `NoteGeometryResolver` (`validate`, `prepareEvaluationScope`, `analyzeInteractions`, …) — not required for the initial rename.

**Avoid:** `EditSessionGeometryResolver` — scopes the type to the caller, not the domain (`NoteGeometry` is the stable noun).

### Note edit read path

| Need | Use | Avoid |
|------|-----|-------|
| MIDI pairs during edit | **NoteEditSession.store** / `editAwareMidiEvents()` | “audible”, “live layer” |
| Reconstructed notes | **`NoteUtils::reconstructNotes`** → **`DisplayNote`** | `audibleNotes`, `sessionNotes` |
| Fader-1 + display list excluding Hidden overlap | **`filterSelectableDisplayNotes`** on **`projectNoteEditDisplayNotes`**; **`EditManager::selectableDisplayNotesAtEditSelect`** (full loop) and **`selectableDisplayNotesForEditUi`** (windowed UI) | `NoteEditSessionView`, `selectableNotes()` as a type, `filterProjectingSelectableDisplayNotes` |
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
| TU / subsystem extraction | **Valid vehicle for architectural renames** — strengthen identifiers in the **same PR** as moves (Phase 0 naming table in plan). Agents must **propose** renames when names hide ownership; do not defer to a separate naming sprint. Behavior-preserving TU work **includes** touch-and-strengthen in touched files; it does **not** mean “no identifier changes.” **Do not remove** legacy wrappers/aliases during extraction — use a dedicated **Legacy Retirement** phase ([legacy_api_retirement_tu_extraction_refinement.md](../Plans/legacy_api_retirement_tu_extraction_refinement.md)). |
| Serial capture tokens frozen | `#CAP`, `PERS`, `REVT` — never rename |
| Plan glossary promotion | When a plan glossary term stabilizes, promote it here; mark plan section superseded |
| Investigation doc for debt | Current-vs-preferred mapping and timing live in the investigation doc, not here |

---

## Architecture review checklist

Use during code review, OpenSpec implementation review, and before merge:

- [ ] No unnecessary new terminology introduced
- [ ] Existing architectural vocabulary reused (check concept boundaries above)
- [ ] **Note geometry:** public cross-module symbols use **NoteGeometry** where generic **Geometry** would be ambiguous (§ Note geometry)
- [ ] **Namespace compression:** no redundant `NoteGeometry` prefix on private methods or locals inside note-geometry owners
- [ ] File names describe responsibilities, not implementation mechanics
- [ ] Public APIs follow established verb conventions
- [ ] This document updated if a new architectural concept is approved
- [ ] Pipeline / Queue / Flow not used for synchronous deterministic algorithms (use **Resolution**)
- [ ] Committed effective-state reads use the selected **LoopSnapshot**, not `NoteGeometryResolver` or a generic `Resolver`. Do not introduce **LoopContentResolution** in new docs
- [ ] Interval-path work uses **structurally bounded** / **empirically bounded** / **unbounded owner** — not a scheduling class or `admit(WorkClass)`
- [ ] New top-level domain nouns have user approval and `DEC-###` entry

Referenced from [docs/Agents/reviewer.md](../Agents/reviewer.md).

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
| [Guides/OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md](../Guides/OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md) | Overdub overlap candidate / classify / resolve / seal (not vocabulary authority) |
| [Guides/OVERDUB_LEDGER_NOTE_EVALUATIONS.md](../Guides/OVERDUB_LEDGER_NOTE_EVALUATIONS.md) | Open ledger identities, catch-up vs clock (not vocabulary authority) |
| [architecture_naming_authority_refinement.md](../Plans/architecture_naming_authority_refinement.md) | Investigation audit, naming debt roadmap |
| [refactor_priority_backlog.md](../Plans/refactor_priority_backlog.md) | Cross-cutting refactor priority index (P1/P2/P3) |
| Archived OpenSpec baselines | `m8-rename`, `m8-edit`, `pool-budget` in `openspec/specs/` |
