# Current work (implementation scope)

**Highest operational priority.** Defines what to implement **now**. Load with [PROJECT_STATE.md](PROJECT_STATE.md) before planning or coding.

Last updated: 2026-08-08 (§12 R1–R5 + pitch-overlap bugfix closed)

---

## Now implementing

### Note edit resolver — orthogonal-state representation (§12) — **complete**

**Plan:** [`docs/plans/note_edit_resolver_authority_contracts_refinement.md`](../plans/note_edit_resolver_authority_contracts_refinement.md) §12  
**Decision:** DEC-030; behavioral migration **complete**

| Phase | Status |
|-------|--------|
| Stages 0–8 + §11 5.1–5.5 | **Done** |
| R1–R5 orthogonal representation | **Done** (native 968/968) |
| Pitch-then-overlap stub Hide skip | **Done** — HITL [`session_20260808_112202`](../../captures/session_20260808_112202.log) @88.669 |

`ParticipatingNotePhase` removed. `NoteEditPresenceType` retained as row storage encoding. `NOTE_EDIT_PROJECTED_STORE_COMPAT` remains a separate track.

Confirm next with user — `note-edit-current-state` Phase 8 archive prep, or persistence/overlay hardening.

### Note edit current state — ownership transfer (`note-edit-current-state`)

**OpenSpec:** [`openspec/changes/note-edit-current-state/`](../../openspec/changes/note-edit-current-state/)  
**Preflight:** [`PREFLIGHT.md`](../../openspec/changes/note-edit-current-state/PREFLIGHT.md)  
**Decision:** DEC-029

| Phase | Status |
|-------|--------|
| 1 — Architecture gate and preflight | **Done** (2026-08-07) |
| 2 — Current-state foundation (read-only build + projection) | **Done** (2026-08-07) |
| 3 — API split and compatibility gates | **Done** (2026-08-07) |
| 4 — Reader migration | **Done** (2026-08-07) |
| 5 — Writer migration | **Done** (2026-08-07) |
| 6 — Undo/redo and folded capture | **Done** (2026-08-07) |
| 7 — Commit and compatibility removal | **Done** (2026-08-07) |
| 8 — Verification and archive prep | Next |

Confirm next slice with user when parallel work conflicts — candidates: HITL CLI Phase 3, persistence/overlay hardening.

### Firmware ownership / lifetime review — **closed (2026-08-06)**

**Branch:** `chore/firmware-ownership-lifetime-review`  
**Plan:** [`docs/plans/firmware_ownership_lifetime_review.md`](../plans/firmware_ownership_lifetime_review.md)

| Item | Status |
|------|--------|
| P0 materialize stale | Done — MT-P0 conditional PASS |
| P1 undo docs + routing | Done — MT-P1-undo PASS (`session_20260806_003023.log`) |
| P1 fold wrap unify + NOTELEN exit bracket | Done — `5af41c7`; MT-P1-fold manual PASS |
| P1 display-audition | Done — MT-P1-display-audition manual PASS |
| Phase 5 recovery | **Parked** — MT-P5 deferred |
| Hygiene | Done — native 828/828; NOTE_EDIT smoke PASS |
| Layered **`base`** HITL | **Parked** — dedicated HITL refactor |

### Note edit undo after reboot — **done**

**Plan:** [`.cursor/plans/note_edit_undo_reboot_58ef9394.plan.md`](../../.cursor/plans/note_edit_undo_reboot_58ef9394.plan.md)

| Item | Status |
|------|--------|
| Global undo **STK1** scoped-edit serialization round-trip | Done |
| `EditManager::markCurrentEditBatchDurable` + autosave/depart call sites | Done |
| Native tests (serialization, A/B/C checkpoint order) | Done |
| LOOP_MIDI durability invariant + **E:** vs **U:** docs | Done |
| HITL reboot-during-NOTE_EDIT | **PASS** — [`session_20260805_212234.log`](../../captures/session_20260805_212234.log): durable checkpoint + `LoopPersist` + post-reboot **U:** restored prior state |

### Edit HITL from scratch — parked

**Plan:** [`docs/plans/m8_edit_note_edit_hitl_automation_refinement.md`](../plans/m8_edit_note_edit_hitl_automation_refinement.md)

Deferred from **edit-session-action-geometry** Phase 5 (D14 full matrix). Interim smoke: `edit_minimal` preset only. Requires new layered edit HITL presets (base + 2× overdub fixture, per-interaction matrix).

### HITL CLI rebuild — Phase 3 (`base` + `edit_full` only)

**OpenSpec:** [`openspec/changes/hitl-cli-rebuild/`](../../openspec/changes/hitl-cli-rebuild/)  
**Plan:** [`docs/plans/hitl_cli_rebuild_enhancement.md`](../plans/hitl_cli_rebuild_enhancement.md)

| Phase | Status |
|-------|--------|
| 0 — inventory + doc scaffold | **Done** (2026-08-04) |
| 1 — foundation (`HITL_ARCHITECTURE.md`, layered runner, actions, flows) | **Done** (2026-08-04) |
| 2 — core scenarios + UIP 5.5 (historical) | **Done** (2026-08-04) |
| 3 — layered **`base`** + **`edit_full`** only | **In progress** — 3.2 bridge stabilized (2026-08-04); next: 3.3 device PASS |
| 4–5 — corpus docs, archive | Pending |

**Layered presets (active):** `base` (`record_overdub`), `edit_full`. Do **not** wire `edit_minimal`, `revision_*`, `load_save_*`, `fader_motor_*`, etc. in this change.

**Phase 3.2 done:** `LayeredLegacyBridge.edit_full_args` includes slot targets + Mode B follow; `run_edit_baseline(..., owns_resources=False)` reuses layered MIDI/serial without opening a second collector; host tests in [`scripts/test_edit_full_layered.py`](../../scripts/test_edit_full_layered.py).

**Phase 3 exit:** Mode B device PASS for `--layered --preset base` and `--layered --preset edit_full`. Keep `legacy_edit_baseline` bridge until `edit_full` no longer needs it.

### Recently archived — edit-session-action-geometry (2026-08-05)

**Archive:** [`openspec/changes/archive/2026-08-05-edit-session-action-geometry/`](../../openspec/changes/archive/2026-08-05-edit-session-action-geometry/)  
**Normative specs:** [`openspec/specs/edit-session-action-geometry/`](../../openspec/specs/edit-session-action-geometry/), updated [`note-edit-modification-session`](../../openspec/specs/note-edit-modification-session/spec.md)

Phases 1–4.10 shipped (pipeline, canonical commit, display-first F1 motors `adf9209`). Phase 5.1–5.2 HITL matrix **parked** — see Edit HITL plan above.

---

### Persistence / overlay (next after Phase B)

**Branch:** `feature/deferred-lazy-load` (Phase B archived — pick next from ROADMAP / CURRENT_WORK with user)

| Item | Status |
|------|--------|
| DeferredJobScheduler Phase B | **Archived** `openspec/changes/archive/2026-07-19-deferred-job-scheduler/` |
| Specs | `openspec/specs/deferred-job-scheduler/`, updated `lazy-slot-hydration` |
| Gates | B.1 [`231510`](../../captures/session_20260718_231510.log); B.3/B.4 [`022107`](../../captures/session_20260719_022107.log) |

**Parked hang hunt:** [`persistence_overlay_large_slot_focus_restore_bugfix.md`](../plans/persistence_overlay_large_slot_focus_restore_bugfix.md) — CAP flush fix kept; unreproducible OLED/stale after focus load.

**Next:** confirm next CURRENT_WORK slice with user (persistence/overlay hardening, or parked large-slot display hunt).

### Hygiene — codebase debt review (sprint complete)

**Plan:** [`docs/plans/codebase_hygiene_technical_debt_review.md`](../plans/codebase_hygiene_technical_debt_review.md) — safe hygiene **complete**; gated leftovers remain.

**Branch:** `chore/codebase-hygiene-sprint1`

| Item | Status |
|------|--------|
| Review + dead Looper / fader stubs / layout / HITL / vocab / merge-cache rename | Done |
| Track stop DRY | Done — [`track_stop_dry_refinement.md`](../plans/track_stop_dry_refinement.md) |
| Display `copySortedCaptureEvents` | Done |
| Playback cursor advance DRY | Done — [`playback_cursor_advance_dry_refinement.md`](../plans/playback_cursor_advance_dry_refinement.md) |
| Shared `RecordStopLength` | Done — [`record_stop_length_shared_helpers_refinement.md`](../plans/record_stop_length_shared_helpers_refinement.md) |
| Clear-slot re-arm after playing clear | Done — [`clear_slot_rearm_after_playing_clear_bugfix.md`](../plans/clear_slot_rearm_after_playing_clear_bugfix.md) (`1cb7ffa`) |
| Plans hygiene (Status Done + README index; no mass purge) | Done |

**Next hygiene (gated / optional):** StorageManager `saveState` extract; `PersistenceQueue` rename; mass `docs/plans/` purge; leave `isTrackAudible` for later.

### Note edit control-surface split — **complete**

**Branch:** `chore/note-edit-control-surface-split` (`d3505db`)  
**Plan:** [`docs/plans/note_edit_control_surface_split_refinement.md`](../plans/note_edit_control_surface_split_refinement.md)

| Item | Status |
|------|--------|
| Phases 0–4 (split, edit events, surface extraction) | Done |
| Phase 5 unnest `LoopEditManager` | Done |
| Phase 6 rename `ControlSurfaceManager` | Done (`d3505db`) |
| Phase 7 docs + hygiene closeout | Done (`c244280`) |
| Phase 8 NOTE_EDIT physical ingress | Done (uncommitted) |

**Supersedes:** rename-only `NoteEditManager` plan (removed with `chore/rename-note-edit-manager` branch).

---

### OpenSpec — slot-performance-interaction Phase −1 (2026-07-19)

Rename-only merge-cache vocabulary (no behavior change). Remaining phases stay queued.

| Item | Status |
|------|--------|
| −1.1–−1.4 rename struct/field/APIs | Done |
| −1.5 native tests | Done (663) |

### Recently closed — Deferred job scheduler (Phase B)

**Archived:** `2026-07-19-deferred-job-scheduler`  
**Specs synced:** `deferred-job-scheduler` (new), `lazy-slot-hydration` (gate note).

| Step | Status |
|------|--------|
| B.1 thin `runFrame` | PASS [`231510`](../../captures/session_20260718_231510.log) |
| B.2 `stepSubmittedLoadJobs` | Done |
| B.3 select then step + `LoadLoopSelectionPolicy` | Done |
| B.4 native + device | PASS [`022107`](../../captures/session_20260719_022107.log) |

### Prioritized boot load isolation (merged to `dev`)

Shipped via PR #4 on `feature/memory-pressure-reclaim`.

---

### Recently closed — Unified commit lazy slot load (Phase A)

**Archived:** `2026-07-18-unified-commit-lazy-slot-load`  
**Specs synced:** `lazy-slot-hydration`, `loop-commit-semantics`, `multi-loop-slots`, `playback-runtime-prewarm`, `timeline-passes`.

| Item | Status |
|------|--------|
| A.6 parse split + A.7 PSRAM headroom fix | **PASS** [`230145`](../../captures/session_20260718_230145.log) |
| 6.2-device interactive | **PASS** [`224607`](../../captures/session_20260718_224607.log) |
| Parked | 6.1 DERIVED_READY; 6.3 undo/import Commit docs |

---

### Recently closed — Slot queue LOOP_EDIT depart

**Plan:** [`docs/plans/slot_queue_loop_edit_depart_bugfix.md`](../plans/slot_queue_loop_edit_depart_bugfix.md)

| Item | Status |
|------|--------|
| No mid-play geometry write on LOOP_EDIT depart | **PASS** |
| Device gate (queue slot, LoopEnd commit, no hang) | **PASS** [`004331`](../../captures/session_20260718_004331.log), reconfirmed [`010126`](../../captures/session_20260718_010126.log) |
| Queued-launch musical-time countdown | **In tree** — OLED field; LoopEnd commits present in `010126` |

---

### Paused — Memory pressure reclaim (M6 follow-on)

**Branch:** `feature/memory-pressure-reclaim`  
**Plan:** [`docs/plans/memory_pressure_reclaim_refinement.md`](../plans/memory_pressure_reclaim_refinement.md)

| Phase | Scope | Status |
|-------|--------|--------|
| **1A** | Advisory FSM + `#CAP,DIAG,pressure` | **Shipped** (`f09f547`) — validated in [`session_20260714_233620.log`](../../captures/session_20260714_233620.log) |
| **1B** | Low reclaim (`try*` owner APIs; background-first) | **Shipped** — pending manual gate |
| 2 | Critical undo trim + persistence inversion | Paused |
| 3 | Replace scattered heap thresholds | Paused |
| 4 | Manual gate 215312; idle defer; OpenSpec archive | Paused |

**Manual gate (1B):** Re-run stress session; confirm reclaim under Low without playback glitches / dropped MIDI on selected track.

---

## Recently landed (M6 Ph 1–2 — merged to `dev`)

**Commits:** `50e0f6e`…`11025ca` on `dev`

| Item | Status |
|------|--------|
| Phase A metrics | Shipped |
| Phase 1 playback window (DEC-016 chunk merge) | Shipped |
| Phase 2 display stale-while-revalidate | Shipped |
| Record-start headroom + live-record display | Shipped |
| Native | **608/608** |
| Manual | **PASS** no crash — [`session_20260714_215312.log`](../../captures/session_20260714_215312.log) (64-bar, 7-track overdub; 42 append failures remain) |

**Plan:** [`multi_track_playback_pressure_closure_refinement.md`](../plans/multi_track_playback_pressure_closure_refinement.md)

---

## Recently landed (persistence work queue — on `dev`)

**Branch:** `feature/persistence-work-queue` — [`current_set_persist_work_item_queue_enhancement.md`](../plans/current_set_persist_work_item_queue_enhancement.md)

| Phase | Status |
|-------|--------|
| B1–B4 | Work queue admission, scheduler, monolith retire |
| B4 follow-up | Sync-drain budget, clear-slot SD-restore, urgent save / mid-pass defer |
| B5 | **604/604** native; HITL **PASS** [`host_midi_automation_serial_20260714_153240.log`](../../captures/host_midi_automation_serial_20260714_153240.log) |

---

## Recently landed on `dev` (pre-queue)

### Bugfix: record-stop MIDI flood — **restore baseline playback, pending HITL**

**Evidence:** [`session_20260713_165557.log`](../../captures/session_20260713_165557.log) — 571 note-ons over 5 pitches in 704 ticks; BPM collapse; `RING,overflow`.

**Fix:** Restored `901c4d9` playback send/anchor (`reanchorPlaybackProjection`, inline `atLoopStart`, `loop.midiEvents()` window). Kept display storage-frame playhead + persistence grace.

**Plan:** [`docs/plans/record_stop_playback_hang_bugfix.md`](../plans/record_stop_playback_hang_bugfix.md) (phase 4c)

| Gate | Status |
|------|--------|
| Native | **562/562** |
| `teensy41-capture-serial` build | **SUCCESS** (2026-07-13) |
| HITL / manual | **User** — record→stop→PLAYING; no MO flood; playhead bar 0 |

---

### Bugfix: record-stop coordinate frame (phase 4) — **superseded by 4c flood fix**

---

### Bugfix: overdub wrap note-off pairing — **implemented, pending HITL**

**Follow-up to capture ownership refactor** ([`overdub_wrap_note_off_pairing_bugfix.md`](../plans/overdub_wrap_note_off_pairing_bugfix.md)). Fixes wrong on/off pairing when notes are held across loop wrap.

**Changes:** `capturePhaseTick` / `appendCaptureNoteOffAtPhase`; finalize clears pending only after append; canonical `wrappedHeadOff`; stop diagnostics; 13 native tests in `test_capture_note_off_rules`.

| Gate | Status |
|------|--------|
| Native | **554/554** |
| `teensy41-capture-serial` build | **SUCCESS** |
| HITL | **User** — 132536 scenario: tail wrap pairs tail on before wrap, not N@0 |

---

### Bugfix: overdub wrap note-off capture ownership — **implemented, pending HITL**

**Scope:** Single close pipeline for open notes at record/overdub stop; playback read-only (no mid-wrap capture mutation). Aligns live capture with `buildCanonicalSpansFromMidi` wrapped linear storage.

**Changes:**
- Removed `closeOpenNotesAtLoopWrap`, `flushPendingNotesIntoCapture`, `removeCaptureNoteOffAt`
- `finalizePendingNotes(currentTick)` on all record/overdub stop paths (playhead close, not L-1)
- `Loop::sealCapture` passes playhead `openTailCloseTick` to `finalizeWrapWindowOnStore`
- Removed `recordMidiEvents` note-off repair (tick bump, L-1 removal)
- New native suite `test_capture_note_off_rules` (7 tests)

**Plan:** [`docs/plans/overdub_wrap_note_off_capture_bugfix.md`](../plans/overdub_wrap_note_off_capture_bugfix.md)

**Deferred:** playback wrap `double_on` ([`session_20260709_224935.log`](../../captures/session_20260709_224935.log)) — separate plan; do not touch `playMidiEventsForSlot` / shared `projectionCycleStartTick`.

| Gate | Status |
|------|--------|
| Native | **554/554** |
| `teensy41-capture-serial` build | **SUCCESS** |
| HITL wrap + capture | **User** — re-run 125437 scenario: balanced SEVT per pitch, no ch4 ghost offs, no lag regression |

---

### Bugfix: loop-wrap playback lag regression — REVERTED to `bc98491` (shipped `901c4d9`)

**Decision:** `19aa47a` ("Fix loop-wrap playback retrigger") and both follow-up wrap-tail rewrites (full-order scan; cursor drain) are **reverted**. `Track::playMidiEvents`, `Track::playMidiEventsForSlot`, `IntervalProjection` (`shouldPlaybackEmitWrapTailEvent` / `shouldPlaybackCrossEvent` helpers), and their tests are restored to `bc98491` — the state the user confirmed "worked perfectly." `closeOpenNotesAtLoopWrap` on overdub playback wrap is restored.

**Proof the wrap fix regressed playback (not the active track):** during the active track's overdub, background **slots** flooded ~1 MO per main-loop frame via `playMidiEventsForSlot`, starving the loop and freezing display ≈2 s ("lag around loop wrap"). MO by channel — good [`session_20260713_115434.log`](../../captures/session_20260713_115434.log) vs bad [`session_20260713_122107.log`](../../captures/session_20260713_122107.log): ch5 (active) 212→223 unchanged; ch4 (slot) 486→**3620**; ch4 ~1 ms-gap events 278→**3422**. `19aa47a` introduced it (ch4 486→2488); cursor drain worsened it (→3622).

**Deferred (not reintroduced yet):** original wrap **double-on** ([`session_20260709_224935.log`](../../captures/session_20260709_224935.log) — MO `double_on` at BAR, 54-bar loop). Any future fix must NOT touch per-frame slot playback / the shared one-per-track `projectionCycleStartTick`.

**Out of scope:** slot/bar queued seek `sendAllNotesOff`; unify pending-note flush at record/overdub stop; stop FSM changes.

| Gate | Status |
|------|--------|
| Native | **542/543** (known empty `test_capture_note_off_rules` suite) |
| `teensy41-capture-serial` build | **SUCCESS** |
| HITL wrap + stop | **User** — compare to 115434: no per-frame slot MO flood, no BAR bunching, no ~120 ms post-stop DISP lag |

---

### OpenSpec: [`continuous-runtime-persistence`](../../openspec/changes/continuous-runtime-persistence/) (DEC-020) — **paused** until wrap fix lands

**Branch:** `dev` @ `644de4f` (recovery stack merged 2026-07-09)

| Phase | Status |
|-------|--------|
| **0** Diagnostics | **Complete** |
| **1** Chunk lifecycle | **Complete** |
| **2** Persistence queue | **Complete** |
| **3** Cooperative scheduler | **Complete** — overdub-stop HITL passed (`f0ee520`) |
| **4** Mid-pass persistence | **Shipped** — native + **HITL passed** [`session_20260709_171043.log`](../../captures/session_20260709_171043.log) (64+64); boot restore [`session_20260709_171951.log`](../../captures/session_20260709_171951.log) |
| **5** Recovery | **Next** — longest valid prefix load + quarantine tail — [**handoff**](../plans/continuous_runtime_persistence_phase5_recovery_handoff.md) |
| **6** Full 64+64 HITL | **Mostly evidenced** (same log) — archive checklist + `oldestDirtyChunkAge` review remain |

| Gate | Owner | Status |
|------|-------|--------|
| Native | Agent | `pio test -e native` — **540/540** (post-merge) |
| `test_storage_loop_io` | Agent | Run before Phase 4 sign-off |
| Phase 4 HITL (64+64 record/overdub) | **User** | **PASS** — [`session_20260709_171043.log`](../../captures/session_20260709_171043.log): 282× `PERS,mid_pass`, 9× `PERS,result,...,ok`, `freeChunk` 105–136 |
| Phase 4 HITL (cold-boot restore) | **User** | **PASS** — [`session_20260709_171951.log`](../../captures/session_20260709_171951.log): `BOOT,load,ok`, deferred restore slot `4/0`, `DISP` 41472 ticks, playback |
| Phase 5 native fixture | Agent | Partial persisted pass load — not started |

**HITL policy:** stop/crash scenarios — user captures serial manually (`capture_session.py`); agent does not loop HITL. On crash, user bisects and shares log before stop-path patches.

| Doc | Role |
|-----|------|
| Agent map | [RUNTIME_STORAGE_AND_PERSISTENCE.md](../Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md) |
| **Phase 5 handoff** | [continuous_runtime_persistence_phase5_recovery_handoff.md](../plans/continuous_runtime_persistence_phase5_recovery_handoff.md) |
| OpenSpec | [continuous-runtime-persistence](../../openspec/changes/continuous-runtime-persistence/) |

---

## Landed on branch (recovery merge — no further agent work unless HITL fails)

| Area | Commit / note | HITL |
|------|----------------|------|
| NOTE_EDIT move/pitch bracket | `736fa33` | User: `session_20260709_155307` replay |
| Split-focus slot switching | `0f086f4` (DEC-025) | User: preview slot + loop-end commit |
| Slot short-press / deferred restore | `f6b496a` | User |
| Playhead + undo geometry | `346f7ce` | User: `session_20260708_233241` |
| Loop-scoped undo (DEC-024 Ph 1) | `b1d3259` | Native only |
| Boot load + slot scan | `50ad01b` | User: cold boot ×5 |
| Slot clear + arm state | `2a565aa` | User |

---

## Parked

### OpenSpec: [`unified-capture-commit-owner`](../../openspec/changes/unified-capture-commit-owner/) (DEC-023)

Phases 1–3 prototype **reverted** at `40db4df` (boot bisect). Storage boot recovery helpers (`37f6b00`) landed. Retry Track/Loop slices only after stable boot + user approval.

### Derived note overlap (`edit-session-action-geometry`)

**Phase 4 wired** (2026-08-04): `runEditSessionGeometryPipeline` in move/length/pitch paths; baseline-vs-live pre-commit rows.

**Remaining Phase 4:** 4.3a Add/Delete, 4.5 retire `overlapNotes`, 4.5b `filterSelectableDisplayNotes`.

**Next:** Phase 4 closeout or Phase 5 HITL matrix.

Handoff: [`derived_note_overlap_logic_handoff.md`](../plans/derived_note_overlap_logic_handoff.md)

### Prior: [`runtime-derived-representation-heap`](../../openspec/changes/runtime-derived-representation-heap/)

M6 Ph 1–2 on `dev`; pressure reclaim + Ph 3–4 in [`memory_pressure_reclaim_refinement.md`](../plans/memory_pressure_reclaim_refinement.md).

---

## Do not start without decision

- DEC-023 capture-commit Track slices (after Phase 5 or explicit user go)
- `currentset-savedset-storage-layout`
- D13 jam-recording (ROADMAP — post JamRecorder)
