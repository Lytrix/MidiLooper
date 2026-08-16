# Project state (execution context)

**Agents: load first** with [CURRENT_WORK.md](CURRENT_WORK.md). Overwrite frequently — **operational only**, no future milestones (those live in [ROADMAP.md](ROADMAP.md)).

Last updated: 2026-08-16 (NOTE_EDIT Layer A pin — copy-then-trim)

---

## Current branch

**NOTE_EDIT entry while overdubbing:** `openNoteEditSession` calls `stopOverdubbing()` first — live capture commits before rematerialize (quick fix).

**NOTE_EDIT lengthened loop select:** committed-content span for bracket/nav; skip detailed-window filter on selectable inventory. Fixture [`015731`](../../captures/session_20260814_015731.log).

**Overdub + NOTE_EDIT exit bake:** device PASS [`025322`](../../captures/session_20260814_025322.log) — `rows=1 saved=1`, session flats 234 held. Native `dropUnrequestedSessionStoreDeletes`. Stage 3 unblocked.

**OLED first-frame mismatch:** RC3 device PASS [`014553`](../../captures/session_20260814_014553.log). Plan: [`oled_dma_partial_frame_bugfix.md`](../Plans/oled_dma_partial_frame_bugfix.md).

**Loop content-only history (DEC-035):** **Archived** 2026-08-14 — OpenSpec `2026-08-14-loop-content-history-persistence`; normative `openspec/specs/loop-content-history/`. Device PASS [`030147`](../../captures/session_20260814_030147.log) / [`032227`](../../captures/session_20260814_032227.log).

**NOTE_EDIT leave-restore painted span:** RC1 native shipped; device gate open — [`note_edit_overlap_leave_restore_painted_span_bugfix.md`](../Plans/note_edit_overlap_leave_restore_painted_span_bugfix.md). [`200154`](../../captures/session_20260813_200154.log) Restore 5 `720–2255` / select `DNTE` 1535; same-loop [`193838`](../../captures/session_20260813_193838.log) first-select paints length **47**.

**NOTE_EDIT mover wrap-length jump:** RC1 device PASS in [`200154`](../../captures/session_20260813_200154.log) — [`note_edit_mover_wrap_length_jump_bugfix.md`](../Plans/note_edit_mover_wrap_length_jump_bugfix.md). No `2351`/`2975`. Note 14 `2256–2304` stays on the wrap-stub plan.

**NOTE_EDIT note-off pairing LIFO:** native shipped; [`213920`](../../captures/session_20260813_213920.log) multi-overlap device PASS — [`note_edit_note_off_pairing_lifo_bugfix.md`](../Plans/note_edit_note_off_pairing_lifo_bugfix.md). @45.971 seals five overlap rows plus the mover (`canonical=7 pre_commit=7`).

**NOTE_EDIT edit-pass replay row payload:** native shipped; [`211832`](../../captures/session_20260813_211832.log) device PASS — [`note_edit_replay_row_payload_bugfix.md`](../Plans/note_edit_replay_row_payload_bugfix.md). `Length 115 48→287` and `Length 115 1008→1103` seal and hold. Rows replay as stored.

**NOTE_EDIT overlap shorten commit seal:** native shipped; device gate open — [`note_edit_overlap_shorten_commit_seal_bugfix.md`](../Plans/note_edit_overlap_shorten_commit_seal_bugfix.md). Deselect commit dropped the overlap `Length` row on a parked mover; [`204700`](../../captures/session_20260813_204700.log) @166.809 `canonical=1 apply_owned=2` then @166.848 seals `Length 10 960–1247` against note 14's focus. Slice B of the resolver contracts plan **withdrawn** — [`225025`](../../captures/session_20260807_225025.log) `len=287` was the correct truncation.

**NOTE_EDIT Length replay loop-boundary:** native shipped; device gate open — [`note_edit_length_replay_loop_boundary_bugfix.md`](../Plans/note_edit_length_replay_loop_boundary_bugfix.md). Persisted `ChangeLength` 14 `2256–2304` replayed as a wrap; same-pitch notes shortened to 2255. No note ends at 2255 in [`204700`](../../captures/session_20260813_204700.log) / [`205054`](../../captures/session_20260813_205054.log).

**NOTE_EDIT wrap-stub commit:** RC3 native shipped; device gate open — [`note_edit_overlap_action_drop_and_wrap_stub_bugfix.md`](../Plans/note_edit_overlap_action_drop_and_wrap_stub_bugfix.md). RC2 device FAIL: 14 still `2256–2304` when cache paints a non-zero span. RC1 withdrawn (67.824 min-length stay-hidden).

**NOTE_EDIT display unification:** Stages 1–2 and 5–9 shipped — [`note_edit_visual_cache_display_unification_refinement.md`](../Plans/note_edit_visual_cache_display_unification_refinement.md). Device [`192007`](../../captures/session_20260813_192007.log): 45 Hide/Restore `1440–1511` (not 2160); 76 as target still `840–2160`; first NOTE_EDIT `DISP` 59/60; resolve 16.8–52.3 ms. Paint gap and undo-warm stay open.

**Active work:** NOTE_EDIT Layer A pinned — [`note_edit_undo_warm_missing_recon_investigation.md`](../Plans/note_edit_undo_warm_missing_recon_investigation.md) (`snapshotFocusForSessionUndo` copies 110 then keeps 1). LED lookup Stage 1 PASS [`114736`](../captures/session_20260816_114736.log). Consumer grooming Slice 1–4c device PASS ([`142548`](../captures/session_20260816_142548.log)); 4e parked. Overdub-stop drain shipped ([`post_overdub_playing_midi_drain_bugfix.md`](../Plans/post_overdub_playing_midi_drain_bugfix.md)). **FinalizeWorkspace slice** shipped (`48bd36f`). LoopPersist finalize **reverted** (`baa03e1`). **6A.1 HITL PASS** [`025651`](../captures/session_20260816_025651.log). **038.2 landed.** Not all of LCR live.

**Merged to `dev`:** PR [#30](https://github.com/Lytrix/MidiLooper/pull/30) overdub overlap; PR [#29](https://github.com/Lytrix/MidiLooper/pull/29) Stage 5a-1/5a-2 + display RC4–RC5.

**Stage 5a-3:** **`pool_alloc` proof abandoned** (2026-08-12) — see [`long_overdub_stage5a3_critical_reclaim_verification_refinement.md`](../Plans/long_overdub_stage5a3_critical_reclaim_verification_refinement.md).

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
- **Long record display freeze:** **NOW** — [`long_record_onset_display_freeze_bugfix.md`](../Plans/long_record_onset_display_freeze_bugfix.md); evidence [`012342`](../../captures/session_20260812_012342.log)
- **Next product slice:** HITL CLI Phase 3 or persistence overlay — see [CURRENT_WORK.md](CURRENT_WORK.md)
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
| **`loop-content-resolution`** | **DEC-037 native prototype** — `resolveState` / `resolveWindow`; materialize stays until three gates |
| **`loop-effective-event-source`** | DEC-036 D1+D2+3b **closeout done** — overdub entry PASS; successor is `loop-content-resolution` |
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
| **`loop-content-history-persistence`** | 2026-08-14 | [`loop-content-history/`](../../openspec/specs/loop-content-history/) |
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
- **`loop-content-history-persistence`** — **archived** 2026-08-14; normative `openspec/specs/loop-content-history/`

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
- **Runtime scheduling:** contract [`runtime_scheduling_admission_model_architecture.md`](../Plans/runtime_scheduling_admission_model_architecture.md); roadmap [`runtime_scheduling_owner_boundary_admission_refinement.md`](../Plans/runtime_scheduling_owner_boundary_admission_refinement.md). Interval reservation is **not** authorized. Persist `admit*` and `DeferredJobScheduler` (DEC-027) remain separate.
