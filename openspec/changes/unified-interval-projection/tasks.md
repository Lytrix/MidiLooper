# Tasks — unified-interval-projection

## 0. OpenSpec and docs

- [x] 0.1 `proposal.md`, `design.md`, delta specs, `tasks.md` (this change)
- [x] 0.2 Handoff [`docs/plans/unified_interval_projection_enhancement.md`](../../docs/plans/unified_interval_projection_enhancement.md) — includes **Agent execution notes** (one chat per phase)
- [x] 0.3 Update [`CURRENT_WORK.md`](../../docs/runtime/CURRENT_WORK.md) + [`PROJECT_STATE.md`](../../docs/runtime/PROJECT_STATE.md) — UIP active, overlap paused
- [x] 0.4 Update [`derived_note_overlap_logic_handoff.md`](../../docs/plans/derived_note_overlap_logic_handoff.md) — dependency on UIP Phases 1–5
- [x] 0.5 OpenSpec refinement — D13–D16, tick vocabulary, naming (`loopStartTick`, `selectedTick`, `queuedStartTick`)
- [x] 0.5a Coordinate model refinement — `TickInterval`, `window` interval, Stage 1/2 separation, identity invariant, Display projection vs rendering
- [x] 0.6 Mark `edit-session-action-geometry` tasks blocked until Phase 6
- [x] 0.7 Prior research appendix in `design.md` + [`unified_interval_projection_sequencer_prior_research_refinement.md`](../../docs/plans/unified_interval_projection_sequencer_prior_research_refinement.md)
- [x] 0.5b LTS architecture refinement — Start/End events, capture/storage/playback split, UIP integration, PlaybackTarget, Manager vs Track
- [x] 0.5c D23 — window is **`TickInterval`** frame; never **`ProjectedNoteInterval`**
- [ ] 0.8 **Doc sync rule:** each phase task that adds/changes button or gesture logic includes a matching control-surface doc update (`README.md` cheat sheet + relevant `docs/Guides/control-surface/*.md`)

## 1. Core engine (Phase 1)

- [x] 1.1 Add `include/Utils/IntervalProjection.h` — `TickInterval`, `ProjectionType`, `ProjectionContext` (core + extensions), `CanonicalNoteSpan`, `ProjectedNoteInterval`
- [x] 1.2 Add `src/Utils/IntervalProjection.cpp` — `generateEquivalentIntervals`, `selectProjectedInterval`, `selectProjectedIntervalsForDisplay`, `projectNoteIntervals`
- [x] 1.3 k bounds from `window` intersect test — no fixed k cap; deterministic candidate order
- [x] 1.4 Centralize phase helpers — `tickPhaseInLoop`, `noteRelativeTick`, `noteStorageTick` (migrate from `SelectNavigation` / consumers)
- [x] 1.5 Native `test/test_interval_projection/` — 900→1080 @ L=960 equivalents; selection table per `ProjectionType`; wrap advance; mid-cycle length
- [x] 1.6 `generateEquivalentIntervals` has no consumer policy beyond k bounds

## 2. Edit projection (Phase 2)

- [x] 2.1 Add `buildEditProjectionContext` — `EditorSelection.primaryNote`, `selectedTick`, v1 full-loop analysis window
- [x] 2.2 Add `projectEditIntervalsForAnalysis` — batch Edit projection for overlap orchestrator
- [x] 2.3 Reuse/port brownfield linear-span logic from `NoteEditFocus` into engine; no parallel wrap paths
- [x] 2.4 Port `test_note_edit_focus` wrap/linear fixtures through Edit projection
- [x] 2.5 Retire `EditSessionInteraction.wraps` (D6 — no conditional gate)
- [x] 2.6 **Do not** start `edit-session-action-geometry` firmware

## 3. Display projection (Phase 3)

- [x] 3.1 Add `projectDisplayNotes()` — Stage 2 display selection + rendering (head/tail split, live capture open-tail)
- [x] 3.2 Refactor `reconstructNotesImpl` to delegate to `projectDisplayNotes()`; preserve outward `DisplayNote` API
- [x] 3.3 Migrate `DisplayWindowUtils` to **`TickInterval`** intersection on **post-projection** `DisplayNote` list (D22 — not head/tail)
- [x] 3.3a Migrate **`DetailedWindowContext`** to **`TickInterval`** (display viewport only)
- [x] 3.4 Extend `test_noteutils_reconstruct` — parity with pre-migration fixtures; head/tail boundary
- [x] 3.5 Extend `test_display_window_utils` — viewport filter distinct from head/tail split

## 4. Playback projection + queued start (Phase 4)

- [x] 4.1 Refactor playback event ordering via Playback projection; full rolling `projectionCycleStartTick`
- [x] 4.2 Preserve NOTE_EDIT Tier 2 — `sessionMidiEvents()` sole source when edit active
- [x] 4.3 Preserve `invalidateCaches` / `sessionPreviewRevision_` contract
- [x] 4.4 Implement per-track `queuedStartTick` + configurable grid quantize (default 16th)
- [x] 4.5 Bar press + slot button queued start — mutually exclusive, last wins at grid
- [x] 4.6 Pending slot replacement — last press wins; reset `queuedAtTick`
- [x] 4.7 Design session: **resolved** — LTS Start/End events; UIP integration; **`LoopTriggerSequenceManager`**; per-Track **`projectionCycleStartTick`**
- [x] 4.8 Native or HITL playback-order check — linear off at loop head
- [x] 4.9 Update control-surface docs for queued-start / bar-press button logic (`README.md` cheat sheet, `Bars-and-16ths.md`, `Main-controls.md` as applicable)
- [x] 4.10 **Retire `PlaybackCursor`** (D21) — move stale revision cache to **`LoopPlaybackRuntime`**; delete duplicate fields; remove **`loopHeadWindow`**; trim dead **`PlaybackWindow`** fields (D22); grep gate
- [x] 4.11 Migrate **`playMidiEvents`** phase/wrap/sort to **`IntervalProjection`** + **`projectionCycleStartTick`**; keep **`Loop.nextEventIndex`** as event scan index

## 5. Integration + HITL (Phase 5 — core migration gate)

- [ ] 5.1 Retire or thin-wrap duplicate helpers: `resolveLinearNoteSpanForOverlap`, `isInflatedDisplaySpan` (edit path)
- [ ] 5.2 Grep gate — no consumer-local wrap math outside `IntervalProjection` helpers
- [ ] 5.3 Migrate `MidiLedManager` / `SelectNavigation` phase modulo to centralized helpers
- [ ] 5.4 `pio test -e native` full suite
- [ ] 5.5 HITL: 152335 move-across-boundary; long-loop display; NOTE_EDIT playback audition; slot queued start (short-press)
- [ ] 5.6 Update LOOP_MIDI guide NOTE_EDIT § — Edit projection replaces `normalizeWrapToLinear` bullet

## 6. Unblock overlap (Phase 6)

- [ ] 6.1 Sync `edit-session-action-geometry` delta specs — step 3 = Edit projection; remove task 1.2a `normalizeWrapToLinear`
- [ ] 6.2 Sync `edit-session-action-geometry/design.md` D20 → Edit projection reference
- [ ] 6.3 Resume [`derived_note_overlap_logic_handoff.md`](../../docs/plans/derived_note_overlap_logic_handoff.md) at Phase 1 types
- [ ] 6.4 `/opsx:archive` → `openspec/specs/unified-interval-projection/`; absorb `loop-wrap-projection` alias language

## 7. LoopTriggerSequence (after core migration — Phase 7)

- [ ] 7.0 Architecture — **`LoopTriggerSequenceManager`**; Start/End event model; UIP integration (no local wrap); **`triggerSequenceTick`** rules; Manager vs Track
- [ ] 7.1 Types — **`TriggerEventType`** Start/End; **`PlaybackTarget`**; **`TriggerPlaybackSnapshot`**; **`LoopTriggerSequence`**; initializer adapters separate
- [ ] 7.2 **LoopTriggerSequence Edit Mode** — build phase; edit **without stop** during playback; idle default 2 bars; local undo
- [ ] 7.3 **triggerSequenceRecord** / **triggerSequenceOverdub** — 4-press lifecycle; capture **Start**/**End** on **`triggerSequenceTick`**; overdub **End** before next **Start**
- [ ] 7.4 **triggerSequencePlay** — **End**/**Start** step transition; build **`ProjectionContext`** from **Start** snapshot → **`IntervalProjection`** on **`Track`**
- [ ] 7.5 Bar/16th jam → slot loop metadata (empty short / filled long overwrite)
- [ ] 7.6 Midi capture routing — track REC in LTS Edit Mode falls back to Loop Edit for **`midiEvents`**; LTS row for trigger capture only
- [ ] 7.7 Global undo — track + slot in normal/play; ignored during build phase; dedicated trigger-sequence undo stack
- [ ] 7.8 Retire `pendingMultiSlotCommit`; retire **jamLoop** / **Jams** naming
- [ ] 7.8a Rename control surface **Jams** row → **LoopTriggerSequence** row — `README.md`, `Jams.md` → `LoopTriggerSequence.md`, `midilooper_v1.ini`, `MidiConfig.h` / `MidiButtonConfig.cpp`
- [ ] 7.9 SD persist/recall **`LoopTriggerSequence`** presets
- [ ] 7.10 HITL record + play + overdub + exit/cue gestures
- [ ] 7.11 Update control-surface docs — LTS Edit Mode, build/record/overdub/play, capture routing

## 8. Loop slice metadata (after LoopTriggerSequence — Phase 8)

- [ ] 8.1 Hold 16th A → press 16th B — temporary projection window; audition on release; loop on next 16th
- [ ] 8.2 Persist 16th-granularity window on **Loop** (verse/chorus slices)
- [ ] 8.3 **`TriggerEvent`** entries may reference Loop slice metadata
- [ ] 8.4 Native + HITL slice audition/loop
- [ ] 8.5 Update control-surface docs — 16th slice + **`TriggerEvent`** capture gestures

## Verification checklist

```bash
pio test -e native
pio test -e native -f test_interval_projection
pio test -e native -f test_noteutils_reconstruct
pio test -e native -f test_display_window_utils
pio test -e native -f test_note_edit_focus
openspec validate unified-interval-projection
```

HITL (Phase 5):

```bash
.venv/bin/python scripts/host_midi_hitl.py run --preset edit_minimal \
  --midi-out "Teensy" --midi-in "Teensy" \
  --serial-port /dev/cu.usbmodem154944801 --track 5
```

Firmware build (ask before upload): `pio run -e teensy41-capture-serial`

## Blocked until this change completes

| Change | Blocked work |
|--------|----------------|
| `edit-session-action-geometry` | Phases 1–4 firmware; `EditSessionAction.h`, analyze, resolver, apply, wire |
