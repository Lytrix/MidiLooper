# Project state (execution context)

**Agents: load first** with [CURRENT_WORK.md](CURRENT_WORK.md). Overwrite frequently — **operational only**, no future milestones (those live in [ROADMAP.md](ROADMAP.md)).

Last updated: 2026-08-11 (RC5e/f STOPPED display handoff)

---

## Current branch

**Active work:** Stage 5 memory/persistence — **5a-3** verification pending; display **RC5e/f** shipped (STOPPED handoff — device verify).
Plan: [`long_overdub_stage5_memory_persistence_bugfix.md`](../Plans/long_overdub_stage5_memory_persistence_bugfix.md); RC5: [`long_overdub_rc5_incremental_display_handoff_investigation.md`](../Plans/long_overdub_rc5_incremental_display_handoff_investigation.md).

Display RC slice closed on [`session_20260811_111528`](../../captures/session_20260811_111528.log).

**Branch tip (local):** `bugfix/long-overdub-display-freeze`

**`chore/firmware-ownership-lifetime-review`** — P0/P1 review **closed** (manual MT gates PASS); Phase 5 + layered **`base`** **parked**.

**`feature/deferred-lazy-load`** — Phase B archived `openspec/changes/archive/2026-07-19-deferred-job-scheduler/`. Specs: `openspec/specs/deferred-job-scheduler/`.

*(Parallel: **`feature/persistence-work-queue`** @ `8b3f93e` — B1–B5 on `dev`.)*

### Firmware lines

| Line | Branch | Role |
|------|--------|------|
| v1 | `main` | Frozen — 16×2, 2-button looper |
| v2 | `midi-faders` | Frozen — iPad/DROID, no PSRAM required |
| v3 | `dev` | Active integration |

See [`docs/BRANCHING.md`](../BRANCHING.md).

## In flight

- **Codebase consistency:** [#18](https://github.com/Lytrix/MidiLooper/issues/18) **NOW** — Phase 4 TrackManager **shipped** PR #22; next extractions in [`codebase_consistency_phase4_extraction_boundaries_refinement.md`](../Plans/codebase_consistency_phase4_extraction_boundaries_refinement.md)
- **StorageManager TU remaining:** **Merged** PR [#17](https://github.com/Lytrix/MidiLooper/pull/17) → `dev` (2026-08-08); closeout [#16](https://github.com/Lytrix/MidiLooper/issues/16)
- **Firmware ownership lifetime review:** **Closed** 2026-08-06 — [`firmware_ownership_lifetime_review.md`](../Plans/firmware_ownership_lifetime_review.md); layered **`base`** HITL **parked** for dedicated refactor
- **HITL CLI rebuild:** Phase 3 — layered presets locked to `base` + `edit_full`; layered **`base`** device PASS deferred to dedicated HITL refactor
- **Edit-session-action-geometry:** **Archived** 2026-08-05 → `openspec/specs/edit-session-action-geometry/`; Phase 5 HITL matrix parked (`m8_edit_note_edit_hitl_automation_refinement.md`)
- **Note-edit control-surface split:** **complete** on `chore/note-edit-control-surface-split` — Phases 0–8; see [note_edit_control_surface_split_refinement.md](../Plans/note_edit_control_surface_split_refinement.md)
- **Next product slice:** confirm with user (persistence/overlay hardening, or parked large-slot display hunt) — see [CURRENT_WORK.md](CURRENT_WORK.md)
- **Archived this branch:** OpenSpec [`deferred-job-scheduler`](../../openspec/changes/archive/2026-07-19-deferred-job-scheduler/) Phase B — gates B.1 [`231510`](../../captures/session_20260718_231510.log), B.3/B.4 [`022107`](../../captures/session_20260719_022107.log); specs in `openspec/specs/deferred-job-scheduler/`
- Memory reclaim + boot/display stack — **merged to `dev`** (PR #4)
- **Hygiene (`chore/codebase-hygiene-sprint1`):** safe debt **complete** — see [`codebase_hygiene_technical_debt_review.md`](../Plans/codebase_hygiene_technical_debt_review.md); sprint plans Status Done + [README index](../Plans/README.md#hygiene-sprint-chorecodebase-hygiene-sprint1)

## Shipped on branch (recent)

| Commit / session | Focus | Docs |
|--------|--------|------|
| `3e9253e` | DEC-029 archive + spec sync + Phase 8 closeout | DELIVERABLE_TRACKING, PHASE8_CLOSEOUT |
| `15c5750` | Playing move/length `refreshPlaybackPreview` | note_edit_playing_move_audition_bugfix |
| `7af8671` | DEC-030 §12 R1–R5 orthogonal state | note_edit_resolver_authority_contracts_refinement |
| `5af41c7` | P1 fold wrap unify; NOTELEN exit bracket; HITL serial proxy | firmware_ownership_lifetime_review |
| `85ae7d6` | P1 undo routing docs (session-gated **E:**) | LOOP_MIDI, ARCHITECTURE_RULES |
| `d87d0c6` + [`010126`](../../captures/session_20260718_010126.log) | Windowed display + queued countdown; device gate PASS | boot_load_windowed plan |
| `8b3f93e` | Sync-drain budget, clear-slot restore, post-reboot undo save | persist queue plan |
| `7a89f03` | Retire monolithic deferred save; work queue (B4) | persist queue plan |
| `b3d066f` | `stepPersistenceWorkItem` scheduler hook (B3) | persist queue plan |
| `3143ef0` | `StorageManager::admit*` API (B2) | persist queue plan |
| `736fa33` | NOTE_EDIT move/pitch bracket — mover NoteId lookup | CURRENT_WORK |
| `0f086f4` | Split-focus slot switching + loop-end commit (DEC-025) | Loops.md |
| `f6b496a` | Slot short-press deferred-restore fix | — |
| `346f7ce` | Playhead projection + undo geometry restore | — |
| `b1d3259` | Loop-scoped undo display (DEC-024 Phase 1) | loop_undo plan |
| `50ad01b` | Cold boot + USB Host defer + slot scan hardening | DEC-021/022 |
| `e40f26c` | DEC-020 Phase 4 mid-pass persistence writer | DEC-020 |
| `f0ee520` | DEC-020 Phase 3 cooperative scheduler HITL | DEC-020 |
| *(prior)* | Phase A step 3 + Phase B — one materialize per revision | handoff |
| `d635296` | Partial Phase A — PLAYING defer, REVT bounds; boot + 16-bar smoke | handoff |
| `eea4734` | Serial evidence index (archived captures) | plan |
| `894d2ea` | Runtime architecture docs + DEC-016 | Architecture/ |
| `d05e736` | Display boot/play OLED freeze | — |
| `b1260ce` | Cold buffers → external memory pool | INTERNAL_HEAP guide |

## Active OpenSpec

| Change | Focus |
|--------|--------|
| **`hitl-cli-rebuild`** | **Phase 3 in progress** — layered `base` + `edit_full`; 3.2 bridge done; next: 3.3 device PASS |
| **`continuous-runtime-persistence`** | Phase 0–4 shipped; Phase 4 HITL **passed**; Phase 5 recovery **parked** (see firmware ownership review) |
| **`runtime-derived-representation-heap`** | M5 Steps 1–1b–2 shipped (`adoptPersistedSnapshot`); lazy load + 64+64 HITL pending |
| **`slot-selection-focus`** | **Shipped (firmware)** — orchestrator + footer extension; manual §8 pending |
| **`unified-interval-projection`** | Phases 1–5 code shipped; native PASS; **5.5 HITL deferred** (DEC-017); Phase **6.1–6.2** synced with geometry (2026-08-04) |
| `set-revision-persistence` | Set revisions, overlay browser — core shipped; loop picker **4.8–4.10** paused |
| `workspace-session-persistence` | Current workspace session model |
| `load-save-overlay-display-regression` | Overlay display fixes |
| `save-status-display` | Deferred save status on display |
| `long-loop-piano-roll-window` | 16-bar piano-roll window + overview; future `Timeline` projection consumer |
| `note-edit-tick-coordinates-and-audition` | Geometry wrap regression: tick spaces + session-store playback preview |
| `linear-loop-tick-storage` | Canonical linear storage + projection alias — prerequisite for UIP |

## Archived OpenSpec (normative specs in `openspec/specs/`)

| Change | Archived | Normative spec |
|--------|----------|----------------|
| **`note-edit-current-state`** | 2026-08-08 | [`note-edit-current-state/`](../../openspec/specs/note-edit-current-state/) — [`PHASE8_CLOSEOUT`](../../openspec/changes/archive/2026-08-08-note-edit-current-state/PHASE8_CLOSEOUT.md) |
| **`edit-session-action-geometry`** | 2026-08-05 | [`edit-session-action-geometry/`](../../openspec/specs/edit-session-action-geometry/) |
| **`deferred-job-scheduler`** | 2026-07-19 | [`deferred-job-scheduler/`](../../openspec/specs/deferred-job-scheduler/) |

**Side fix (2026-07-03):** `note-edit-tick-coordinates-and-audition` — long-loop pitch edit regression at loop wrap; handoff [note_edit_geometry_wrap_regression_bugfix.md](../Plans/note_edit_geometry_wrap_regression_bugfix.md). HITL pending.

**Archived (2026-07-03):** `note-edit-fader-feedback-regression` → `openspec/changes/archive/2026-07-03-note-edit-fader-feedback-regression/`; normative spec `openspec/specs/note-edit-fader-feedback/`. Phase 12–13 + RC11 §8.3–8.4 deferred.

**Archived (2026-07-03):** `note-edit-stable-note-id` → `openspec/changes/archive/2026-07-03-note-edit-stable-note-id/`; normative spec `openspec/specs/note-edit-stable-note-id/`.

**Handoffs (shipped):** [note_edit_fader_feedback_next_steps_handoff.md](../Plans/archive/handoff/note_edit_fader_feedback_next_steps_handoff.md), [note_edit_stable_note_id_phase_a_handoff.md](../Plans/archive/handoff/note_edit_stable_note_id_phase_a_handoff.md)

Parked (not active): `currentset-savedset-storage-layout`

## Current constraints

- Record/overdub stop: no full validate; deferred SD save only
- Default build env: `teensy41-capture-serial`; upload only after user confirms
- Base module (encoder + 4 GPIO) capable in principle; DROID is extension only
- Governance docs do not change firmware by themselves
- Do not implement from [ROADMAP.md](ROADMAP.md) — scope is [CURRENT_WORK.md](CURRENT_WORK.md) only
- **`edit-session-action-geometry`** — **archived** 2026-08-05; normative `openspec/specs/edit-session-action-geometry/`
- **`note-edit-current-state`** — **archived** 2026-08-08; normative `openspec/specs/note-edit-current-state/`

## Current blockers

- `currentset-savedset-storage-layout` parked until revision model stable — see CURRENT_WORK

## Accepted decisions (summary)

Full log: [DECISION_LOG.md](../DECISION_LOG.md).

- Authority: `docs/Authority/` → OpenSpec → Guides → plans → code
- Gesture-first; five gestures per button
- passes[] per slot (v4+); NoteEditSession for live edit RAM
- Current auto-save; Save appends Set revision without clearing Current
- Overlay confirm: Edit short + encoder short only (DEC-001)
- Persistence RAM model: `StorageSession` on `StorageManager` — request/held/dispatched revision load (DEC-012, **shipped**)

## Current architecture notes

- Persistence owner: `StorageManager` — no parallel save Manager without reassessment (DEC-008)
- **`StorageSession`** (DEC-012): job RAM in `Internal.cpp`; overlay in `Overlay.cpp`; FSM in `WorkspaceSave.cpp`, `RevisionCommit.cpp`, `RevisionLoad.cpp`; orchestrator in `StorageManager.cpp`
- HITL serial line buffer in RAM (FLASHMEM `processHitlSerialCommands` cannot access DMAMEM on IMXRT1062)
- **Capture-serial RAM budget (2026-07-04):** note-edit overlap code in FLASHMEM (`NoteEditMem.h`); deferred `#DBG`/`REVT`/`PERF` via 96 KB PSRAM ring in `DebugSessionCapture` — see [capture_serial_ram1_recovery_extmem_debug_enhancement.md](../Plans/capture_serial_ram1_recovery_extmem_debug_enhancement.md)
- Normative specs: `openspec/specs/revision-load/`, `storage-session-jobs/`, `storage-session-layout/`
- **Next persistence layout:** `transport.bin` / `global.bin` — not started (post DEC-012)
- **Interval projection:** `IntervalProjection` engine owns all wrap math — **`include/Utils/IntervalProjection.h`** + **`src/Utils/IntervalProjection.cpp`**; Phases 1–5.1–5.4 + 5.6 shipped (grep gate on D7 consumers); `Track.projectionCycleStartTick` + queued start at grid; `PlaybackCursor` retired; **5.5 HITL** pending
- `ButtonManager` / GPIO dormant (DEC-005)
- M8 edit + pool-budget archived to `openspec/specs/`
