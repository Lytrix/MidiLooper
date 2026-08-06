# DisplayManager translation-unit extraction

**Kind:** refinement  
**Branch:** `refactor/displaymanager` (from `dev`)  
**Parent context:** [codebase_hygiene_technical_debt_review.md](codebase_hygiene_technical_debt_review.md), [multi_track_playback_pressure_closure_refinement.md](multi_track_playback_pressure_closure_refinement.md), [runtime_process_building_blocks_overview.md](runtime_process_building_blocks_overview.md)  
**Prerequisite merged:** PR #9 — StorageManager translation-unit extraction (`refactor/storagemanager` → `dev`)

---

## One-line goal

Shrink `src/DisplayManager.cpp` from a ~3.3k-line monolith into a thin frame orchestrator by moving cohesive draw/read domains into `src/DisplayManager/*.cpp`, **one phase per PR**, behavior-preserving, no ownership or lifecycle changes.

---

## Baseline (2026-08-06)

| Artifact | LOC / status |
|----------|----------------|
| `src/DisplayManager.cpp` | **~295** (root TU; was ~3300) |
| `src/DisplayManager/BootDisplay.cpp` | **~130** (Phase 5) |
| `src/DisplayManager/SidebarAndInfo.cpp` | **~578** (Phase 4) |
| `src/DisplayManager/PianoRollDraw.cpp` | **~552** (Phase 3) |
| `src/DisplayManager/DisplayNoteResolve.cpp` | **~864** (Phase 2a–2c) |
| `src/DisplayManager/LoadSaveOverlay.cpp` | **~906** (Phase 1) |
| `src/DisplayManager/DisplayColdHelpers.cpp` | **~68** (Phase 0) |
| `include/DisplayManagerInternal.h` | **~48** (Phase 0 + open-tail helpers) |
| Related split already shipped | [`include/HitlDisplayBridge.h`](../../include/HitlDisplayBridge.h) — HITL serial entry points only |

### Target end state

| Artifact | Target |
|----------|--------|
| `DisplayManager.cpp` | **~400–600** — `update()`, `setup()`, boot gate, ctor, thin delegates |
| New TUs (this plan) | **~2700** moved out in 6 phases |
| `DisplayManager` instance | **unchanged** — `_display`, `liveDisplayNotes`, load/save caches stay on the class |
| Ownership / transitions | **unchanged** — hygiene only |

---

## Rules (every phase)

1. **Behavior-preserving** — no changes to `resolveDisplayNotes` semantics, defer gates, or overlay FSM wiring during extraction.
2. **Architecture checkpoint** — ownership change **NO**, state transition change **NO** before first edit ([architecture-checkpoint-bugfix](../../.cursor/rules/architecture-checkpoint-bugfix.mdc)).
3. **Pre-implementation review** — trace symbols with `rg`; post gate table in PR description ([Plan-Pre-Implementation-Review](../../.cursor/rules/Plan-Pre-Implementation-Review.mdc)).
4. **Verification gate** — `pio test -e native` (828/828); `pio run -e teensy41-capture-serial`; phase-appropriate manual / HITL smoke when overlay or `#CAP DISP` touched.
5. **One phase per session / PR** — do not mix unrelated extractions.
6. **State stays on `DisplayManager`** — move **method bodies** only; do not introduce `DisplayView` / `PianoRollRenderer` manager classes without user approval ([Naming-Vocabulary](../../.cursor/rules/Naming-Vocabulary-Teensy-Looper.mdc)).
7. **Headers** — declare moved file-static helpers and shared buffers in [`include/DisplayManagerInternal.h`](../../include/DisplayManagerInternal.h) (new); keep [`include/DisplayManager.h`](../../include/DisplayManager.h) public surface stable unless a rename is already scoped elsewhere.
8. **Member access** — extracted TUs implement `DisplayManager::method` out-of-line; they use `this->` / private members the same as today (friend + include internal header if needed — prefer keeping methods as `DisplayManager::` in `.cpp` files only).

### Do not split (yet)

| Area | Reason |
|------|--------|
| `DisplayManager::update` orchestration | Stays in root until Phases 1–5 land; single frame pipeline owner |
| `resolveDisplayNotes` **mode sub-resolvers** (behavioral split) | Separate track — [note_edit_display_commit_stream_refactor_refinement.md](note_edit_display_commit_stream_refactor_refinement.md), M6 perf in [multi_track_playback_pressure_closure_refinement.md](multi_track_playback_pressure_closure_refinement.md) |
| `Loop::visualCache` / capture preview ownership | `Loop` builds; `DisplayManager` composes — do not move cache build into display TUs |
| SSD1322 driver / `SSD1322_Config` | Out of scope |

---

## Phase map (highest ROI first)

```text
Phase 0  Internal scaffold + cold helpers   (~150 LOC)
Phase 1  Load/save overlay                  (~800 LOC)
Phase 2  Display note resolve + capture     (~1000 LOC)
Phase 3  Piano roll draw + window geometry    (~550 LOC)
Phase 4  Sidebar, info strip, track column  (~450 LOC)
Phase 5  Boot screen                          (~200 LOC)
Phase 6  Cache invalidation lifecycle         (~200 LOC)
         ─────────────────────────────────────
         DisplayManager.cpp  3300 → ~400–600
```

Milestone after **Phase 1**: root TU **~2500 LOC** (largest decoupled block removed).  
Milestone after **Phase 2**: root TU **~1500 LOC** (hot read path isolated for future mode split).  
Milestone after **Phases 0–6**: root TU within target band.

---

## Phase 0 — `DisplayManagerInternal` scaffold (~150 LOC)

**Status:** Done on `refactor/displaymanager`.

**New files**

| File | Role |
|------|------|
| `include/DisplayManagerInternal.h` | Extern buffers + cold-path helper declarations |
| `src/DisplayManager/DisplayColdHelpers.cpp` | Anonymous-namespace helpers moved from root TU |

### Move (file-static today, lines ~42–92)

| Symbol | Role |
|--------|------|
| `shouldAvoidFullVisualRebuild` | Long-loop policy gate |
| `shouldDeferHeavyDisplayRebuild` | Undo / deferred-save soft defer |
| `rebuildDisplayNotesInWindow` | Windowed `DisplayNote` reconstruction |
| `resolveTrackIndex` | Track index from `Track&` |
| `liveDisplayEventBuffer` | `DMAMEM SessionMidiEventVec` — declare `extern` in internal header; **single definition** in cold helpers TU |

### Keep in root

- `displayManager` global (`DMAMEM DisplayManager displayManager`)
- All `DisplayManager` member fields in `DisplayManager.h`

### Verify

- `pio test -e native`; `pio run -e teensy41-capture-serial`
- Compile-only is sufficient if zero behavior change

**PR title:** `refactor(displaymanager): add DisplayManagerInternal scaffold and cold helpers`

---

## Phase 1 — `LoadSaveOverlay.cpp` (~800 LOC) — **shipped** (`refactor/displaymanager`)

**Priority:** Highest ROI — self-contained overlay UI; minimal coupling to piano-roll hot path.

### Move

| Symbol | Role |
|--------|------|
| `drawLoadSaveDetailUnsavedMarker`, `copyLoadSaveBrowserLabel`, `loadSaveDividerX`, `loadSaveDetailContentX`, … | Layout constants + file-static helpers (~109–197) |
| `refreshLoadSaveListCache`, `invalidateLoadSaveRevisionListCache`, `refreshLoadSaveRevisionHistoryCache`, `ensureLoadSaveRevisionListCache` | List caches |
| `invalidateLoadSaveDetailCache`, `resolveLoadSaveWorkspaceDetail` | Detail cache |
| `handleLoadSaveOverlayPress`, `adjustLoadSaveListSelection`, `adjustLoadSaveListSelectionInDrillMode`, `confirmLoadSaveFocusedRow` | Input / navigation |
| `resolveFocusedRootSetId`, `resolveFocusedRevisionId` | Focus helpers |
| `drawLoadSaveTrackFilledBar`, `drawLoadSaveDetailMetric*`, `drawLoadSaveDetailDateColumn`, `drawLoadSaveWorkspaceDetail` | Detail metrics |
| `drawAutoSaveBeforeLoadToast`, `refreshAutoSaveBeforeLoadToast` | Toast |
| `drawLoadSaveDirtyPromptView`, `drawLoadSaveMinimalLoadingView`, `drawLoadSaveRevisionHistoryView`, `drawLoadSaveLoopPickView`, `drawLoadSaveView` | Overlay views |
| `drawLoadSaveRowLoadStatusDots` | Row load-status dots (overlay rows) |
| `openRevisionHistoryFromHitl`, `navigateLoadSaveOverlayBackFromHitl` | HITL bridge targets |

### Member state (stays on `DisplayManager`)

`loadSaveListEntries_`, `loadSaveRevisionListEntries_`, `loadSaveListSelection_`, `loadSaveDetailCache_*`, `autoSaveBeforeLoadToast*`, `loadSaveModeWasActive_`, etc.

### Boundaries

- `update()` load/save branch stays in root but calls `drawLoadSaveView` in extracted TU.
- [`StorageManager`](../../include/StorageManager.h) overlay API unchanged — display only reads getters.
- [`HitlDisplayBridge.h`](../../include/HitlDisplayBridge.h) continues to forward into `DisplayManager` public methods.

### Verify

- Manual: enter load/save overlay, scroll list, drill revision history, dirty prompt, minimal loading spinner
- HITL overlay preset if available (`host_midi_hitl` layered UIP gate)
- Serial: `#CAP` overlay / revision load lines unchanged

**PR title:** `refactor(displaymanager): extract load/save overlay drawing and caches`

---

## Phase 2 — `DisplayNoteResolve.cpp` (~1000 LOC) — **shipped** (`refactor/displaymanager`, sub-phases 2a → 2b → 2c)

**Priority:** Second — largest cohesive **read** domain; isolates hot path for a later **behavioral** mode split (optional sub-phases below).

**Commits:** `f439eaa` (2a tick/window), `a4bbf6f` (2b live capture + open-tail helpers), `bcf693a` (2c playback/NOTE_EDIT dispatch + `#CAP DISP`).

### Move

| Symbol | Role |
|--------|------|
| `isLiveRecordingDisplay`, `resolveDisplayLoopLength`, `resolveDisplayTick`, `resolveLoopOriginTick`, `resolvePlayheadInLoop` | Tick / length helpers |
| `syncDetailedPaintWindow`, `resolveWindowedDisplayNotes` | Bounded window gather |
| `resolveDisplayNotes` | Full mode dispatch (delegates to `resolveDisplayNotesLiveCapture` for live capture) |
| `resolveDisplayNotesLiveCapture` | Live record / overdub branch (Phase 2b) |
| `applyCapturePlayheadTails`, `applyLiveOpenTails`, `applyRecordingPreviewOpenTails` | Open-tail helpers (`DisplayManagerInternal`) |
| `resolveBracketDisplayTick`, `resolveDrawHighlightIndex` | NOTE_EDIT bracket/highlight helpers (`DisplayManagerInternal`; shared with `drawNoteInfo` until Phase 4) |
| `emitDisplayCaptureSnapshot` (both overloads), `maybeEmitDisplayCaptureOnChange` | `#CAP DISP` telemetry |

### Optional sub-phases (if single PR > ~1200 LOC diff)

| Sub | Scope | ~LOC |
|-----|--------|------|
| **2a** | Tick helpers + `resolveWindowedDisplayNotes` + window gather | ~250 |
| **2b** | Live record / overdub branch inside `resolveDisplayNotes` | ~350 |
| **2c** | NOTE_EDIT + PLAYING / STOPPED branches | ~400 |

Sub-phases must land **in order 2a → 2b → 2c** on one branch; do not merge 2b before 2a.

### Member state (stays on `DisplayManager`)

`liveDisplayNotes`, `liveDisplayCache*`, `liveWindowGather*`, `livePlaybackDisplaySlot_`, `detailedWindowStartTick_`, `detailedWindowBars_`, etc.

### Do not change during extraction

- Stale-while-revalidate / `deferVisualRebuild` gates
- M6 capture-active merge replacements ([multi_track_playback_pressure_closure_refinement.md](multi_track_playback_pressure_closure_refinement.md) Phase 2) — **separate PR after** Phase 2 extract

### Verify

- HITL base preset (`#CAP DISP`, `DNTE` where applicable)
- Manual: record → stop → play; overdub warm cache; NOTE_EDIT piano roll; 32+ bar windowed roll
- Reference: [record_overdub_memory_display_timeline_enhancement.md](record_overdub_memory_display_timeline_enhancement.md), [boot_load_windowed_display_reconstruction_refinement.md](boot_load_windowed_display_reconstruction_refinement.md)

**PR title:** `refactor(displaymanager): extract display note resolve and capture snapshot`

---

## Phase 3 — `PianoRollDraw.cpp` (~550 LOC) — **shipped** (`refactor/displaymanager`)

### Move

| Symbol | Role |
|--------|------|
| `tickToScreenX`, `noteToScreenY` | Coordinate mapping |
| `drawGridLines`, `drawAllNotes`, `drawNoteBar`, `drawBracket`, `drawOverviewStrip` | Roll primitives |
| `shouldAutoFollowDetailedWindow`, `centerDetailedWindowOnPlayhead`, `resolveDetailedWindow` | Window geometry (paint-aligned) |
| `drawPianoRoll` | Piano roll composer |

### Verify

- Long-loop overview strip + detailed window ([long_loop_piano_roll_overview_enhancement.md](long_loop_piano_roll_overview_enhancement.md))
- Playhead + bracket in NOTE_EDIT

**PR title:** `refactor(displaymanager): extract piano roll drawing and window geometry`

---

## Phase 4 — `SidebarAndInfo.cpp` (~450 LOC) — **shipped** (`refactor/displaymanager`)

### Move

| Symbol | Role |
|--------|------|
| `drawTrackStatus` | Track column letters |
| `drawSidebar`, `resolveSidebarMode`, `sidebarModeLabel`, `resolveMidiOutput`, `midiOutputLabel` | Right column mode / transport |
| `drawPersistenceStatusDots`, `drawSaveStatusIndicator` | Save/load status dots |
| `drawInfoArea`, `drawInfoField` | Bottom info strip (BPM, CHN, LEN, undo) |
| `drawNoteInfo` | Selected note readout |

### Verify

- Sidebar save-status dots ([save-status-display OpenSpec](../../openspec/changes/save-status-display/))
- NOTE_EDIT `U:` / bracket display

**PR title:** `refactor(displaymanager): extract sidebar and info strip drawing`

---

## Phase 5 — `BootDisplay.cpp` (~200 LOC) — **shipped** (`refactor/displaymanager`)

### Move

| Symbol | Role |
|--------|------|
| `drawScaledFixedMonoChar`, `bootTitleScaleX`, `bootTitleScaleY`, … | Boot anonymous helpers (~1210–1280) |
| `drawBootScreen`, `beginBootOled`, `finishBootSetup` | Boot OLED path |

### Notes

- Boot helpers may stay file-static in `BootDisplay.cpp` anonymous namespace.
- `bootScreenVisible_`, `bootSetupComplete_`, `bootScreenHoldUntilMs_` remain on `DisplayManager`.

### Verify

- Cold boot title screen; transition to main UI after hold ([BOOT_LOAD.md](../Guides/BOOT_LOAD.md))

**PR title:** `refactor(displaymanager): extract boot screen drawing`

---

## Phase 6 — `DisplayCacheLifecycle.cpp` (~200 LOC)

### Move

| Symbol | Role |
|--------|------|
| `invalidateForSlotChange`, `refreshViewportAfterRecordStop` | Slot switch / record-stop viewport |
| `invalidateLiveDisplayCache`, `invalidateNoteEditDisplayCache` | Frame cache drops |
| `applyWorkspaceDisplayRefreshPending`, `requestNoteInfoRefresh` | Workspace reload refresh |

### Keep in root

- `update()` — calls invalidation hooks; owns 30 FPS gate and `loadSaveActive` branch

### Verify

- Slot switch during playback; workspace reload after revision load
- Record stop viewport recenter ([capture_display_lag_record_stop_playhead_bugfix.md](capture_display_lag_record_stop_playhead_bugfix.md))

**PR title:** `refactor(displaymanager): extract display cache invalidation lifecycle`

---

## Optional follow-up (not in ROI order)

| Item | Notes |
|------|--------|
| `resolveDisplayNotes` mode sub-resolvers | Behavioral refactor after Phase 2 — `resolveLiveCaptureDisplayNotes`, `resolveNoteEditDisplayNotes`, `resolvePlayingDisplayNotes` |
| M6 display defer / capture-active merge | [multi_track_playback_pressure_closure_refinement.md](multi_track_playback_pressure_closure_refinement.md) — **not** mixed into extract PRs |
| [note_edit_display_commit_stream_refactor_refinement.md](note_edit_display_commit_stream_refactor_refinement.md) Phase A | Single projection owner — coordinate with NOTE_EDIT resolve split |
| `clearDisplayBuffer` debug serial noise | Optional trim when touching root TU |
| `shouldDeferFullDisplayVisualRebuild` in `DisplayManager.cpp` root | Already a thin wrapper — inline or move with Phase 4 if desired |

---

## Per-phase checklist (copy into PR)

```markdown
## Architecture gate
- Owner: DisplayManager (unchanged)
- Invariant: frame pipeline order unchanged (track status → notes → piano roll → sidebar → info)
- Ownership change: NO
- Transition change: NO
- Reuse: YES — move bodies to `src/DisplayManager/<Phase>.cpp`

## Pre-implementation review
- [ ] `rg <symbol>` — all call sites listed
- [ ] No change to resolveDisplayNotes defer / merge semantics
- [ ] `DisplayManagerInternal.h` updated when moving file-static helpers

## Tests
- [ ] `pio test -e native`
- [ ] `pio run -e teensy41-capture-serial`
- [ ] Manual / HITL: <phase-specific smoke>
```

---

## Suggested PR stack on `refactor/displaymanager`

| # | Phase | Base | Parallel-safe |
|---|-------|------|----------------|
| 1 | Phase 0 — scaffold | `dev` | — |
| 2 | Phase 1 — load/save overlay | Phase 0 | yes (after 0) |
| 3 | Phase 2 — note resolve | Phase 0 | yes (after 0; parallel with 1 if no merge conflicts) |
| 4 | Phase 3 — piano roll draw | Phase 0 | after 2 recommended (shared window helpers) |
| 5 | Phase 4 — sidebar / info | Phase 0 | parallel with 3 |
| 6 | Phase 5 — boot | `dev` | parallel with 1–4 |
| 7 | Phase 6 — cache lifecycle | Phase 2 | after note resolve |

**Recommended serial path for one agent:** **0 → 1 → 2 → 3 → 4 → 5 → 6**.  
Phases **1 and 5** can run in parallel after Phase 0. Phase **3** should follow Phase **2** so `syncDetailedPaintWindow` / `resolveDetailedWindow` edits do not conflict.

---

## References

- [Guides/control-surface/Display.md](../Guides/control-surface/Display.md) — draw entry points
- [NOTE_WRAPPING_LOGIC.md](../Guides/NOTE_WRAPPING_LOGIC.md) — live wrap / tail display
- [LOOP_MIDI_STORAGE_AND_VALIDATION.md](../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md) — capture display read path
- [storagemanager_translation_unit_extraction_refinement.md](storagemanager_translation_unit_extraction_refinement.md) — pattern reference (static façade vs instance class)
- [docs/00-authority/ARCHITECTURE_RULES.md](../00-authority/ARCHITECTURE_RULES.md) — DisplayManager ownership row
