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
    ctor[ctor / setup / getTrack / setSelectedTrack]
    prewarm[prewarmPlaybackRuntime / display cache]
  end
  subgraph capture [TrackManagerCaptureQueue]
    queueRec[queueRecordingTrack / pending record]
    pendStart[handlePendingRecordStart / quantized stop]
    finalize[finalizeCaptureAndSelectSlot]
  end
  subgraph transport [TrackManagerTransportTick]
    update[updateAllTracks main loop]
    loopEnd[LoopEnd pending slot commit in tick]
    transportStop[handleTransportStop]
    playCtl[startPlayingTrack / stopPlayingTrack]
  end
  subgraph slots [TrackManagerSlotMatrix]
    enabled[slotEnabled / slotMuted matrices]
    selectHold[beginSlotSelectionHold / multi-slot commit]
    selectIdx[setSelectedSlotIndex / requestSlotSwitch]
  end
  subgraph mix [TrackManagerMixBus]
    mute[mute / solo / isTrackAudible]
    master[masterLoopLength / autoAlign]
  end
  subgraph led [TrackManagerLedFeedback]
    leds[updateLeds / refreshTrackAndLoopSelectLeds]
  end
  subgraph mem [TrackManagerMemoryPressure]
    reclaim[reclaimUnreferencedDisabledPasses]
    bgRelease[releaseBackgroundPlaybackMergedMidiEventsMemory]
  end
  root --> capture
  root --> transport
  root --> slots
  root --> mix
  root --> led
  root --> mem
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
src/TrackManager/TrackManagerMixBus.cpp         mute/solo/master length
src/TrackManager/TrackManagerLedFeedback.cpp    LED refresh paths
src/TrackManager/TrackManagerMemoryPressure.cpp reclaim + background merge release
```

| Module | Process boundary | Est. LOC | Risk |
|--------|------------------|----------|------|
| **TrackManagerCaptureQueue** | When does multi-track **capture** arm, queue, and finalize into a slot? | ~280 | **high** — touches `finalizeCaptureAndSelectSlot`, `admitLoopSlotPersist` |
| **TrackManagerTransportTick** | Per-tick **playback** advance + LoopEnd slot commit + `handleTransportStop` | ~350 | **high** — hot path + persistence gating on stop |
| **TrackManagerSlotMatrix** | **Enabled set** / UI slot focus / pending switch orchestration (calls `SlotStateMachine`) | ~320 | medium |
| **TrackManagerMixBus** | Audible mix (mute/solo) + master loop length policy | ~120 | low |
| **TrackManagerLedFeedback** | Droid/track LED projection from slot state | ~150 | low |
| **TrackManagerMemoryPressure** | Background merge reclaim under memory pressure | ~100 | medium |

### Protected paths (architecture gate required)

| Symbol | Why |
|--------|-----|
| `finalizeCaptureAndSelectSlot` | Slot capture stop + persist admit + playback start |
| `updateAllTracks` LoopEnd block | Pending slot switch commit + `ensurePlaybackMergedEventsForSlot` |
| `handleTransportStop` | Mass persist / workspace save policy |

Read [LOOP_MIDI_STORAGE_AND_VALIDATION.md](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) before moving capture or transport-stop bodies.

### PR stack (one phase per PR)

`0 (TrackManagerInternal scaffold) → 6 (MixBus) → 7 (LedFeedback) → 5 (SlotMatrix) → 1 (CaptureQueue) → 2 (TransportTick) → 8 (MemoryPressure) → 10 (root trim)`

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
src/Utils/NoteMovementUtils.cpp              applyNoteEditChange + thin re-exports (coordinator ~80 LOC)
include/Utils/NoteMovementUtils.h            public API unchanged
include/Utils/NoteMovementUtilsInternal.h      pair finder decls shared across TUs
src/EditManager/NoteMovementPairResolve.cpp  findCorrespondingNoteOff, findNoteOffPairedAt, resolveNoteOff*, findNoteOn*
src/EditManager/NoteMovementWrapAdjust.cpp     wrap-head scrub, open-tail helpers, storageOffTickForSpanEnd
src/EditManager/NoteMovementGeometryApply.cpp moveNoteWithOverlapHandling, changeLengthWithOverlapHandling, applyPitchChange, finalReconstructAndSelect
```

| Module | Architectural question | Est. LOC | Risk |
|--------|------------------------|----------|------|
| **NoteMovementPairResolve** | How do we pair note-on/off for edit spans in the session store? | ~280 | low |
| **NoteMovementWrapAdjust** | How do wrap-head / open-tail display ticks differ from storage ticks on move? | ~180 | medium |
| **NoteMovementGeometryApply** | How does geometry apply mutate store + focus + selection bracket? | ~500 | **high** — calls `applySelectionFromGeometryEdit`, overlap |

**Placement rationale:** apply modules live under `src/EditManager/` (edit-session owner); pair helpers could stay `Utils/` but colocating under `EditManager/` matches NoteEditFocus split and keeps `NoteMovementUtils.h` as façade.

### PR stack

`0 (NoteMovementUtilsInternal.h) → 2 (PairResolve) → 3 (WrapAdjust) → 4 (GeometryApply) → 10 (root trim)`

### Do not

- Rename `applyNoteEditChange` call sites to a new top-level Manager.
- Delete `NoteMovementUtils` namespace — keep as stable include for ControlSurface + EditManager.

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
src/DisplayManager/DisplayWindowGather.cpp         syncDetailedPaintWindow, rebuildDisplayNotesInWindow, wrap-head segment helpers
```

| Module | Process boundary | Est. LOC | Risk |
|--------|------------------|----------|------|
| **DisplayTickResolve** | Map clock tick → display phase / loop length for a slot | ~120 | low |
| **DisplayWindowGather** | Windowed gather + margin policy for long loops | ~200 | medium |
| **DisplayNoteResolveLiveCapture** | Live record/overdub display merge + capture preview | ~280 | medium |
| **DisplayNoteResolveCommitted** | Committed passes + NOTE_EDIT projection overlay | ~260 | medium |

### PR stack

`0 (extend DisplayManagerInternal.h) → 2 (DisplayTickResolve) → 3 (DisplayWindowGather) → 4 (LiveCapture) → 5 (Committed) → 10 (coordinator trim)`

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
| `NoteMovementUtils::applyNoteEditChange` | Edit geometry apply (façade) | Utils name is historical — optional rename in Legacy Retirement only |
| `resolveDisplayNotes*` | DisplayManager read path | Not Loop storage |
| `projectNoteEditDisplayNotes` | NoteEditFocus display projection | Not DisplayManager paint |
| `applySelectNav` vs `applySelectionFromGeometryEdit` | EditManager — **intentional twins** (documented Phase 3.2) | Do not merge |

---

## Verification policy (implementation phases)

| Target | Per-phase firmware | Native | HITL |
|--------|-------------------|--------|------|
| TrackManager | `pio run -e teensy41-capture-serial` every phase | batch end; full suite before merge | **base** after TransportTick + CaptureQueue phases |
| NoteMovementUtils | every phase | `test_edit_apply`, `test_note_edit_focus` after GeometryApply | **edit** smoke after GeometryApply |
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
- [ ] **Implementation** — separate branches/PRs per target (out of Phase 4 scope)

---

## References

- [refactor_priority_backlog.md](refactor_priority_backlog.md) — add P2 rows when implementation starts
- [legacy_api_retirement_tu_extraction_refinement.md](legacy_api_retirement_tu_extraction_refinement.md)
- [NAMING.md](../Authority/NAMING.md) § Migration policy
