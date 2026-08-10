# Codebase consistency — Phase 4 extraction boundaries

**Kind:** refinement (design only — no firmware in Phase 4)  
**Date:** 2026-08-08  
**Parent:** [codebase_consistency_maintainability_refinement.md](codebase_consistency_maintainability_refinement.md) § Phase 4  
**GitHub:** [#18](https://github.com/Lytrix/MidiLooper/issues/18)  
**Workflow:** [Mechanical-TU-Split-Workflow.mdc](../../.cursor/rules/Mechanical-TU-Split-Workflow.mdc) · [translation_unit_extraction_plan_template.md](../Templates/translation_unit_extraction_plan_template.md)  
**Process map:** [runtime_process_building_blocks_overview.md](runtime_process_building_blocks_overview.md) — extend **owner**, do not invent parallel Managers

---

## One-line goal

Pin down **named process boundaries** for the four remaining large artifacts (#18 Phase 4) so follow-up TU extraction PRs move **method bodies only** — same pattern as shipped Track / EditManager / Loop / NoteEditFocus / DisplayManager splits.

**Success criteria:** each target has an owner-aligned module map, phased PR stack, risk tier, and explicit **do not** list. **Not** LOC targets.

---

## Phase 0 — prior splits inventory (mandatory)

| Domain | Shipped plan | Root TU today | Already under `src/<Domain>/` |
|--------|--------------|---------------|-------------------------------|
| **Track** | [track_translation_unit_extraction_refinement.md](track_translation_unit_extraction_refinement.md) | `Track.cpp` thin coordinator | `TrackCaptureStopCommit`, `TrackPlaybackHotPath`, `TrackPlaybackWindowBuild`, … |
| **EditManager** | [editmanager_translation_unit_extraction_refinement.md](editmanager_translation_unit_extraction_refinement.md) | `EditManager.cpp` coordinator | `NoteEditSessionCommit`, `NoteGeometryResolver`, … |
| **NoteEditFocus** | [noteditfocus_translation_unit_extraction_refinement.md](noteditfocus_translation_unit_extraction_refinement.md) | **root `NoteEditFocus.cpp` removed** | `NoteEditFocusLinearSpan`, `Overlap`, `PreCommit`, `DisplayProjection`, … |
| **DisplayManager** | [displaymanager_translation_unit_extraction_refinement.md](displaymanager_translation_unit_extraction_refinement.md) | `DisplayManager.cpp` ~184 LOC | `DisplayNoteResolve`, `PianoRollDraw`, … |
| **TrackManager** | — | **`TrackManager.cpp` ~1387** — **not started** | delegates to `SlotStateMachine` (~87 LOC) |

**Implication:** Phase 4 designs **second-level** splits for artifacts that are already extracted once (`DisplayNoteResolve`, `NoteEditFocus.h`) or never split (`TrackManager`, `NoteMovementUtils`).

---

## Recommended implementation order

| Order | Target | Why |
|-------|--------|-----|
| **1** | `TrackManager` | Largest monolith; clear runtime blocks (capture queue, transport tick, slot matrix, LED); aligns with [runtime_process_building_blocks_overview.md](runtime_process_building_blocks_overview.md) Slots + Transport |
| **2** | `NoteMovementUtils` | Edit hot path; split **pair resolution** from **geometry apply** without collapsing note sources |
| **3** | `DisplayNoteResolve` | Second mechanical slice inside DisplayManager; defers **behavioral** mode refactor to existing display plans |
| **4** | `NoteEditFocus.h` | Header hygiene only — bodies already under `src/EditManager/` |

Each target is a **separate branch** (`refactor/trackmanager`, `refactor/note-movement-utils`, …) off `dev`.

---

## Naming / rename step (all targets)

**Authority:** [NAMING.md](../Authority/NAMING.md) § Identifier rules, § Migration policy · [Mechanical-TU-Split-Workflow.mdc](../../.cursor/rules/Mechanical-TU-Split-Workflow.mdc) Phase 0 naming pass · [architecture_naming_authority_refinement.md](architecture_naming_authority_refinement.md) § B naming debt

**Policy:** Phase 4 **implementation** must not introduce new abbreviated identifiers (`Led`, `Utils`, `Rec`, `Idx`, …). **Plan diagrams and prose** use the same rule — spell out terms in labels (`constructor`, not `ctor`; `pendingRecordStart`, not `pendStart`). Adopt preferred names **touch-and-rename** in the same PR as the first extraction slice that touches a symbol — **no rename-only mega-PRs**. When a rename spans multiple upcoming phases (namespace / public header), run a dedicated **Phase RN** immediately after Phase 0 scaffold, before body moves.

**Frozen (never rename):** `#CAP` / HITL log tokens (`GEOM_APPLY`, `DISP`, …), serial capture matchers, archived OpenSpec paths.

### Phase 0 naming table (mandatory before first edit)

Each implementation branch copies this template into its branch plan / PR description and fills **Resolved** rows before Phase 0 scaffold lands:

| Current | Preferred | Target branch | Bundle with |
|---------|-----------|---------------|-------------|
| | | | Phase N or RN |

**Evaluate on touch** (from naming debt audit — adopt only when the listed extraction PR moves the symbol):

| Current | Preferred | Notes |
|---------|-----------|-------|
| `EditedGeometry` | `EditedNoteGeometry` | NoteMovement geometry apply slice |
| `resolveConstrainedGeometry` / `ResolveConstrainedGeometry.*` | `resolveConstrainedNoteGeometry` / `ResolveConstrainedNoteGeometry.*` | Same slice or defer |
| `flattenActiveCapturePasses` (if any remain in TrackManager paths) | `mergeActiveCapturePasses` | TrackManager memory / transport touch |

### Phase RN — abbrev cleanup (per branch)

| Branch | RN scope | Preferred timing | Verify |
|--------|----------|------------------|--------|
| `refactor/trackmanager` | `Led` → `MidiLed` on new TU name + public `updateLeds*` / `clearLeds` / `forceLedUpdate` / `refreshTrackAndLoopSelectLeds` | **RN** after `TrackManagerInternal` scaffold; before `TrackManagerMidiLedFeedback` body move | `pio run -e teensy41-capture-serial`; native batch end |
| `refactor/note-movement-utils` | `NoteMovementUtils` namespace + headers → `NoteEditGeometryApply`; strip `Movement` from new pair/wrap TU filenames | **RN** after `NoteEditGeometryApplyInternal.h`; before pair-resolve body move | `pio test -e native` (`test_edit_apply`, `test_note_edit_focus`, wrap suites) |
| `refactor/display-note-resolve` | New sub-TU filenames only (no abbrev); optional `DisplayWindowGather` → `DisplayNoteWindowGather` if window gather is the first moved slice | Touch-and-rename in first gather PR, or defer | `test_display_window_utils` |
| `refactor/note-edit-focus-header` | None required — new headers already spell out `Types` / `Select` | — | compile + native once |

### Phase LR — rename shims (after Phase 10)

After extraction stabilizes and retirement criteria met ([legacy_api_retirement_tu_extraction_refinement.md](legacy_api_retirement_tu_extraction_refinement.md)):

| Legacy shim | Replacement | Branch |
|-------------|-------------|--------|
| `include/Utils/NoteMovementUtils.h` forwarding include | `#include "NoteEditGeometryApply.h"` only | **LR merged** |
| `namespace NoteMovementUtils` deprecated alias | `NoteEditGeometryApply` | **LR merged** |
| `include/Utils/NoteMovementWrap.h` forwarding include | `#include "NoteEditGeometryApplyWrap.h"` | **LR merged** |

---

## 1 — TrackManager translation-unit extraction

**Branch:** `refactor/trackmanager`  
**One-line goal:** Thin `TrackManager.cpp` into a multi-track **orchestrator**; move bodies into `src/TrackManager/*.cpp` by **runtime process**, not file size.

### Architectural north star

From [runtime_process_building_blocks_overview.md](runtime_process_building_blocks_overview.md):

- **Slots / focus** — `TrackManager` activates slots; `SlotStateMachine` holds per-track slot indices and pending switch state.
- **Transport** — `ClockManager` owns time; `TrackManager::updateAllTracks` drives per-tick commits and playback.
- **Capture** — `Track` owns stop/commit; `TrackManager` queues arms and `finalizeCaptureAndSelectSlot`.

**Do not** move `SlotStateMachine` methods into TrackManager TUs — only **callers** that compose slot + track + storage side effects.

### Domain map

```mermaid
flowchart TB
  subgraph root [TrackManager.cpp coordinator]
    constructor[constructor / setup / getTrack / setSelectedTrack]
    prewarmDisplayCache[prewarmPlaybackRuntime / display visual cache]
  end
  subgraph capture [TrackManagerCaptureQueue]
    queueRecording[queueRecordingTrack / pending record]
    pendingRecordStart[handlePendingRecordStart / quantized record start]
    finalizeCapture[finalizeCaptureAndSelectSlot]
  end
  subgraph transport [TrackManagerTransportTick]
    updateAllTracks[updateAllTracks main loop]
    loopEndSlotCommit[loop end pending slot commit in tick]
    transportStop[handleTransportStop]
    playControl[startPlayingTrack / stopPlayingTrack]
  end
  subgraph slots [TrackManagerSlotMatrix]
    slotEnabledMatrices[slotEnabled / slotMuted matrices]
    slotSelectionHold[beginSlotSelectionHold / multi-slot commit]
    selectedSlotIndex[setSelectedSlotIndex / requestSlotSwitch]
  end
  subgraph playbackPolicy [TrackManagerPlaybackPolicy]
    muteSolo[mute / solo / isTrackAudible]
    masterLoopLength[masterLoopLength / autoAlign]
  end
  subgraph midiLed [TrackManagerMidiLedFeedback]
    midiLedRefresh[updateMidiLeds / refreshTrackAndLoopSelectMidiLeds]
  end
  subgraph memoryPressure [TrackManagerMemoryPressure]
    reclaimPasses[reclaimUnreferencedDisabledPasses]
    backgroundMemoryRelease[releaseBackgroundPlaybackMergedMidiEventsMemory]
  end
  root --> capture
  root --> transport
  root --> slots
  root --> playbackPolicy
  root --> midiLed
  root --> memoryPressure
  transport --> slots
  capture --> slots
```

### Primary modules

```text
src/TrackManager.cpp                          coordinator (~150–250 LOC target)
include/TrackManagerInternal.h                shared file-static helpers (reclaimTrackPriority, resolveTrackIndex, …)
src/TrackManager/TrackManagerCaptureQueue.cpp   record arm/queue, pending start, finalizeCaptureAndSelectSlot
src/TrackManager/TrackManagerTransportTick.cpp  updateAllTracks, LoopEnd commit, transport stop, play/stop track
src/TrackManager/TrackManagerSlotMatrix.cpp     enabled/mute matrices, selection hold, setSelectedSlotIndex, requestSlotSwitch
src/TrackManager/TrackManagerPlaybackPolicy.cpp   track mute/solo + master loop length policy
src/TrackManager/TrackManagerMidiLedFeedback.cpp  Midi LED refresh paths (matches `MidiLedManager`)
src/TrackManager/TrackManagerMemoryPressure.cpp reclaim + background merge release
```

| Module | Process boundary | Est. LOC | Risk |
|--------|------------------|----------|------|
| **TrackManagerCaptureQueue** | When does multi-track **capture** arm, queue, and finalize into a slot? | ~280 | **high** — touches `finalizeCaptureAndSelectSlot`, `admitLoopSlotPersist` |
| **TrackManagerTransportTick** | Per-tick **playback** advance + LoopEnd slot commit + `handleTransportStop` | ~350 | **high** — hot path + persistence gating on stop |
| **TrackManagerSlotMatrix** | **Enabled set** / UI slot focus / pending switch orchestration (calls `SlotStateMachine`) | ~320 | medium |
| **TrackManagerPlaybackPolicy** | Track mute/solo playback gate + master loop length policy | ~120 | low |
| **TrackManagerMidiLedFeedback** | Droid/track Midi LED projection from slot state | ~150 | low |
| **TrackManagerMemoryPressure** | Background merge reclaim under memory pressure | ~100 | medium |

### Phase RN — abbrev cleanup (TrackManager)

| Current | Preferred | When |
|---------|-----------|------|
| `TrackManagerLedFeedback` (planned TU) | `TrackManagerMidiLedFeedback` | Phase 0 scaffold — **do not** create `*LedFeedback*` filename |
| `updateLeds`, `updateLedsDeferred` | `updateMidiLeds`, `updateMidiLedsDeferred` | Phase RN (before Midi LED TU body move) |
| `clearLeds`, `forceLedUpdate` | `clearMidiLeds`, `forceMidiLedUpdate` | same PR |
| `refreshTrackAndLoopSelectLeds` | `refreshTrackAndLoopSelectMidiLeds` | same PR |
| `getLedPhaseSlotIndex` | `getMidiLedPhaseSlotIndex` | touch-and-rename if moved in Midi LED TU |

**Rationale:** `Led` abbreviates hardware feedback; `MidiLed` matches established `MidiLedManager` ([NAMING.md](../Authority/NAMING.md) § no abbreviations in new identifiers).

### Protected paths (architecture gate required)

| Symbol | Why |
|--------|-----|
| `finalizeCaptureAndSelectSlot` | Slot capture stop + persist admit + playback start |
| `updateAllTracks` LoopEnd block | Pending slot switch commit + `ensurePlaybackMergedEventsForSlot` |
| `handleTransportStop` | Mass persist / workspace save policy |

Read [LOOP_MIDI_STORAGE_AND_VALIDATION.md](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) before moving capture or transport-stop bodies.

### PR stack (one phase per PR)

`0 (TrackManagerInternal scaffold) → RN (MidiLed symbol rename) → 6 (PlaybackPolicy) → 7 (MidiLedFeedback) → 5 (SlotMatrix) → 1 (CaptureQueue) → 2 (TransportTick) → 8 (MemoryPressure) → 10 (root trim) → LR (remove deprecated Led shims if any)`

Extract **low/medium** modules before **high** capture/transport slices (same pattern as Track TU plan).

### Do not

- Introduce `CaptureManager` / `PlaybackEngine` top-level classes.
- Duplicate `SlotStateMachine` state — extend `SlotStateMachine` only if **slot index ownership** must move (formal trigger).
- Move `Track::stopRecording` / `commitCaptureForStop` bodies — stay on `Track`.

---

## 2 — NoteMovementUtils translation-unit extraction

**Branch:** `refactor/note-movement-utils`  
**One-line goal:** Split **MIDI pair resolution** from **note-edit geometry apply** while keeping `NoteMovementUtils::applyNoteEditChange` as the single public entry for geometry kinds.

### Architectural north star

From [MOVE_NOTE_LOGIC.md](../Guides/MOVE_NOTE_LOGIC.md):

- **Resolution** — find note-on/off pairs, wrap-head scrub, span length (read-mostly on `MidiEventVec`).
- **Apply** — mutate session store + focus + `NoteGeometryResolver` path via `applyNoteEditChange` / `moveNoteWithOverlapHandling`.

**Do not** merge `reconstructNotes` / `projectNoteEditDisplayNotes` / `editAwareMidiEvents` into one API (#18 explicit non-goal).

### Primary modules

```text
src/EditManager/NoteEditGeometryApply.cpp       applyNoteEditChange + thin re-exports (coordinator ~80 LOC)
include/NoteEditGeometryApply.h                 public API (replaces Utils/NoteMovementUtils.h after LR)
include/NoteEditGeometryApplyInternal.h         pair finder decls shared across TUs
src/EditManager/NoteEditPairResolve.cpp       findCorrespondingNoteOff, findNoteOffPairedAt, resolveNoteOff*, findNoteOn*
src/EditManager/NoteEditWrapHeadAdjust.cpp      wrap-head scrub, open-tail helpers, storageOffTickForSpanEnd
src/EditManager/NoteEditGeometryApplyMutate.cpp moveNoteWithOverlapHandling, changeLengthWithOverlapHandling, applyPitchChange, finalReconstructAndSelect
```

| Module | Architectural question | Est. LOC | Risk |
|--------|------------------------|----------|------|
| **NoteEditPairResolve** | How do we pair note-on/off for edit spans in the session store? | ~280 | low |
| **NoteEditWrapHeadAdjust** | How do wrap-head / open-tail display ticks differ from storage ticks on move? | ~180 | medium |
| **NoteEditGeometryApplyMutate** | How does geometry apply mutate store + focus + selection bracket? | ~500 | **high** — calls `applySelectionFromGeometryEdit`, overlap |

**Placement rationale:** apply modules live under `src/EditManager/` (edit-session owner); colocating pair helpers under `EditManager/` matches NoteEditFocus split. Public façade moves from historical `Utils/` to **`NoteEditGeometryApply`** (action + scope per [NAMING.md](../Authority/NAMING.md)).

### Phase RN — abbrev cleanup (NoteMovementUtils)

| Current | Preferred | When |
|---------|-----------|------|
| `NoteMovementUtils` namespace | `NoteEditGeometryApply` | Phase RN — before pair-resolve body move |
| `include/Utils/NoteMovementUtils.h` | `include/NoteEditGeometryApply.h` + deprecated shim in old path | RN creates shim; LR removes shim |
| `NoteMovementUtilsInternal.h` | `NoteEditGeometryApplyInternal.h` | Phase 0 scaffold |
| `NoteMovementPairResolve` (planned filename) | `NoteEditPairResolve` | Phase 0 — drop redundant `Movement` |
| `NoteMovementWrapAdjust` | `NoteEditWrapHeadAdjust` | Phase 0 — spell out wrap-head domain term |
| `NoteMovementGeometryApply` (two symbols: namespace vs TU) | `NoteEditGeometryApplyMutate` (mutation bodies TU) | Phase 4 geometry-apply slice |

**Keep stable through RN:** `applyNoteEditChange`, `moveNoteWithOverlapHandling`, `changeLengthWithOverlapHandling`, `applyPitchChange` — already action + scope; no rename.

### PR stack

`0 (NoteEditGeometryApplyInternal.h) → RN (namespace + header rename + shims) → 2 (PairResolve) → 3 (WrapHeadAdjust) → 4 (GeometryApplyMutate) → 10 (coordinator trim) → LR (remove NoteMovementUtils shims)`

### Do not

- Rename `applyNoteEditChange` call sites to a new top-level Manager.
- Leave `Utils/` façade permanently — `NoteEditGeometryApply` is the target owner name; shims are temporary only until LR.

---

## 3 — DisplayNoteResolve second-level split

**Branch:** `refactor/display-note-resolve`  
**Context:** [displaymanager_translation_unit_extraction_refinement.md](displaymanager_translation_unit_extraction_refinement.md) Phase 2 **shipped** — `DisplayNoteResolve.cpp` ~864 LOC remains one TU.

**One-line goal:** Mechanical sub-split by **display mode read path** — not a behavioral rewrite of `resolveDisplayNotes`.

### Architectural north star

Display consumes **Runtime Request** (project → window filter). `DisplayManager` composes; `Loop` builds visual cache. See [runtime_process_building_blocks_overview.md](runtime_process_building_blocks_overview.md) Display section.

**Behavioral** split (`resolveNoteEditDisplayNotes`, playing stale-while-revalidate policy) stays on:

- [note_edit_display_commit_stream_refactor_refinement.md](note_edit_display_commit_stream_refactor_refinement.md)
- [multi_track_playback_pressure_closure_refinement.md](multi_track_playback_pressure_closure_refinement.md)

This plan is **body moves only** inside `src/DisplayManager/`.

### Primary modules

```text
src/DisplayManager/DisplayNoteResolve.cpp           mode dispatch + resolveDisplayNotes (~120 LOC coordinator)
src/DisplayManager/DisplayTickResolve.cpp           resolveDisplayTick/LoopLength/PlayheadInLoop, isLiveRecordingDisplay
src/DisplayManager/DisplayNoteResolveLiveCapture.cpp resolveDisplayNotesLiveCapture, capture tails, copySortedCaptureEvents
src/DisplayManager/DisplayNoteResolveCommitted.cpp  committed + NOTE_EDIT overlay branch of resolveDisplayNotes
src/DisplayManager/DisplayNoteWindowGather.cpp      syncDetailedPaintWindow, rebuildDisplayNotesInWindow, wrap-head segment helpers
```

| Module | Process boundary | Est. LOC | Risk |
|--------|------------------|----------|------|
| **DisplayTickResolve** | Map clock tick → display phase / loop length for a slot | ~120 | low |
| **DisplayNoteWindowGather** | Windowed gather + margin policy for long loops | ~200 | medium |
| **DisplayNoteResolveLiveCapture** | Live record/overdub display merge + capture preview | ~280 | medium |
| **DisplayNoteResolveCommitted** | Committed passes + NOTE_EDIT projection overlay | ~260 | medium |

### Phase RN — abbrev cleanup (DisplayNoteResolve)

| Current | Preferred | When |
|---------|-----------|------|
| `DisplayWindowGather` (planned TU) | `DisplayNoteWindowGather` | Phase 0 scaffold or first gather PR |
| `copySortedCaptureEvents` (if split to new helper) | keep — `Capture` is domain noun, not abbrev | — |

New sub-TU names must spell out scope words (`DisplayNote`, `LiveCapture`, `Committed`) — no `Disp`, `Wnd`, `Gather` alone.

### PR stack

`0 (extend DisplayManagerInternal.h) → 2 (DisplayTickResolve) → 3 (DisplayNoteWindowGather) → 4 (LiveCapture) → 5 (Committed) → 10 (coordinator trim)`

### Do not

- Move `Loop::visualCache` build into DisplayManager TUs.
- Change `#CAP DISP` wire format or stale-while-revalidate policy in this extraction.

---

## 4 — NoteEditFocus header hygiene

**Branch:** `refactor/note-edit-focus-header`  
**Context:** [noteditfocus_translation_unit_extraction_refinement.md](noteditfocus_translation_unit_extraction_refinement.md) — **implementation complete**; `include/NoteEditFocus.h` ~423 LOC mixes types + ~40 function decls + inline select templates.

**One-line goal:** Split **types** from **API surface** without moving ownership or changing call sites.

### Proposed headers

```text
include/NoteEditFocusTypes.h     NoteEditFocus, NoteBaseline, OverlapNote, BaselineMap, enums
include/NoteEditFocus.h          #includes Types.h; public function declarations only
include/NoteEditFocusSelect.h    (optional) template select-navigation helpers currently inline in .h
```

| Change | Boundary |
|--------|----------|
| **Types** | Pure data — safe for EditManager, tests, Display projection includes |
| **API decls** | Free functions implemented in `src/EditManager/NoteEditFocus*.cpp` |
| **Templates** | Stay header-only or move to `NoteEditFocusSelect.h` — no .cpp unless explicit instantiation already exists |

### PR stack

Single PR acceptable (low risk): `Types.h` extract + include shims + `NoteEditFocus.h` trim.

### Do not

- Move `NoteEditFocus` struct off `EditSession.focus` member.
- Split implementation TUs further — already done.

---

## Cross-domain placement (all targets)

| Symbol / area | Owner | Misleading name? |
|---------------|-------|------------------|
| `TrackManager::updateAllTracks` | TrackManager transport tick | — |
| `SlotStateMachine::*` | SlotStateMachine | Do not absorb into TrackManager TUs |
| `NoteMovementUtils::applyNoteEditChange` | Edit geometry apply (façade) | **Phase RN** → `NoteEditGeometryApply`; LR removes `Utils/` shim |
| `TrackManager::updateLeds*` / `*Leds` refresh | Midi LED feedback | **Phase RN** → `updateMidiLeds*` / `refreshTrackAndLoopSelectMidiLeds` |
| `resolveDisplayNotes*` | DisplayManager read path | Not Loop storage |
| `projectNoteEditDisplayNotes` | NoteEditFocus display projection | Not DisplayManager paint |
| `applySelectNav` vs `applySelectionFromGeometryEdit` | EditManager — **intentional twins** (documented Phase 3.2) | Do not merge |

---

## Verification policy (implementation phases)

| Target | Per-phase firmware | Native | HITL |
|--------|-------------------|--------|------|
| TrackManager | `pio run -e teensy41-capture-serial` every phase | batch end; full suite before merge; **RN** re-run native | **base** after TransportTick + CaptureQueue phases |
| NoteMovementUtils | every phase | `test_edit_apply`, `test_note_edit_focus`, wrap suites after **RN** + GeometryApplyMutate | **edit** smoke after GeometryApplyMutate |
| DisplayNoteResolve | every phase | `test_display_window_utils`, `test_noteutils_reconstruct` | optional display smoke |
| NoteEditFocus header | compile only | full native once | none |

Reference HITL gates: Phase 2 [`173010`/`173332`](codebase_consistency_maintainability_refinement.md), Phase 3 [`174827`](codebase_consistency_maintainability_refinement.md).

---

## Phase 4 deliverable checklist (#18)

- [x] TrackManager — process boundaries + PR stack documented
- [x] NoteMovementUtils — pair vs apply split documented
- [x] DisplayNoteResolve — second mechanical split documented (behavioral split deferred)
- [x] NoteEditFocus.h — header hygiene path documented
- [x] Prior splits inventory + cross-domain table
- [x] Naming / rename step — Phase 0 table, per-branch Phase RN, LR shims ([NAMING.md](../Authority/NAMING.md), no new abbreviations)
- [x] **Implementation** — `refactor/trackmanager` Phase 0–8 + coordinator trim **merged** PR #22
- [x] **Implementation** — `refactor/note-movement-utils` **merged** PR #23
- [x] **Implementation** — `refactor/display-note-resolve` **merged** PR #24
- [x] **Implementation** — `refactor/note-edit-focus-header` **merged** PR #25
- [x] **Implementation** — Phase LR **merged** PR #26

---

## References

- [refactor_priority_backlog.md](refactor_priority_backlog.md) — add P2 rows when implementation starts
- [legacy_api_retirement_tu_extraction_refinement.md](legacy_api_retirement_tu_extraction_refinement.md)
- [NAMING.md](../Authority/NAMING.md) § Migration policy
