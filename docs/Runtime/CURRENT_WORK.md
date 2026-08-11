# Current work (implementation scope)

**Highest operational priority.** Defines what to implement **now**. Load with [PROJECT_STATE.md](PROJECT_STATE.md) before planning or coding.

Last updated: 2026-08-12 (Phase 2 G2 slice 2 wired; STK2 GUS)

---

## Now implementing

### Overdub pass overlap resolution (OpenSpec)

**Branch:** `feature/overdub-pass-overlap-resolution`  
**OpenSpec:** [`openspec/changes/overdub-pass-overlap-resolution/`](../../openspec/changes/overdub-pass-overlap-resolution/)  
**Evidence plan:** [`long_overdub_wrap_duplicate_display_freeze_bugfix.md`](../Plans/long_overdub_wrap_duplicate_display_freeze_bugfix.md)  
**Capture:** [`183525`](../../captures/session_20260811_183525.log) — 191× `duplicate` after wrap; not `pool_alloc`

**Now:** **G2 Phase 2 slice 2 complete (uncommitted)** — overdub NoteOff → `accumulatePendingNoteChangesForIncomingNote`; stop seal Shorten/Hide → EditPass companions; one `OverdubPassAdded` undo (+ GUS **STK2** companions); restore gate skipped when `hasOverdubSourceView()`. Native **1015/1015**. Next: Phase 2 remaining (3.7 demote reverse-tick/`isDuplicateCaptureEvent`, 3.8 shared min length) then Phase 3 device verify. U1/U2 out of scope.

### Stage 5 — memory / persistence pressure (merged to `dev` via PR #29)

**5a-1/5a-2 (shipped on `dev`):** Critical reclaim during transport + authoritative append-deny CAP.  
**5a-3:** [`183525`](../../captures/session_20260811_183525.log) falsifies reclaim for `duplicate` class; `pool_alloc` proof still open.  
**Display RC4–RC5 + live-record tick-0:** shipped on `dev` (PR #29).

### Codebase consistency & maintainability — Phase 4 + LR complete

**GitHub:** [#18](https://github.com/Lytrix/MidiLooper/issues/18) · **Plan:** [`codebase_consistency_phase4_extraction_boundaries_refinement.md`](../Plans/codebase_consistency_phase4_extraction_boundaries_refinement.md)  
**Shipped:** TrackManager #22, NoteEditGeometryApply #23, DisplayNoteResolve #24, NoteEditFocus header #25, Phase LR #26. **Next:** close #18; pick next queue item (HITL CLI Phase 3 or persistence — see table below).

### StorageManager TU extraction — shipped (PR #17)

**GitHub:** [#16](https://github.com/Lytrix/MidiLooper/issues/16) — closed · [PR #17](https://github.com/Lytrix/MidiLooper/pull/17) merged to `dev`

### HITL CLI rebuild — Phase 3 (`base` + `edit_full` only)

**OpenSpec:** [`openspec/changes/hitl-cli-rebuild/`](../../openspec/changes/hitl-cli-rebuild/)  
**Plan:** [`docs/Plans/hitl_cli_rebuild_enhancement.md`](../Plans/hitl_cli_rebuild_enhancement.md)

| Phase | Status |
|-------|--------|
| 0–2 — foundation, scenarios | **Done** (2026-08-04) |
| 3 — layered **`base`** + **`edit_full`** | **In progress** — 3.2 bridge stabilized; next: 3.3 device PASS |
| 4–5 — corpus docs, archive | Pending |

**Layered presets (active):** `base` (`record_overdub`), `edit_full`. Do **not** wire `edit_minimal`, `revision_*`, `load_save_*`, `fader_motor_*`, etc. in this change.

**Phase 3 exit:** Mode B device PASS for `--layered --preset base` and `--layered --preset edit_full`.

### Persistence — pick one track (not all parallel)

**Guides:** [`RUNTIME_STORAGE_AND_PERSISTENCE.md`](../Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md) · **Handoffs:** [`set_revision_persistence_handoff.md`](../Plans/set_revision_persistence_handoff.md), [`continuous_runtime_persistence_phase5_recovery_handoff.md`](../Plans/continuous_runtime_persistence_phase5_recovery_handoff.md)  
**Work-queue baseline (shipped):** [`current_set_persist_work_item_queue_enhancement.md`](../Plans/current_set_persist_work_item_queue_enhancement.md)

| Track | OpenSpec / plan | Remaining | When to pick |
|-------|-----------------|-----------|--------------|
| **A — Overlay loop picker** | `set-revision-persistence` §4.8–4.10 | Loop picker UI polish + HITL `set_revision_overlay` | Product overlay milestone |
| **B — Crash recovery** | `continuous-runtime-persistence` Phase 5 | `.sealj` / slot **prefix load**, quarantine tail, native fixtures | After architecture gate; orthogonal to overlay |
| **C — Admit API migration** | [#18](https://github.com/Lytrix/MidiLooper/issues/18) Phase 1.3 | `admitLoopSlotPersist` → `admitLoopPersist(LoopId)` at domain call sites | Hygiene with #18 closeout |
| **D — Parked** | overlay hang, 3.9 failsafe | [`persistence_overlay_large_slot_focus_restore_bugfix.md`](../Plans/persistence_overlay_large_slot_focus_restore_bugfix.md); set-revision §3.9 | Investigation only |

**DeferredJobScheduler Phase B:** **Archived** [`2026-07-19-deferred-job-scheduler`](../../openspec/changes/archive/2026-07-19-deferred-job-scheduler/). Specs: `deferred-job-scheduler/`, `lazy-slot-hydration`.

**DEC-020 note:** Phases 0–4 **shipped** on `dev`. Phase 5 was historically **paused** pending wrap-fix validation — that does not block overlay track A; confirm with user before starting B if wrap HITL is still open (see § Parked wrap investigation below).

---

## Recently shipped (2026-08)

| Slice | Decision / commit | Evidence |
|-------|-------------------|----------|
| Live-record tick-0 NoteOn blip | `8de682c` | Native 999/999; device PASS [`182949`](../../captures/session_20260811_182949.log); pre-fix [`182528`](../../captures/session_20260811_182528.log) |
| Note edit current state | DEC-029; `3e9253e` | Native 969/969; [`PHASE8_CLOSEOUT`](../../openspec/changes/archive/2026-08-08-note-edit-current-state/PHASE8_CLOSEOUT.md); HITL [`112202`](../../captures/session_20260808_112202.log), [`115120`](../../captures/session_20260808_115120.log), [`032118`](../../captures/session_20260808_032118.log) |
| StorageManager TU remaining trim | PR [#17](https://github.com/Lytrix/MidiLooper/pull/17) → `dev` (2026-08-08) | [#16](https://github.com/Lytrix/MidiLooper/issues/16); [`storagemanager_translation_unit_extraction_refinement.md`](../Plans/storagemanager_translation_unit_extraction_refinement.md) |
| Codebase consistency Phase 4 — DisplayNoteResolve | PR [#24](https://github.com/Lytrix/MidiLooper/pull/24) → `dev` (2026-08-10) | Native 969/969; [#18](https://github.com/Lytrix/MidiLooper/issues/18) |
| Codebase consistency Phase LR — NoteMovementUtils shims | PR [#26](https://github.com/Lytrix/MidiLooper/pull/26) → `dev` (2026-08-10) | Native 969/969; [#18](https://github.com/Lytrix/MidiLooper/issues/18) |
| Codebase consistency Phase 4 — NoteEditFocus header | PR [#25](https://github.com/Lytrix/MidiLooper/pull/25) → `dev` (2026-08-10) | Native 969/969; [#18](https://github.com/Lytrix/MidiLooper/issues/18) |
| Codebase consistency Phase 4 — TrackManager TU | PR [#22](https://github.com/Lytrix/MidiLooper/pull/22) → `dev` (2026-08-08) | Native 969/969; manual [`225737`](../../captures/session_20260808_225737.log); [#18](https://github.com/Lytrix/MidiLooper/issues/18) |
| Codebase consistency Phase 1 — authority | PR [#19](https://github.com/Lytrix/MidiLooper/pull/19) merged to `dev` (2026-08-08) | Native 969/969; HITL [`163904`](../../captures/session_20260808_163904.log); [#18](https://github.com/Lytrix/MidiLooper/issues/18) |
| Playing move/length audition | `15c5750` | [`113626`](../../captures/session_20260808_113626.log), [`115120`](../../captures/session_20260808_115120.log); [bugfix doc](../Plans/note_edit_playing_move_audition_bugfix.md) |
| Resolver §12 orthogonal state | DEC-030; `7af8671` | Native 969/969; HITL [`112202`](../../captures/session_20260808_112202.log) @88.669 |
| Edit-session-action-geometry archive | 2026-08-05 | [`openspec/specs/edit-session-action-geometry/`](../../openspec/specs/edit-session-action-geometry/) |
| Docs folder hygiene Phase 3 | 2026-08-08 | Root doc roles; `PROJECT_INTENT` redirect; `FEATURES` / `FEATURE_PLANS` banners |
| Docs folder hygiene Phase 2d | 2026-08-08 | 9 `Refinements/` logs → `Plans/archive/refinements/` |
| Docs folder hygiene Phase 2c | 2026-08-08 | 2 FROZEN `*_bugfix.md` → `Plans/archive/bugfix/` |
| Docs folder hygiene Phase 2b | 2026-08-08 | 10 `*_handoff.md` → `Plans/archive/handoff/` |
| Docs folder hygiene Phase 2a | 2026-08-08 | 43 `*.plan.md` → `Plans/archive/cursor-exports/`; 4 retained at root |
| Docs folder hygiene Phase 1.5 | 2026-08-08 | PascalCase `docs/` folders (`Authority/`, `Plans/`, `Runtime/`, `Agents/`, `Templates/`) |
| Docs folder hygiene Phase 1 | 2026-08-08 | Four-bucket `docs/README.md`; slim `Plans/README.md`; `HITL_ARCHITECTURE` → `Authority/Architecture/` — [`docs_folder_hygiene_refinement.md`](../Plans/docs_folder_hygiene_refinement.md) |

Normative specs: `note-edit-current-state`, `note-edit-modification-session`, `note-edit-session-undo`, `internal-heap-external-memory-routing`, `edit-session-action-geometry`.

---

## Parked / closed (queue references)

### Edit HITL from scratch — parked

**Plan:** [`docs/Plans/m8_edit_note_edit_hitl_automation_refinement.md`](../Plans/m8_edit_note_edit_hitl_automation_refinement.md)

Deferred from **edit-session-action-geometry** Phase 5 (D14 full matrix). Interim smoke: `edit_minimal` preset only.

### Firmware ownership / lifetime review — closed (2026-08-06)

**Plan:** [`docs/Plans/firmware_ownership_lifetime_review.md`](../Plans/firmware_ownership_lifetime_review.md) — P0/P1 **done**; Phase 5 recovery and layered **`base`** HITL **parked**.

### Note edit undo after reboot — done

**Plan:** [`.cursor/plans/note_edit_undo_reboot_58ef9394.plan.md`](../../.cursor/plans/note_edit_undo_reboot_58ef9394.plan.md) — HITL **PASS** [`session_20260805_212234`](../../captures/session_20260805_212234.log).

### Hygiene — codebase debt review — complete

**Plan:** [`docs/Plans/codebase_hygiene_technical_debt_review.md`](../Plans/codebase_hygiene_technical_debt_review.md) — safe hygiene **done** on `chore/codebase-hygiene-sprint1`.

### Note edit control-surface split — complete

**Plan:** [`docs/Plans/note_edit_control_surface_split_refinement.md`](../Plans/note_edit_control_surface_split_refinement.md) — Phases 0–8 on `dev`.

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

**Plan:** [`docs/Plans/slot_queue_loop_edit_depart_bugfix.md`](../Plans/slot_queue_loop_edit_depart_bugfix.md)

| Item | Status |
|------|--------|
| No mid-play geometry write on LOOP_EDIT depart | **PASS** |
| Device gate (queue slot, LoopEnd commit, no hang) | **PASS** [`004331`](../../captures/session_20260718_004331.log), reconfirmed [`010126`](../../captures/session_20260718_010126.log) |
| Queued-launch musical-time countdown | **In tree** — OLED field; LoopEnd commits present in `010126` |

---

### Paused — Memory pressure reclaim (M6 follow-on)

**Branch:** `feature/memory-pressure-reclaim`  
**Plan:** [`docs/Plans/memory_pressure_reclaim_refinement.md`](../Plans/memory_pressure_reclaim_refinement.md)

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

**Plan:** [`multi_track_playback_pressure_closure_refinement.md`](../Plans/multi_track_playback_pressure_closure_refinement.md)

---

## Recently landed (persistence work queue — on `dev`)

**Branch:** `feature/persistence-work-queue` — [`current_set_persist_work_item_queue_enhancement.md`](../Plans/current_set_persist_work_item_queue_enhancement.md)

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

**Plan:** [`docs/Plans/record_stop_playback_hang_bugfix.md`](../Plans/record_stop_playback_hang_bugfix.md) (phase 4c)

| Gate | Status |
|------|--------|
| Native | **562/562** |
| `teensy41-capture-serial` build | **SUCCESS** (2026-07-13) |
| HITL / manual | **User** — record→stop→PLAYING; no MO flood; playhead bar 0 |

---

### Bugfix: record-stop coordinate frame (phase 4) — **superseded by 4c flood fix**

---

### Bugfix: overdub wrap note-off pairing — **implemented, pending HITL**

**Follow-up to capture ownership refactor** ([`overdub_wrap_note_off_pairing_bugfix.md`](../Plans/overdub_wrap_note_off_pairing_bugfix.md)). Fixes wrong on/off pairing when notes are held across loop wrap.

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

**Plan:** [`docs/Plans/overdub_wrap_note_off_capture_bugfix.md`](../Plans/overdub_wrap_note_off_capture_bugfix.md)

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

### OpenSpec: [`continuous-runtime-persistence`](../../openspec/changes/continuous-runtime-persistence/) (DEC-020)

**Branch:** `dev` — Phases 0–4 shipped; **Phase 5 not started**

| Phase | Status |
|-------|--------|
| **0** Diagnostics | **Complete** |
| **1** Chunk lifecycle | **Complete** |
| **2** Persistence queue | **Complete** |
| **3** Cooperative scheduler | **Complete** — overdub-stop HITL passed (`f0ee520`) |
| **4** Mid-pass persistence | **Shipped** — native + **HITL passed** [`session_20260709_171043.log`](../../captures/session_20260709_171043.log) (64+64); boot restore [`session_20260709_171951.log`](../../captures/session_20260709_171951.log) |
| **5** Recovery | **Not started** — longest valid prefix load + quarantine tail — [**handoff**](../Plans/continuous_runtime_persistence_phase5_recovery_handoff.md) — **CURRENT_WORK pick track B** |
| **6** Full 64+64 HITL | **Mostly evidenced** (same log) — archive checklist + `oldestDirtyChunkAge` review remain |

| Gate | Owner | Status |
|------|-------|--------|
| Native | Agent | `pio test -e native` — **969/969** (2026-08-10) |
| `test_storage_loop_io` | Agent | Run before Phase 4 archive sign-off |
| Phase 4 HITL (64+64 record/overdub) | **User** | **PASS** — [`session_20260709_171043.log`](../../captures/session_20260709_171043.log) |
| Phase 4 HITL (cold-boot restore) | **User** | **PASS** — [`session_20260709_171951.log`](../../captures/session_20260709_171951.log) |
| Phase 5 native fixture | Agent | Prefix load from truncated `.sealj` / slot — **not started** |

**HITL policy:** stop/crash scenarios — user captures serial manually (`capture_session.py`); agent does not loop HITL. On crash, user bisects and shares log before stop-path patches.

| Doc | Role |
|-----|------|
| Agent map | [RUNTIME_STORAGE_AND_PERSISTENCE.md](../Guides/RUNTIME_STORAGE_AND_PERSISTENCE.md) |
| **Phase 5 handoff** | [continuous_runtime_persistence_phase5_recovery_handoff.md](../Plans/continuous_runtime_persistence_phase5_recovery_handoff.md) |
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

### Derived note overlap (`edit-session-action-geometry`) — **archived 2026-08-05**

Pipeline shipped; normative [`openspec/specs/edit-session-action-geometry/`](../../openspec/specs/edit-session-action-geometry/). Phase 5 HITL matrix **parked** — see Edit HITL plan above. Historical handoff: [`derived_note_overlap_logic_handoff.md`](../Plans/derived_note_overlap_logic_handoff.md).

### Prior: [`runtime-derived-representation-heap`](../../openspec/changes/runtime-derived-representation-heap/)

M6 Ph 1–2 on `dev`; pressure reclaim + Ph 3–4 in [`memory_pressure_reclaim_refinement.md`](../Plans/memory_pressure_reclaim_refinement.md).

---

## Do not start without decision

- DEC-023 capture-commit Track slices (after Phase 5 or explicit user go)
- `currentset-savedset-storage-layout`
- D13 jam-recording (ROADMAP — post JamRecorder)
