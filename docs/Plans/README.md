# Design plans (`docs/Plans/`)

**Not implementation authority.** Historical and proposed design work lives here. For what to build **now**, use [`Runtime/CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md) only. For deliverable-level shipped vs next, see [`DELIVERABLE_TRACKING.md`](../DELIVERABLE_TRACKING.md) (not a second work queue).

Authority order: [`Authority/README.md`](../Authority/README.md) → OpenSpec → [`Guides/`](../Guides/) → this folder → code.

---

## Conventions

| Pattern | Meaning |
|---------|---------|
| `*_bugfix.md` | Root-cause fix with capture evidence |
| `*_refinement.md` | Scoped refactor or enhancement |
| `*_handoff.md` | Session handoff (may be superseded) |
| `*_enhancement.md` | Larger feature or tooling work |
| `*.plan.md` | Cursor export from `~/.cursor/plans/` |

Add `Status: Active | Done | FROZEN` at the top when a plan's lifecycle matters. Do not bulk-edit old files merely to add status.

**New human-written plans:** `docs/Plans/<topic>_<kind>.md` (see [FEATURE_PLANS.md](../FEATURE_PLANS.md)).

**Cursor exports:** `cp ~/.cursor/plans/*.plan.md docs/Plans/` when you want them versioned.

---

## Archive policy (Phase 2)

When a plan is no longer active, `git mv` into `docs/Plans/archive/`:

| Subfolder | Contents |
|-----------|----------|
| `cursor-exports/` | Historical `*.plan.md` (43 archived 2026-08-08) |
| `handoff/` | Superseded `*_handoff.md` (10 archived 2026-08-08) |
| `bugfix/` | `Status: FROZEN` bugfixes (2 archived 2026-08-08) |
| `refinements/` | Historical `docs/Refinements/` logs (9 archived 2026-08-08) |

**Retained at `Plans/` root (live references):** `multi-loop_leds_and_droid_lfo_3a62f325.plan.md`, `dual-tick_view_override_architecture_856310b1.plan.md`, `exclude_led_channels_from_all_notes_off_1fca1ecd.plan.md`, `reduce_undo_and_lazy_loop_b89758a6.plan.md`.

Root rule: keep a plan at `docs/Plans/` only if `CURRENT_WORK.md` points to it, or it is an explicitly active near-term effort. See [`docs_folder_hygiene_refinement.md`](docs_folder_hygiene_refinement.md).

---

## Cross-cutting pointers

| Document | Role |
|----------|------|
| [phase-3-multi-loop.md](phase-3-multi-loop.md) | Phase 3 multi-loop requirements (jam capture not shipped) |
| [loop_layer_history_persistence_architecture.md](loop_layer_history_persistence_architecture.md) | DEC-035 — Loop persists content only; Layer A next |
| [runtime_scheduling_admission_model_architecture.md](runtime_scheduling_admission_model_architecture.md) | Runtime timing-telemetry contract (interval reservation deferred). Musical gate: per-event MIDI deadline lateness; MIG is interval/contention |
| [runtime_scheduling_owner_boundary_admission_refinement.md](runtime_scheduling_owner_boundary_admission_refinement.md) | Owner-Boundary Gate roadmap (O–T–R–C–A–P) |
| [consumer_window_budget_ownership_architecture.md](consumer_window_budget_ownership_architecture.md) | After [`213401`](../../captures/session_20260817_213401.log): per-consumer horizon/budget; LCR stays resolver; Experiment 1 detach only |
| [overdub_present_at_tick_jit_architecture.md](overdub_present_at_tick_jit_architecture.md) | DEC-041: `playCommittedLoopMidi` writes `ActiveNoteLedger`; `sendMidiEvent` and `collectOverdubNoteOnParticipantIds` read `Entry.noteId` |
| [overdub_present_at_tick_jit_enhancement.md](overdub_present_at_tick_jit_enhancement.md) | Occupy lookup of `Entry.noteId`; write-before-emit shipped |
| [overdub_occupy_off_tick_display_investigation.md](overdub_occupy_off_tick_display_investigation.md) | 203948 `n=0 a=1` at 96: paint is source-view; named write is `(S, occupy]` after wrap reanchor |
| [overdub_occupy_after_wrap_s_interval_bugfix.md](overdub_occupy_after_wrap_s_interval_bugfix.md) | Catch-up `playMidiEvents` when `lastTickInLoop < occupyPhase` **reverted** [`214856`](../../captures/session_20260818_214856.log); occupy CAP `as=`/`ae=` |
| [overdub_occupy_capture_stream_ledger_bugfix.md](overdub_occupy_capture_stream_ledger_bugfix.md) | **FROZEN** — capture emit-only walk; remaining writer was capture in `mergedMidiEvents` |
| [overdub_occupy_merged_capture_ledger_bugfix.md](overdub_occupy_merged_capture_ledger_bugfix.md) | **FROZEN** — playback `mergedMidiEvents` is committed-only; leftover `n=1 a=0` met on [`231038`](../../captures/session_20260818_231038.log) |
| [overdub_occupy_on_tick_clock_catchup_bugfix.md](overdub_occupy_on_tick_clock_catchup_bugfix.md) | USB occupy ledger catch-up `(lastTick, occupyPhase]`; HITL **FAIL** [`233247`](../../captures/session_20260818_233247.log); successor Off-before-On |
| [overdub_occupy_same_tick_off_before_on_bugfix.md](overdub_occupy_same_tick_off_before_on_bugfix.md) | Catch-up interval Off then On at equal tick; HITL **FAIL** [`235314`](../../captures/session_20260818_235314.log) (4 `n=0 a=1`; `n=1 a=0` met); successor clock equal-tick |
| [overdub_occupy_clock_same_tick_off_before_on_bugfix.md](overdub_occupy_clock_same_tick_off_before_on_bugfix.md) | Clock `rebuildPlaybackOrder` Off before On at equal phase; HITL **PASS** [`001021`](../../captures/session_20260819_001021.log) |
| [overdub_occupy_missing_open_identity_bugfix.md](overdub_occupy_missing_open_identity_bugfix.md) | Extra covering span whose On is absent from playback; pin [`111819`](../../captures/session_20260819_111819.log) L948; 5893-class HITL **MET** [`121141`](../../captures/session_20260819_121141.log); remaining `n=1 a=2` has `on=1` — **STOP** |
| [overdub_occupy_leftover_identity_bugfix.md](overdub_occupy_leftover_identity_bugfix.md) | Leftover Entry whose On is gone from committed stream; pin [`104654`](../../captures/session_20260819_104654.log) `lid=5701`; HITL **PASS** [`111819`](../../captures/session_20260819_111819.log) |
| [overdub_occupy_clock_duplicate_off_ledger_bugfix.md](overdub_occupy_clock_duplicate_off_ledger_bugfix.md) | Clock duplicate Off still applies ledger; HITL [`104654`](../../captures/session_20260819_104654.log) leftover **FAIL**; pin two-Off dumps 0 |
| [overdub_occupy_duplicate_open_identity_bugfix.md](overdub_occupy_duplicate_open_identity_bugfix.md) | Same `noteId` already open → no second Entry; HITL [`103234`](../../captures/session_20260819_103234.log) `led == n` **MET** |
| [overdub_occupy_catchup_open_note_stack_bugfix.md](overdub_occupy_catchup_open_note_stack_bugfix.md) | Catch-up per-phase Off then On; native 095902 fixture; HITL [`101319`](../../captures/session_20260819_101319.log) extra-open remains |
| [overdub_occupy_active_note_ledger_cardinality_refinement.md](overdub_occupy_active_note_ledger_cardinality_refinement.md) | DEC-042 open-NoteOn ledger; nested HITL **MET** [`095902`](../../captures/session_20260819_095902.log); extra-open unmasked |
| [overdub_occupy_unmatched_off_ledger_investigation.md](overdub_occupy_unmatched_off_ledger_investigation.md) | Observability; nested geometry **proven** [`092336`](../../captures/session_20260819_092336.log); successor is cardinality refinement |
| [overdub_loop_head_playback_ledger_bugfix.md](overdub_loop_head_playback_ledger_bugfix.md) | OverdubWrap skips Q16 min-length so On@0 stays in the pass; 0-clock `atLoopStart` occupy. HITL **PASS** [`203948`](../../captures/session_20260818_203948.log) |
| [overdub_loop_head_playback_ledger_investigation.md](overdub_loop_head_playback_ledger_investigation.md) | Closed — wrap seal dropped 8-tick On@0; pin [`185831`](../../captures/session_20260818_185831.log) `54243271` |
| [overdub_wrap_committed_pass_playback_bugfix.md](overdub_wrap_committed_pass_playback_bugfix.md) | Wrap-S ledger catch-up; HITL **PASS** [`185831`](../../captures/session_20260818_185831.log); fail pin [`180844`](../../captures/session_20260818_180844.log) wrap 15 |
| [overdub_wrap_playback_rebuild_before_occupy_bugfix.md](overdub_wrap_playback_rebuild_before_occupy_bugfix.md) | **FROZEN** — full-loop rebuild was the wrong occupy prerequisite; HITL [`180844`](../../captures/session_20260818_180844.log) wrap 15 |
| [overdub_participant_loop_content_architecture.md](overdub_participant_loop_content_architecture.md) | Notes present at tick `S`; `PresentNote` C++ type; NOTE_EDIT overlap sibling; Phase 1 + companion publish HITL; Phase 2a session-undo inverse in tree; source-view prepared-span fill in tree (device gate open); hydrate firmware not authorized |
| [overdub_participant_source_view_span_membership_bugfix.md](overdub_participant_source_view_span_membership_bugfix.md) | 021716 pre-wrap A empty: fill source view from prepared `NoteSpan`s; unprepared MIDI must not finish opens |
| [playback_gather_lcr_consume_enhancement.md](playback_gather_lcr_consume_enhancement.md) | Stage 1 hooks on device. 2-bar gather not the 64-bar overdub stall; Problem B not next firmware |
| [runtime_scheduler_lcr_consumer_grooming_refinement.md](runtime_scheduler_lcr_consumer_grooming_refinement.md) | Consumer rule + A/B/C stalkers; Slice 1 rem split, Slice 2/2b idle one-source (wrap-held edge append). NOTE_EDIT hydrate is not this file. |
| [note_edit_hydrate_enhancement.md](note_edit_hydrate_enhancement.md) | NOTE_EDIT overlap = selected/mover LinearSpan participants (queued; Select stays `selectedTick` neighborhood) |
| [overdub_overlap_hold_same_start_bugfix.md](overdub_overlap_hold_same_start_bugfix.md) | Overdub hold snapshot includes same-start notes (RC1–RC8) |
| [overdub_overlap_hold_enter_source_view_rebuild_bugfix.md](overdub_overlap_hold_enter_source_view_rebuild_bugfix.md) | RC7: enter source view uses wrap rebuild, not visual cache |
| [overdub_overlap_hold_hold_window_jit_bugfix.md](overdub_overlap_hold_hold_window_jit_bugfix.md) | RC8: hold fill is 16-bar this-pitch JIT, not full-loop adds |
| [overdub_overlap_hold_transport_stop_pending_bugfix.md](overdub_overlap_hold_transport_stop_pending_bugfix.md) | RC9: overdub transport stop finalizes pending before All Notes Off |
| [overdub_overlap_hold_live_pending_display_bugfix.md](overdub_overlap_hold_live_pending_display_bugfix.md) | RC10: piano-roll paint applies pending Hide/Shorten at note-off |
| [overdub_overlap_hold_display_cache_bugfix.md](overdub_overlap_hold_display_cache_bugfix.md) | RC11/RC12 FROZEN: source-view consume + display parity; HITL [`140355`](../../captures/session_20260817_140355.log) |
| [overdub_wrap_crossing_hold_head_consume_bugfix.md](overdub_wrap_crossing_hold_head_consume_bugfix.md) | Wrap-shaped off consumes `[S, L) ∪ [0, E)` as one hold; HITL [`155450`](../../captures/session_20260817_155450.log) |
| [overdub_loop_length_during_overdub_enhancement.md](overdub_loop_length_during_overdub_enhancement.md) | Queued: LOOP_EDIT length change during overdub keeps source view, LEN, and playback aligned |
| [overdub_lifecycle_representation_authority.md](overdub_lifecycle_representation_authority.md) | Overdub consume → seal → rebuild → display authority diagram |
| [refactor_priority_backlog.md](refactor_priority_backlog.md) | Cross-cutting refactor priority (P1/P2/P3) |
| [codebase_hygiene_technical_debt_review.md](codebase_hygiene_technical_debt_review.md) | Hygiene backlog + item 18 archive remainder |
| [docs_folder_hygiene_refinement.md](docs_folder_hygiene_refinement.md) | Docs folder cleanup plan |

Living guides and refinements index: [Documentation index](../README.md).
