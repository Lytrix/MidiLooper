# Architecture review — edit-session-action-geometry

**Change:** `edit-session-action-geometry`  
**Date:** 2026-08-05  
**Status:** Active — use this file before and after **each phase** of the transaction-baseline ownership rebuild  

**Related:** [proposal.md](proposal.md), [design.md](design.md), [tasks.md](tasks.md),
[`docs/plans/note_edit_session_action_geometry_transaction_baseline_bugfix.md`](../../../docs/plans/note_edit_session_action_geometry_transaction_baseline_bugfix.md)

---

## Primary invariant (north star)

> Given the **immutable transaction baseline** and **current edited geometry**, compute what
> **live store** must look like right now. Overlap analysis is gated to the **mover's current
> pitch lane**. Baseline is frozen at **edit driver boundary** (D19) and is never pruned from
> live-store presence.

---

## Finding → phase map (transaction baseline rebuild)

| Severity | Finding | Phase | Owner |
|----------|---------|-------|-------|
| Critical | Full-loop baseline turned every noteId lookup miss into a `Delete` row → notes destroyed at commit | 7 | `buildPreCommitBaselineLiveDiffOverlapPasses` |
| High | `checkLinearNoteOff` compared unpaired offs → false `check=2` on any repeated pitch | 8 | `checkLinearNoteOff` |
| Medium | `overlapPitchLane` cast to `(void)` → every tick evaluated the whole loop | 9 | `runEditSessionGeometryPipeline` |
| Critical | `buildEditClosureNoteIds` seeds baseline from `focus.last.pitch` only → zero overlap candidates when mover pitch ≠ neighbour pitches | 2 | `NoteEditFocus` / `buildEditClosureNoteIds` |
| Critical | `analyzeEditSessionInteractions` has no pitch gate → widening baseline would hide unrelated pitches | 1 | `analyzeEditSessionInteractions` |
| Critical | Per-tick `enrichBaselineMapFromCommittedAndLive` prune deletes hidden notes' baseline → no restore, no Delete commit row | 2 | `runEditSessionGeometryPipeline` / baseline ownership |
| Critical | Mid-edit `assignMissingNoteIds` mints ids absent from capture materialize → Delete rows replay as no-ops | 3 | `openNoteEditSession` |
| High | Resolve uses unprojected baseline while analyze uses projected → inverted spans → `LinearNoteOff` (check=2) | 4 | `resolveAllConstrainedGeometry` |
| High | Session-tagged apply/builder fallbacks paper over LIFO ambiguity | 5 | `ApplyEditSessionActions` / `EditSessionActionBuilder` |
| Medium | `overlapNotes` still drives display filter / legacy commit while pipeline writes none | 5 | OpenSpec 4.5 / 4.5b |
| High | Active `NoteEditFocus` can keep driving a prior note after `EditorSelection.primaryNote` changes | D19a bugfix | `EditManager::liveEditDisplayNoteAtSelect` / `ensureNoteEditFocusForLiveEdit` |

---

## Evidence anchors

| Concern | Primary location |
|---------|------------------|
| Same-pitch closure | `buildEditClosureNoteIds` in `NoteEditFocus.cpp` |
| Per-tick enrich + prune | `enrichBaselineMapFromCommittedAndLive`, `runEditSessionGeometryPipeline` |
| Pitch-less analyze | `analyzeEditSessionInteractions` in `EditSessionInteraction.cpp` |
| Baseline writes on apply | `ensureBaselineMapEntryForEditSessionAction`, `ensureBaselineMapBeforeShortenApply` |
| Pre-commit row source | `buildPreCommitBaselineLiveDiffOverlapPasses` |
| noteId assign | `Loop::assignMissingNoteIds`, `openNoteEditSession` |
| Capture evidence | `captures/session_20260805_013428.log`, `session_20260805_011000.log` |

---

## Per-phase gates

### Phase 0 — Documentation + WIP preserve (this session)

| Architecture gate | Answer |
|-------------------|--------|
| Owner module | `NoteEditFocus.baselineMap`; `runEditSessionGeometryPipeline` |
| Primary invariant | Immutable full-loop baseline; pitch-lane analyze |
| Ownership change? | **YES** — approved (plan); documented here |
| State transition change? | **NO** |
| Behavior-preserving? | **YES** for Phase 0 (docs / branch only) |
| Reuse decision | YES — extend existing baseline + pipeline |
| Phase scope | Branch/commit WIP; this file; bugfix plan; Q14 in design.md |

| Implementation review | |
|-------------------------|--|
| WIP committed off detached HEAD | [x] |
| Bugfix plan written | [x] |
| Q14 resolved in design.md | [x] |
| **Approval** | APPROVE |

---

### Phase 1 — Pitch-lane gate in analyze

**Scope:** Skip pairs in `analyzeEditSessionInteractions` when target baseline pitch ≠ edited causing span pitch.

| Architecture gate | Answer |
|-------------------|--------|
| Owner module | `analyzeEditSessionInteractions` |
| Primary invariant | Positive interaction graph is same-pitch only |
| Ownership change? | **NO** |
| State transition change? | **NO** |
| Behavior-preserving? | **YES** in isolation (baseline still same-pitch today) |
| Reuse decision | YES — extend analyzer classify path |
| Phase scope | `EditSessionInteraction.cpp` + interaction tests |

| Implementation review | |
|-------------------------|--|
| Cross-pitch pair omitted | [ ] |
| Pitch-change destination lane in scope | [ ] |
| `pio test -e native` (interaction suite) | [ ] |

---

### Phase 2 — Immutable full-loop transaction baseline

**Scope:** Snapshot `baselineMap` once per edit driver from committed materialize; remove per-tick enrich/prune; delete apply-path baseline writers; full-loop closure.

| Architecture gate | Answer |
|-------------------|--------|
| Owner module | `NoteEditFocus.baselineMap` at edit-driver boundary |
| Primary invariant | Baseline immutable until `primaryNote` changes |
| Ownership change? | **YES** — approved Phase 2 |
| State transition change? | **NO** — refresh still at D19 boundary |
| Behavior-preserving? | **NO** — intentional fix |
| Reuse decision | YES — extend `rebuildNoteEditFocus*` / driver boundary |
| Phase scope | `NoteEditFocus.*`, `RunEditSessionGeometryPipeline.*`, `ApplyEditSessionActions.*`, `EditManager.*` |

| Implementation review | |
|-------------------------|--|
| No per-tick prune | [ ] |
| Hide survives to pre-commit Delete row | [ ] |
| Restore after leave | [ ] |
| `pio test -e native` | [ ] |

---

### Phase 3 — noteId identity contract

| Architecture gate | Answer |
|-------------------|--------|
| Owner module | `openNoteEditSession` / `Loop::assignMissingNoteIds` |
| Primary invariant | Session noteIds stable for geometry + commit replay |
| Ownership change? | **NO** — move call site only |
| State transition change? | **NO** |
| Behavior-preserving? | **NO** — intentional fix for Delete replay |
| Reuse decision | YES — existing assign APIs |
| Phase scope | `EditManager::openNoteEditSession`, `runEditSessionGeometryPipeline` |

---

### Phase 4 — Projection consistency + canonical store

| Architecture gate | Answer |
|-------------------|--------|
| Owner module | `resolveAllConstrainedGeometry` / pipeline orchestrator |
| Primary invariant | Resolve uses same projected baseline as analyze; no inverted spans |
| Ownership change? | **NO** |
| State transition change? | **NO** |
| Behavior-preserving? | **NO** — fixes check=2 |
| Reuse decision | YES — existing projection helpers |
| Phase scope | `RunEditSessionGeometryPipeline.cpp`, `ResolveConstrainedGeometry.cpp` |

---

### Phase 5 — Retire workarounds + overlapNotes scratch

| Architecture gate | Answer |
|-------------------|--------|
| Owner module | Live store + baselineMap (display/commit); retire `overlapNotes` |
| Primary invariant | No persistent constraint registry (D5, D15) |
| Ownership change? | **YES** — approved OpenSpec 4.5 / 4.5b |
| State transition change? | **NO** |
| Behavior-preserving? | **NO** — intentional retire |
| Reuse decision | YES — baseline-diff commit path already primary |
| Phase scope | Apply/builder cleanup; display filter; commit gates |

---

### D19a Bugfix — Single edit driver identity

**Scope:** Enforce **`EditorSelection.primaryNote`** as the single edit driver identity. **`NoteEditFocus`** may provide `focus.last` only when `focus.movingNoteId == selection.primaryNote`; otherwise live geometry paths rebuild focus for the selected note before producing edited geometry.

| Architecture gate | Answer |
|-------------------|--------|
| Owner module | `EditManager::liveEditDisplayNoteAtSelect`; `EditManager::ensureNoteEditFocusForLiveEdit` |
| Primary invariant | `EditorSelection.primaryNote` is the edit driver; `NoteEditFocus` is subordinate driver state |
| Ownership change? | **NO** — enforces D19 / D19a |
| State transition change? | **NO** |
| Behavior-preserving? | **NO** — intentional bugfix for stale focus driving the wrong note |
| Reuse decision | YES — extend existing select/focus rebuild paths |
| Phase scope | `EditManager.*`; native focus/selection tests |

| Implementation review | |
|-------------------------|--|
| Live fader read ignores stale focus | [x] |
| Live edit entry rebuilds stale focus for selected primary note | [x] |
| Future multi-note domain remains `EditorSelection.selectedNotes` | [x] |
| `pio test -e native` | [x] 766/766 |

---

### Phase 7 — Delete-row authority (`changedOverlapNoteIds`)

**Scope:** A pre-commit `Delete` row requires positive evidence that the geometry pipeline hid the
note. An unresolved baseline entry is preserved and warned about, never deleted.

| Architecture gate | Answer |
|-------------------|--------|
| Owner module | `buildPreCommitBaselineLiveDiffOverlapPasses`; `changedOverlapNoteIds` written only by geometry actions in `applyEditSessionActions` |
| Primary invariant | A baseline lookup miss never removes a note |
| Ownership change? | **YES** — approved Phase 7; Delete authority moves from baseline lookup to the Hide action |
| State transition change? | **NO** — driver boundary refresh stays D19 (`focus.clear()`) |
| Behavior-preserving? | **NO** — intentional fix for note destruction |
| Reuse decision | YES — extend the existing pre-commit diff and apply-path dispatch |
| Phase scope | `NoteEditFocus.*`, `ApplyEditSessionActions.cpp`, `NoteEditSessionUndo.cpp`, focus/apply tests |

| Implementation review | |
|-------------------------|--|
| Unresolved baseline entry warns, emits no Delete | [x] `test_unresolved_baseline_entry_emits_no_delete_row` |
| Hidden note still emits Delete | [x] `test_hidden_note_baseline_survives_for_pre_commit_delete` |
| Hidden overlap same start as mover emits Delete, not mover Length | [x] `test_hidden_overlap_same_start_as_mover_emits_delete_not_length` |
| Stale live overlap mismatch without geometry authority emits no Length row | [x] `test_unchanged_overlap_live_mismatch_emits_no_length_row` |
| Committed overlap Delete clears restore authority | [x] `test_committed_overlap_delete_clears_focus_restore_authority` |
| Committed overlap Update promotes shortened baseline | [x] `test_committed_overlap_update_promotes_shortened_focus_baseline` |
| Session 134610 shorten+hide pre-commit rows | [x] `test_session_134610_shortened_overlap_commit_rows` |
| Hide then restore leaves nothing pending | [x] `test_restored_overlap_note_leaves_no_pending_delete` |
| End-to-end through real writers | [x] `test_same_pitch_complete_cover_hide_and_restore_on_leave` |
| `pio test -e native`; `teensy41-capture-serial` build | [x] 773/773; SUCCESS |

**Evidence:** `captures/session_20260805_020716.log` — `take_only flatEvents=172` vs
`replay_flat flatEvents=148`, a twelve-note gap after edit-pass replay.

**Reading these two markers (correction):** neither is by itself proof of note destruction.
`commitEditAction <stage>: M<pitch>@<tick> missing in recon` comes from
`logChangeLengthCommitTrace`, which only asks whether the note is still at its **home start tick** —
expected after any move, and designed for the change-length path. The `take_only` vs `replay_flat`
gap counts **all** edit passes on the loop, including rows persisted by earlier firmware, so it does
not isolate the current session. Positive evidence that no Delete row was emitted is the **absence**
of pre-commit `Delete` rows and unresolved-baseline warnings, as in
`captures/session_20260805_030517.log`.

**D19b correction:** `captures/session_20260805_120601.log` shows the mover on pitch 23
covering a same-pitch overlap target that shares the mover's live start after the cover. The
pre-commit baseline/live reader must not resolve that hidden target to `focus.movingNoteId`; it
must return "not live" so `changedOverlapNoteIds` emits the authorized `Delete` row instead of a
spurious mover-shaped `Length` row.

---

### Phase 8 — Canonical pairing in pair-shape invariants

**Scope:** Judge a note-off against the note-on it belongs to, not against every same
channel/pitch off in the vector. Applies to **`checkLinearNoteOff`** (`check=2`) and
**`checkNoWrappedPairStorage`** (`check=4`), which had the identical unpaired all-pairs scan —
after the `check=2` fix, `check=4` became the reported first failure on the same canonical stores
(`captures/session_20260805_030517.log`). Both now share one walker,
`validateCanonicalNotePairs`, parameterised by a violation predicate.

| Architecture gate | Answer |
|-------------------|--------|
| Owner module | `validateCanonicalNotePairs` (`LoopEventValidation`), used by `checkLinearNoteOff` and `checkNoWrappedPairStorage` |
| Primary invariant | Validation depends only on canonical note pairing, never on relative event order |
| Ownership change? | **NO** |
| State transition change? | **NO** |
| Behavior-preserving? | **NO** — removes a false `check=2` / `check=4` on any repeated pitch |
| Reuse decision | YES — same file's existing `std::vector<bool>` index-marking pattern; the shared walker takes a plain function pointer, so one instantiation stays in flash (ITCM budget) |
| Phase scope | `src/Utils/LoopEventValidation.cpp`, `test_loop_event_validation` |

Pairing order: noteId on both sides, else nearest preceding unpaired note-on on the lane (LIFO),
else a leftover open note-on against a leftover off, which is the stored wrap pair the check
exists to reject. An off with no note-on at all stays with `checkOrphanNoteOff`.

| Implementation review | |
|-------------------------|--|
| Two sequential same-pitch notes pass | [x] `test_validate_sequential_same_pitch_notes_pass` |
| Same, with noteId tags | [x] `test_validate_sequential_same_pitch_notes_with_note_ids_pass` |
| Real inversion still fails | [x] `test_validate_linear_note_off_fails_inverted_own_pair` |
| Stored wrap pair still fails | [x] `test_validate_linear_note_off_fails_wrap_pair` |
| Note stored beyond loopLength passes | [x] `test_validate_linear_note_off_beyond_loop_length_passes` |
| Head+tail same-pitch pair is not a wrap pair | [x] `test_validate_no_wrapped_pair_storage_sequential_same_pitch_passes` |
| Same, with noteId tags | [x] `test_validate_no_wrapped_pair_storage_sequential_same_pitch_with_note_ids_passes` |
| Genuine stored wrap pair still fails `check=4` | [x] `test_validate_no_wrapped_pair_storage_fails` |

---

### Phase 9 — Pitch-lane evaluation scope

**Scope:** Honour the existing `overlapPitchLane` parameter so each tick evaluates the mover's
lane plus the sticky changed set, instead of the whole loop.

| Architecture gate | Answer |
|-------------------|--------|
| Owner module | `collectEvaluationScopeNoteIds` (`EditSessionInteraction`, beside `determineEligiblePairs`) |
| Primary invariant | Candidate pairing and baseline projection consume one scope list, so they cannot disagree |
| Ownership change? | **NO** — honours a documented parameter that was cast to `(void)` |
| State transition change? | **NO** |
| Behavior-preserving? | **NO** — narrows the evaluated set; the analyze pitch gate stays as backstop |
| Reuse decision | YES — extend `EditSessionInteraction` scope helpers and the existing move call site |
| Phase scope | `EditSessionInteraction.*`, `RunEditSessionGeometryPipeline.cpp`, `NoteMovementUtils.cpp`, interaction tests |

The transaction baseline stays **full loop**. Narrowing the baseline itself would lose restore for
notes hidden on the source lane after a pitch change; the sticky `changedOverlapNoteIds` term in
the scope is what keeps those in play.

| Implementation review | |
|-------------------------|--|
| Cross-lane notes excluded from candidates | [x] `test_evaluation_scope_excludes_cross_lane_notes` |
| No lane means all notes (pre-lane parity) | [x] `test_evaluation_scope_without_lane_includes_all_notes` |
| Source-lane hide survives pitch change | [x] `test_evaluation_scope_keeps_hidden_source_lane_note_after_pitch_change` |
| Projection keeps the mover | [x] `test_projected_baseline_drops_cross_lane_keeps_mover` |
| Move call site passes `movingNotePitch` | [x] `NoteMovementUtils` move path |

---

### Phase 10 — Note edit identity is `NoteId`, not the track MIDI channel

**Scope:** Stop gating note-edit read paths on `track.getMidiChannel()`. Identity inside the
materialized session store is `NoteId`, per spec § *"resolve targets by `NoteId`"*.

| Architecture gate | Answer |
|-------------------|--------|
| Owner module | `NoteEditFocus` identity resolvers (`findLinearNoteSpanForNoteId`, `stampNoteIdsOntoPairedNoteOffs`) and `collectEvaluationScopeNoteIds` |
| Primary invariant | Within the materialized session store a note is identified by `NoteId`; the track's live MIDI channel never gates which materialized notes the pipeline can see |
| Ownership change? | **YES** — user-approved: visibility authority moves off `track.getMidiChannel()` onto `NoteId` |
| State transition change? | **NO** |
| Behavior-preserving? | **NO** — intentional; same-pitch overlap becomes visible to analyze |
| Reuse decision | YES — extend the existing resolvers. `findLinearOffForNoteId` already pairs on `NoteId` + pitch and needed no change |
| Phase scope | `NoteEditFocus.*`, `EditSessionInteraction.*`, `RunEditSessionGeometryPipeline.cpp`, focus + interaction tests |

**Why the channel was wrong.** `Track::noteOn` records the **incoming** MIDI channel, so materialized
record/overdub passes carry the channel played at record time, which need not equal the track's
output channel. `Loop::rematerializeEditView` → `LoopPasses::materialize` is the only writer of the
session store, so the store is exactly the record/overdub + edit passes — never live incoming MIDI.
Reading it through an output-channel filter is what injected live MIDI configuration into the edit
path.

**How the split showed up.** Channel-independent readers worked while channel-gated readers did
not, which is why notes were visible and movable but never shortened or hidden:

| Reader | Channel gate | Observed |
|---|---|---|
| `filterSelectableDisplayNotes` → `reconstructDisplayNotes` (display) | none | all 70 notes drawn |
| `findNoteOnForNoteIdAnyChannel` (move fallback) | none | move worked |
| `collectEvaluationScopeNoteIds` | `evt.channel != channel` | `candidates=0` |
| `findLinearNoteSpanForNoteId` | `evt.channel != channel` | `baselineMap=1` |
| `stampNoteIdsOntoPairedNoteOffs` | `evt.channel != channel` | note-offs left unstamped |

**Pitch pairing keeps the channel.** Only the `NoteId` match stops being gated.
`stampNoteIdsOntoPairedNoteOffs` now pairs within **each event's own** channel, so a note-off can
never take the id of a note-on on another channel. `findLinearNoteSpanForNoteId` tries the track
channel first and falls back to a channel-independent match, so single-channel stores keep their
exact previous resolution order.

**Dead parameters removed** rather than left as `(void)`: `channel` is gone from
`stampNoteIdsOntoPairedNoteOffs` and `collectEvaluationScopeNoteIds`.

**Diagnostic:** the pipeline line now leads with `storeNoteOns=`, so a future capture distinguishes
"the store is empty" from "the store is full but scope is empty" without re-deriving it.

| Implementation review | |
|-------------------------|--|
| Span resolves when store channel ≠ track channel | [x] `test_find_linear_note_span_resolves_across_store_channel` |
| Scope keeps same-pitch overlap regardless of store channel | [x] `test_evaluation_scope_ignores_store_channel` |
| Off stamping pairs within a channel, never across | [x] `test_stamp_note_ids_pairs_within_store_channel` |
| Mispaired-off rejection still holds | [x] `test_find_linear_note_span_rejects_mispaired_off` |
| Pitch-lane scope unchanged | [x] Phase 9 scope tests still pass |
| Remaining channel-gated baseline/live-store readers fixed | [x] `test_populate_baseline_includes_store_channel_notes`; `test_baseline_map_diff_reads_store_channel_live_span`; `test_apply_hide_removes_store_channel_target`; `test_can_apply_simple_pitch_change_blocks_store_channel_target_lane`; `test_builder_does_not_remap_hidden_overlap_target_to_mover` |
| `pio test -e native` | [x] 763/763 |
| HITL edit gate | [ ] pending re-run |

---

### RAM1 / ITCM constraint (applies to Phases 7-10)

ITCM is allocated in 32 KB blocks. Baseline before this work: `code:424620 padding:1364`, i.e.
**1364 bytes** of headroom before a whole extra block is claimed and the image overflows by 20 KB.

A `std::unordered_set<NoteId, ...>` for `changedOverlapNoteIds` cost ~1.9 KB of ITCM (new hashtable
instantiation) and overflowed the image by 20 KB. `NoteIdList` reuses the geometry pipeline's
existing `std::vector<NoteId, InternalHeapFirstAllocator<NoteId>>` instantiation instead, and
`checkLinearNoteOff` moved to flash via the new `LOOP_VALIDATION_MEM`
(`include/Utils/LoopValidationMem.h`, same pattern as `TrackMem.h` / `NoteEditMem.h`; scoped to
`LoopEventValidation` because that module serves Track, Loop and Storage alike).

Final after Phase 9: `code:425020 padding:964` — **964 bytes of headroom**, on par with the
1364-byte baseline. After Phase 10 channel-independent reader cleanup:
`code:425148 padding:836` — **836 bytes**, i.e. the channel-independent resolvers plus the shared
pair walker and remaining live-store/action fallbacks cost 128 bytes net (the walker replaced a
duplicated scan, which offset most of the additions).

Before adding RAM1 code in this area, check `pio run -e teensy41-capture-serial` size output and
prefer moving cold-path functions to flash. Watch for **new container instantiations** in
particular: one extra `std::unordered_*` type in a hot translation unit is enough to flip a block.

---

### Phase 6 — Verification

| Gate | Pass |
|------|------|
| `pio test -e native` | [x] 742/742 (2026-08-05) |
| `teensy41-capture-serial` build | [x] SUCCESS |
| Device: stable `flatEvents` | [ ] pending user flash + re-run |
| Device: `type=1` / `type=2` on same-pitch overlap | [ ] pending user flash + re-run |
| Device: no `non-canonical store` / `missing in recon` | [ ] pending user flash + re-run |

---

## Phase A — Single display projection (2026-08-05)

**Plan:** [`docs/plans/note_edit_display_commit_stream_refactor_refinement.md`](../../../docs/plans/note_edit_display_commit_stream_refactor_refinement.md)  
**Evidence:** `captures/session_20260805_141706.log` — geometry `ShortenNote` / `HideNote` rows apply to the live
store, but OLED/DNTE still derive mover and overlap geometry through parallel paths.

| Architecture gate | Answer |
|-------------------|--------|
| Owner module | `NoteEditFocus::projectNoteEditDisplayNotes` (projection); `EditManager::projectedNoteEditDisplayNotes` (cache + consumers) |
| Primary invariant | One live geometry stream (`applyEditSessionActions` + session store) feeds one display projection for active note edit |
| Ownership change? | **YES** — approved Phase A; display projection moves out of `DisplayManager` / movement telemetry side channels |
| State transition change? | **NO** |
| Behavior-preserving? | **NO** — intentional; overlap notes must show live shortened/hidden geometry without reselect |
| Reuse decision | YES — extend `filterSelectableDisplayNotes` rules into `projectNoteEditDisplayNotes`; collapse duplicate caches into `EditManager` |
| Phase scope | `NoteEditFocus.*`, `EditManager.*`, `DisplayManager.*`, `NoteMovementUtils.cpp`, `test_note_edit_focus` |

| Implementation review | |
|-------------------------|--|
| `projectNoteEditDisplayNotes` is the sole projection producer | [x] |
| `DisplayManager` is paint-only (no duplicate cache, no `focus.last` overlay bar) | [x] |
| Movement-side `SC_DNTE` removed | [x] |
| Pitch 22/23 overlap projection native tests | [x] |
| `pio test -e native` | [x] 777/777 |

**Out of scope for Phase A:** apply-owned `editPass` rows (Phase B), retiring `buildPreCommitBaselineLiveDiffOverlapPasses` (Phase C).

---

## Commit stream review (Phase 4.9 — 2026-08-05)

**Plan:** [`docs/plans/note_edit_commit_stream_review_refinement.md`](../../../docs/plans/note_edit_commit_stream_review_refinement.md)  
**Evidence:** `captures/session_20260805_134610.log`

| Question | Answer |
|----------|--------|
| Interim commit authority | `buildPreCommitEditPasses` (code today) |
| Target commit authority | **Superseded by Phase 4.10f** — canonical post-apply session state diff |
| Refactor scope now | Diagnostics only — row-level `NOTE_EDIT pre-commit row` logging; no further pre-commit heuristics without HITL proof |
| Next implementation | Phase 4.10f canonical commit serialization |

| Gate | Pass |
|------|------|
| Authority decision documented | [x] |
| Row-level commit logging (`SESSION_CAPTURE`) | [x] `logPreCommitEditPassRows` in `EditManager.cpp` |
| Native regression `session_20260805_134610` | [x] `test_session_134610_shortened_overlap_commit_rows` |
| HITL re-run with new logs | [ ] pending user flash |

---

## Phase 4.10f — Canonical commit serialization

**Plan:** [`docs/plans/note_edit_singular_commit_pipeline_refinement.md`](../../../docs/plans/note_edit_singular_commit_pipeline_refinement.md)  
**Evidence:** `captures/session_20260805_171134.log` — commit serializes overlap
`Delete noteId=31` plus mover `NoteRange noteId=36`, then selection resolves to an unrelated
pitch-60 note at tick 960 and empty-step deselect clears focus without a deselect
`SelectionChanged` path.

| Architecture gate | Answer |
|-------------------|--------|
| Owner module | `commitAllPendingNoteEditActions` orchestrates commit; canonical row builder lives beside existing `buildPreCommitEditPasses` / baseline-live diff helpers |
| Primary invariant | Commit is pure serialization of canonical post-apply `NoteEditSession.store` relative to transaction baseline |
| Ownership change? | **YES** — approved Phase 4.10f; persistence authority moves away from `applyOwnedEditPassRows` to canonical state diff |
| State transition change? | **YES** — approved Phase 4.10f for selection resolution only; empty-step deselect becomes a first-class selection consumer transition, not a commit-row source |
| Behavior-preserving? | **NO** — intentional fix for action-history / state-diff divergence and deselect highlight drift |
| Reuse decision | YES — extend existing `buildPreCommitEditPasses` / baseline-live diff path; apply-owned rows become parity diagnostics only |
| Phase scope | `EditManager.*`, `NoteEditFocus.*`, `ApplyOwnedEditPassRows.*`, `ControlSurfaceManager.cpp`, OpenSpec docs, `test_note_edit_focus`, `test_apply_edit_session_actions`, selection/fader feedback tests |

| Implementation review | |
|-------------------------|--|
| OpenSpec authority updated: canonical state diff wins | [x] |
| `commitAllPendingNoteEditActions` always uses canonical rows | [x] |
| Apply-owned rows cannot affect persistence | [x] `test_canonical_commit_rows_do_not_depend_on_apply_owned_diagnostic_rows` |
| Empty-step deselect resolves selection/display consistently | [x] `test_empty_step_deselect_is_selection_target_change` |
| `session_20260805_171134` native regression | [x] `test_session_171134_canonical_commit_rows_from_final_session_store` |
| `pio test -e native`; `teensy41-capture-serial` build | [x] 797/797; SUCCESS |

---

## Post-4.10 implementation review (2026-08-05 doc sync)

Phases 1–4.10 code shipped; OpenSpec tasks 0.7/0.8 and HITL 4.7h/4.8i marked complete.

| Item | Status |
|------|--------|
| Pipeline wire (`runEditSessionGeometryPipelineForCausingNote`) | [x] move / length / pitch / add / delete |
| Canonical commit (`buildPreCommitBaselineLiveDiffOverlapPasses`) | [x] apply-owned rows diagnostic only |
| Display projection (`projectNoteEditDisplayNotes`) | [x] sole active display producer |
| F1 select display-first motor sync | [x] `adf9209` — settle gates F2–F4; paint epoch before motor flush |
| HITL 4.7h pitch-lane gate (two notes one pitch) | [x] manual PASS |
| HITL 4.8i same-pitch overlap Hide/Shorten | [x] manual PASS |
| Phase 5 full HITL matrix + archive | [ ] open — tasks 5.1–5.3 |

---

## Architecture checkpoint (bugfix)

1. **Does this bug require changing ownership?** YES — transaction baseline mutability / prune ownership. Approved via this review + bugfix plan.
2. **Does this bug require changing state transitions?** NO — edit-driver boundary refresh remains D19.
