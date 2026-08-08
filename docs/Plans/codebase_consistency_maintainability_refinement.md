# Codebase consistency & maintainability refinement

**Kind:** refinement  
**Date:** 2026-08-08  
**Status:** Active — Phase **4 design shipped**; implementation queued on separate `refactor/*` branches  
**GitHub:** [#18](https://github.com/Lytrix/MidiLooper/issues/18) (Task) · Project [Work](https://github.com/users/Lytrix/projects/1) **NOW**  
**Branch:** `dev` — Phase 3 landed via PR #21; Phase 2 via PR #20
**Decision:** Refinement — align representation with established authority/ownership; behavior-preserving unless explicitly approved otherwise  
**Naming authority:** [NAMING.md](../Authority/NAMING.md)  
**Lifecycle:** [WORKFLOW_LIFECYCLE.md](../Authority/WORKFLOW_LIFECYCLE.md) · [GITHUB_WORK_TRACKING.md](../Authority/GITHUB_WORK_TRACKING.md)

---

## One-line goal

Bring code representation into alignment with the already-established authority and ownership model **without reopening product behavior**.

---

## Findings (audit)

| Artifact | Role |
|----------|------|
| Interactive report | Cursor canvas `codebase-consistency-audit` (session 2026-08-08; not in-repo) |
| Prior naming debt | [architecture_naming_authority_refinement.md](architecture_naming_authority_refinement.md) |
| Vocabulary policy | [NAMING.md](../Authority/NAMING.md) |
| Refactor index | [refactor_priority_backlog.md](refactor_priority_backlog.md) |

Audit date: **2026-08-08**. Read-only; no code changed in the audit pass.

---

## Work order

One **conceptual migration per implementation step**. Refine call sites and verification **immediately before coding** each checkbox (Plan-Pre-Implementation-Review).

### Phase 1 — Authority

Align live code with declared owners. Highest risk; native + HITL where edit/persist paths are touched.

| Step | Item | Evidence anchor | Must not change |
|------|------|-----------------|-----------------|
| 1.1 | Retire projected-store mutations | `NOTE_EDIT_PROJECTED_STORE_COMPAT` still `1`; `sessionMidiEvents` / `mutEditProjection*` | Commit/HITL edit semantics |
| 1.2 | Establish selection tick SoT | `EditManager.selectedTick` + `EditorSelection.selectedTick` | Fader/motor select behavior |
| 1.3 | Complete `admit*` migration | ~18 `markCurrentSet*Dirty` vs ~2 `admit*` call sites | When SD work is queued |

**Architecture note:** 1.1 completes DEC-029 / `note-edit-current-state` removal trigger. **Pinned:** retire now (option 1); persistence stays queued.

#### Phase 1.1 — projected-store mutation retirement (sliced)

Declared owner: mutate `NoteEditCurrentState` → `projectToSessionStore` / `refreshNoteEditSessionProjection`. Store remains projection + audition flat, not an independent editor SoT.

| Slice | Concept | Status |
|-------|---------|--------|
| **1.1a** | Geometry apply never uses live-store-only path — always pass non-const current state into `applyEditSessionActions` from `NoteGeometryResolver` | **Done** (this branch) |
| **1.1b** | Retire NoteMovementUtils reverse sync (`mutate store` → `syncProjectingRowsFromSessionStore`) after resolve | **Done** (this branch) |
| **1.1c** | FaderDependentSnapshot / commit normalize: store normalize via projection owner (`normalizeNoteEditClosureProjection` / `normalizeNoteEditSessionProjectionForCommit`) | **Done** (this branch) |
| **1.1d** | Delete empty-currentState delete fallback; remove `NOTE_EDIT_PROJECTED_STORE_COMPAT` + `mut*Compat` APIs | **Done** (this branch) |

**Architecture checkpoint (1.1 overall):** Ownership — completing approved DEC-029 transfer (not a new owner). Transitions — no new session/mode transitions. Behavior-preserving per slice unless a bypass was already violating declared authority.

### Phase 2 — API and vocabulary

Touch-and-rename / API unification. No rename-only mega-PRs ([NAMING.md](../Authority/NAMING.md) § Migration policy).

| Step | Item | Evidence anchor | Must not change |
|------|------|-----------------|-----------------|
| 2.1 | Selectable-display API | Four live names; NAMING cites deleted `filterSelectableDisplayNotes` | Projected note list contents | **Done** — `filterSelectableDisplayNotes`; cache helper private |
| 2.2 | Geometry vocabulary | `NoteGeometryResolver` shipped; `pipelineApplied`, `GEOM_APPLY,pipeline`, log strings | Resolver apply results; HITL matchers if keyed on CAP tokens | **Done** — `geometryResolved`, `GEOM_APPLY,resolve`, `logGeomApplyResolve` |
| 2.3 | `published` → `committed` | ~35 `published` hits for committed material | Wire formats / CAP tokens unless proven unused | **Done** — locals/tests; CAP `published` outcome retained |
| 2.4 | Playing vs Playback terminology | `PendingPlayingGeometry`, `isPlayingBack`, merge-cache “window” naming | Transport vs machinery meanings | **Done** |

### Phase 3 — Structural cleanup

Low risk after Phases 1–2 stabilize call sites.

| Step | Item | Evidence anchor | Must not change | Status |
|------|------|-----------------|-----------------|--------|
| 3.1 | Dead helpers | Unused NoteMovement / IntervalProjection / MidiHandler / SyncDrain / ControlSurface mappers | Behavior (delete dead only) | **Done** — 8 symbols removed (see below) |
| 3.2 | Duplicate selection writes | `applySelectNav` vs `applySelectionFromGeometryEdit` | Selection outcomes | **Done** — intentional twins documented on `EditManager` |
| 3.3 | Legacy / twin cleanup | Monolith quarantine twins; leftover deprecated wrappers after 1.3 | Quarantine/SD outcomes | **Done** — removed unused `markCurrentSet*Dirty` public API |

#### Phase 3.1 — removed dead helpers

| Symbol | Module |
|--------|--------|
| `NoteMovementUtils::notesOverlap` | superseded by `NoteUtils::notesOverlap` |
| `lengthEditLoopTickToCoarsePitchbend`, `lengthEditFineCcFromOffset` | superseded by `NoteEditLengthFaderMapping` / `NoteEditDependentFaderSnapshot` |
| `MidiHandler::sendAfterTouch`, `sendContinueMIDI`, `setOutputUSB`, `setOutputSerial`, `isOutputUSBEnabled` | unused; routing via `sendMidiEvent` / internal flags |

**Architecture gate (3.x):** Ownership change: NO. Transition change: NO. Behavior-preserving: YES.

**Verification:** `pio test -e native`; `pio run -e teensy41-capture-serial`.

**HITL smoke (Phase 3)**

| Capture | Scope | Result |
|---------|-------|--------|
| [`session_20260808_174827.log`](../../captures/session_20260808_174827.log) | Post–PR #21 full edit smoke (~53s) | **PASS** — boot load; 126× `GEOM_APPLY,resolve` (0× `pipeline`); 65 Move + 49 Pitch + 12 Length while `transport=1`; 126/126 `done,1`; `NoteEditPassClosed edits=6`; 6× `UNDO_PUSH,admit_ok` |

**Phase 3 HITL gate:** **PASS** — combined move/pitch/length playing-transport defer + clean session exit on post–Phase 3 firmware.

### Phase 4 — Extraction boundaries (design)

**No firmware in Phase 4.** Produce pin-down notes before any extraction PR.

| Target | Design outcome | Implementation plan |
|--------|----------------|---------------------|
| `TrackManager.cpp` (~1387) | Six process modules under `src/TrackManager/` (capture queue, transport tick, slot matrix, playback policy, Midi LED, memory pressure) | [codebase_consistency_phase4_extraction_boundaries_refinement.md](codebase_consistency_phase4_extraction_boundaries_refinement.md) §1 |
| `NoteMovementUtils.cpp` (~1026) | Pair resolution vs geometry apply; apply TUs under `src/EditManager/` | Same doc §2 |
| `DisplayNoteResolve.cpp` (~864) | Second mechanical split by display read mode (behavioral split deferred) | Same doc §3 |
| `NoteEditFocus.h` (~423) | Types vs API header split; bodies already shipped | Same doc §4 |

**Phase 4 status:** **Design shipped** — implementation queued as separate `refactor/*` branches (recommended order: TrackManager → NoteMovementUtils → DisplayNoteResolve → header).

### Phase 4 — Investigation only (superseded table)

---

## Rules

1. **Behavior-preserving** unless explicitly approved otherwise.
2. **No arbitrary LOC-driven splitting.**
3. **No abstraction solely for DRY.**
4. **One conceptual migration per implementation step.**
5. **Refine implementation details immediately before coding** (pre-implementation review + architecture checkpoint on Phase 1).
6. **Native / HITL verification** where behavior could be affected:

| Phase | Minimum verification |
|-------|----------------------|
| 1.1 / 1.2 | `pio test -e native` (note-edit suites) + HITL edit path (`edit_full` or edit baseline) |
| 1.3 | `pio test -e native` (persistence) + optional load/save / boot smoke |
| 2.x | `pio test -e native`; HITL only if CAP/matcher strings change |
| 3.x | `pio run -e teensy41-capture-serial` + `pio test -e native` |
| 4.x | Docs / design note only |

7. Branch/PR default: short-lived `refactor/<scope>` → PR → `dev` ([GITHUB_WORK_TRACKING.md](../Authority/GITHUB_WORK_TRACKING.md) §10).
8. Do **not** mix this change with product persistence/overlay hardening unless user merges scopes.

---

## Explicit non-goals

- Splitting `RevisionCommit` / `RevisionLoad` / `StorageLoopIo` by size alone.
- Further StorageManager façade micro-slices as priority work (root ~422; PR #17).
- Mass rename of outbound motor **Pipeline** without design (may be legitimate async Pipeline per NAMING).
- Collapsing `reconstructNotes` / `projectNoteEditDisplayNotes` / `editAwareMidiEvents` into one “getNotes”.
- Reopening NOTE_EDIT product behavior or overlap resolution design.

---

## Suggested GitHub shape

| Type | Scope |
|------|--------|
| **Task** (parent) | Codebase consistency & maintainability refinement |
| Sub-issue / checklist | Optional: one per phase (not per checkbox) |

Project board: [Work](https://github.com/users/Lytrix/projects/1) — **NOW** (#18).

---

## Pre-implementation review (Phase 1.1a)

### Ready
- Owner path exists: `NoteEditCurrentState::applyEditSessionAction` → `projectToSessionStore` inside `applyEditSessionActions` when `currentState != nullptr`.
- Open session builds current state (`NoteEditSessionLifecycle`).
- DEC-029 already transferred authority; 1.1 completes removal trigger.

### Resolved
| Topic | Decision |
|-------|----------|
| Persist vs COMPAT retire | Retire COMPAT now (user option 1) |
| First code slice | **1.1a** — `NoteGeometryResolver` always passes non-const `&currentState` |

### Open before later slices
1. 1.1b reverse-sync in `NoteMovementUtils` after resolve
2. 1.1c normalize paths (fader latch / commit)
3. 1.1d flag flip + delete `mut*Compat`

### Architecture gate (1.1a)
- Owner: `NoteGeometryResolver::resolve` → `applyEditSessionActions`
- Invariant: geometry apply mutates current state then projects; no production live-store-only apply path
- Ownership change: NO (closes bypass of declared owner)
- Transition change: NO
- Reuse: YES — extend resolve call site only

### Proceed?
- YES — 1.1a

### Phase 1.2 — selection tick SoT (started)

**SoT:** `sessionState.selection.selectedTick` (`EditorSelection`). `getSelectedTick` / `setSelectedTick` route through session state; duplicate `EditManager::selectedTick` member removed.

| Area | Change |
|------|--------|
| `EditManager.h` | get/set → `sessionState.selection.selectedTick`; drop member |
| `NoteEditSelection.cpp` | Remove mirror writes in `applySelectionFromGeometryEdit`, `syncGeometrySelectionToUi`, `syncNoteEditSessionStateToUi` |
| `EditNoteStateCoordinator.cpp` | Reads via `getSelectedTick()`; writes via `applySelectNav` only |
| Commit / focus rebuild / undo | Drop redundant member mirrors |

**Architecture gate (1.2):** Owner — `EditorSelection` in `NoteEditSessionState`; accessors on `EditManager`. Ownership change: NO. Transition change: NO. Behavior-preserving: YES. Native: **969/969**. HITL: **PASS** [`162859`](../../captures/session_20260808_162859.log) + [`163043`](../../captures/session_20260808_163043.log).

### Phase 1.3 — admit* migration (domain call sites)

| API | Role |
|-----|------|
| `markLoopSlotMaterialDirty` | Workspace dirty bit + material anchor (no queue admit) |
| `admitLoopSlotPersist` | `admitSlotMeta` + slot-keyed `LoopPersist` work item |
| `markTrackSlotsMaterialDirty` / `admitTrackSlotPersistence` | Clear-track / track-wide paths |

Deprecated `markCurrentSet*Dirty` wrappers forward to the split APIs. **18** production call sites migrated (Track capture/stop, loop geometry, loop edit, undo, slot clear). Native: **969/969**.

### HITL smoke (Phase 1.1)
| Capture | Scope | Result |
|---------|-------|--------|
| [`session_20260808_161219.log`](../../captures/session_20260808_161219.log) | 1.1a–b — move + overlap + pitch + commit (~53s); boot recovery | **PASS** |
| [`session_20260808_162038.log`](../../captures/session_20260808_162038.log) | 1.1c — loaded workspace + move/overlap/pitch + projection-owner normalize (~61s) | **PASS** — all `commit parity ok`; `NoteEditPassClosed edits=4` |
| [`session_20260808_162859.log`](../../captures/session_20260808_162859.log) | 1.2 — selection tick SoT + boot-loaded workspace (~37s) | **PASS** — select/motor bracket aligned; move+overlap+pitch; `NoteEditPassClosed edits=6` |
| [`session_20260808_163043.log`](../../captures/session_20260808_163043.log) | 1.2 extended — length bracket, overlap chain, chord select (~48s active; tail continuation) | **PASS** — length end-tick motor sync; 20+ overlap moves; `ChangeLength` commit; `NoteEditPassClosed edits=4` |
| [`session_20260808_163904.log`](../../captures/session_20260808_163904.log) | Phase 1 gate — boot load + select/move/pitch (~50s) post 1.1–1.3 | **PASS** — `UNDO_PUSH,admit_ok`; move+pitch commits; `NoteEditPassClosed edits=4` |

### Phase 2.1 — selectable-display API (vocabulary)

**Goal:** One filter name aligned with NAMING; two public `EditManager` entry points; cached helper private.

| Layer | Symbol | Role |
|-------|--------|------|
| Filter | `filterSelectableDisplayNotes` | Paint projection → selectable rows (was `filterProjectingSelectableDisplayNotes`) |
| Public | `selectableDisplayNotesAtEditSelect` | Full-loop select inventory (or cached notes outside NOTE_EDIT) |
| Public | `selectableDisplayNotesForEditUi` | Windowed UI inventory |
| Private | `filteredSelectableDisplayNotesForNoteEdit` | Cached selectable slice |

**Architecture gate (2.1):** Owner — `NoteEditFocus` filter + `EditManager` cache. Ownership change: NO. Transition change: NO. Behavior-preserving: YES.

**Verification:** `pio test -e native` (note-edit suites).

**HITL smoke (Phase 2.1)**

| Capture | Scope | Result |
|---------|-------|--------|
| [`session_20260808_170709.log`](../../captures/session_20260808_170709.log) | Boot load + select/move/overlap/pitch + exit (~47s) | **PASS** — 133× `GEOM_APPLY,pipeline,*,1,*`; `NoteEditPassClosed edits=4`; bracket `DNTE` tracks mover |

### Phase 2.2 — geometry vocabulary

**Goal:** Align geometry CAP tokens and locals with `NoteGeometryResolver` / Resolution vocabulary. No automated HITL matchers keyed on `GEOM_APPLY,pipeline` (verified: none in `scripts/` or `test/`).

| Before | After |
|--------|-------|
| `pipelineApplied` locals | `geometryResolved` |
| `logGeomApplyPipeline` | `logGeomApplyResolve` |
| `#CAP,…,GEOM_APPLY,pipeline,…` | `#CAP,…,GEOM_APPLY,resolve,…` |
| `GeometryPipeline:` debug prefix | `NoteGeometryResolver:` |

**Architecture gate (2.2):** Owner — `NoteGeometryResolver`. Ownership change: NO. Transition change: NO. Behavior-preserving: YES (log token rename only).

**Verification:** `pio test -e native`; post-flash captures use `GEOM_APPLY,resolve` (pre-2.2 captures such as [`170709`](../../captures/session_20260808_170709.log) still show `pipeline`).

**HITL smoke (Phase 2.2)**

| Capture | Scope | Result |
|---------|-------|--------|
| [`session_20260808_173010.log`](../../captures/session_20260808_173010.log) | Boot load + playing-transport move/pitch + overlap (~80s) | **PASS** — 81× `GEOM_APPLY,resolve,*,1,*`; 0× `pipeline`; 81/81 `done,1` with `transport=1` on queue |
| [`session_20260808_173332.log`](../../captures/session_20260808_173332.log) | Playing-transport move/length + overlap + clean exit (~13s continuation) | **PASS** — 44× `resolve`; 29 Move + 15 Length queue; `NoteEditPassClosed edits=2` |

**Phase 2 HITL gate (2.1–2.4 combined):** **PASS** — [`173010`](../../captures/session_20260808_173010.log) covers boot + move/pitch/overlap while playing; [`173332`](../../captures/session_20260808_173332.log) covers length while playing + session close. Pre-2.2 reference: [`170709`](../../captures/session_20260808_170709.log) (`GEOM_APPLY,pipeline`).

### Phase 2.3 — published → committed (vocabulary)

**Goal:** Remove `published` from production identifiers for committed material; retain documented CAP wire tokens.

| Area | Change |
|------|--------|
| Locals | `publishedIds` → `committedChunkIds`; `PendingCapturePass published` → `pendingPass`; materialize scratch → `committedEvents` / `committedLoopMidi` |
| `SC_DISP` | Parameter `published` → `hasCommittedPasses` (wire field unchanged — same int position) |
| Tests | `publishedIds` + test symbol renames |
| **Retained** | `commitResultLabel(Committed)` → `"published"` — HITL `legacy_record_baseline` `completed_outcomes` |

**Architecture gate (2.3):** Owner — Loop/Track capture commit paths. Ownership change: NO. Transition change: NO. Behavior-preserving: YES.

**Verification:** `pio test -e native`; HITL gate above.

### Phase 2.4 — Playing vs Playback (vocabulary)

**Goal:** Disambiguate transport **Playing** from runtime **Playback** machinery; clarify note-edit geometry deferred while transport runs vs merge-cache gather window.

| Before | After |
|--------|-------|
| `PendingPlayingGeometry*` / `queuePendingPlaying*` / `processPendingPlayingGeometry` | `PendingPlayingEditGeometry*` / `queuePendingPlayingEdit*` / `processPendingPlayingEditGeometry` |
| `applyPlayingPitchGeometry` | `applyPlayingEditPitchGeometry` |
| `PlayingGeometryDefer.cpp` | `PlayingEditGeometryDefer.cpp` |
| `isPlayingBack` | `ignorePlaybackMidiInput` (overdub capture ignores playback-echo MIDI) |
| `playbackWindowBuildInProgress` | `mergedMidiEventsBuildInProgress` |
| `kPlaybackWindowBars` | `kMergedMidiEventsGatherBars` |
| `makeFullLoopPlaybackWindow` | `makeFullLoopPlaybackProjectionInterval` (full-loop `TickInterval` for projection — not domain `PlaybackWindow`) |

**Architecture gate (2.4):** Owner — `ControlSurfaceManager` defer queue; `Track` capture ingress; `TrackPlaybackWindowBuild` merge cache. Ownership change: NO. Transition change: NO. Behavior-preserving: YES.

**Verification:** `pio test -e native`; HITL gate above.

## Exit criteria

- Phase 1: projected-store compat retired or explicitly PARKED with rationale; selection SoT single; production paths use `admit*` (dirty wrappers unused or Legacy Retirement).
- Phase 2: selectable-display has one canonical public name; NAMING.md matches code; geometry/published/Playing debt reduced on touched files.
- Phase 3: confirmed-dead helpers removed; selection write helper or documented intentional twins.
- Phase 4: extraction boundary design filed — [codebase_consistency_phase4_extraction_boundaries_refinement.md](codebase_consistency_phase4_extraction_boundaries_refinement.md); implementation as separate refactor branches.
- Parent Task closed after docs closeout ([DOCUMENTATION_CLOSEOUT.md](../Authority/DOCUMENTATION_CLOSEOUT.md)).

---

## References

- [refactor_priority_backlog.md](refactor_priority_backlog.md)
- [legacy_api_retirement_tu_extraction_refinement.md](legacy_api_retirement_tu_extraction_refinement.md)
- OpenSpec `note-edit-current-state` (DEC-029) — current-state authority
- OpenSpec `note-edit-current-state` (DEC-029) — COMPAT removal shipped Phase 1.1d (#18)
