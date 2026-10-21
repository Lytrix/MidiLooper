# 2.12 / 3.13 — preview-contract closeout (native)

**Change:** `windowed-playback-generation` · **Tasks:** 2.12 (preview-source fixture) and 3.13 (preview contract —
immutable deferred publication) · **Date:** 2026-10-21 · **Branch:**
`feature/unified-rolling-playback-path`, base `5ca226da`.
**Nature:** verification + docs only. No `src/` or `include/` line changed, so both images and every pin are
unchanged by construction.

The two tasks close together because they are one claim from two sides: 2.12 is the fixture, 3.13 is the contract that
fixture verifies, and 3.13's own note named exactly two owed halves — *"fixture 2.12's integration half and the
production callers"*. The integration half landed 2026-10-05; the production-callers half landed 2026-10-21 as **3.15d
slice 1** (interval composition) and **slice 2** (clock-edge adoption plus the unconditional per-track requester visit).
2.12's own text additionally needs the interval-qualified shape (slice 1) and the save clause (slice 3), neither of which
existed when the 2026-10-05 evidence was written.

Nothing in this closeout adds a term to either task.

## 2.12 — clause by clause

`test/test_windowed_preview_integration` (29 cases) unless marked RWO
(`test/test_runtime_working_set_ownership`, 230 cases).

| Clause | Fixture | The assertion that carries it |
|---|---|---|
| **on a long loop** | all of them | `kLongLoopTicks = 64 × 768 = 49 152`; the emission fixtures use `commitEmissionLoop` on the same length |
| **edit while playing** | `test_3_15d_slice3_short_and_long_note_edit_emit_the_same_stream`, `test_3_15d_slice2_note_edit_serves_the_preview_interval_at_the_clock_edge` | the session is open on a playing track and the edit is **served** — the edge fixture asserts the preview frame, not the merged reader, is what the turn consumed |
| **cross window boundaries** | `test_3_15d_slice2_the_lane_re_covers_the_playhead_across_intervals`; `test_3_15d_slice2_the_lane_re_covers_the_playhead_when_its_interval_ends` | a real traversal from `kPlayhead` past the second interval's content (3 100) keeps serving: the crossing On/Off sound **once** each, the re-covered interval serves at 3 100, the lane holds the playhead's interval at 3 600, and the ledger closes empty; the second fixture pins *when* the re-cover happens (coverage end) and that a cadence one boundary ahead would be refused by name |
| **edit-session undo/redo** | `test_an_in_session_undo_and_redo_advance_the_preview_revision_and_the_served_content` | production `pushSessionUndoOnKindChange` / `sessionUndo` / `sessionRedo`: each advances the preview revision and the served window follows to *that* revision's content; the undone edit is gone |
| **switch selected tracks** | `test_a_selected_track_change_cancels_the_departing_tracks_preview_by_its_own_lifecycle`; `test_3_15d_slice2_a_slot_switch_yields_the_departing_slots_preview` | the production focus route re-opens the session on the incoming track; the departing preview is cancelled as `SelectedTrackChanged` (`NothingToCancel`/`BuildCancelled`, never a committed build), its served window is untouched field-for-field, and **no** handoff is owed; the slot switch additionally asserts the departing frame yields (`rolling.phase != Serving`) |
| **mutate … between build grains** | `test_a_mutation_between_build_grains_never_publishes_a_mixed_or_stale_preview`; RWO `test_preview_window_is_cancelled_by_its_lifecycle_and_hands_off_to_committed` | a rebuild is begun and one grain taken, then mutated: the cancelled completion publishes **nothing**, the served window stays the previous revision *and* the previous content (never a mix), and the new revision publishes only as a complete successor; the RWO case asserts `BuildCancelled` on an in-flight build and that a cancelled build publishes no window at all |
| **… or exit between build grains** | RWO `test_cancelling_withheld_preview_unblocks_next_admission` | the exit lands while the completed promise is **pending judgement**: `BuildCancelledAndHandoffOwed`, the promise is cleared, the source released, the handoff owed, and the next admission is admitted |
| **then save** | `test_the_save_serializes_the_committed_checkpoint_and_never_the_served_preview`; `test_3_15d_slice3_a_save_while_the_frame_serves_leaves_it_serving` | the serialized body carries the committed checkpoint and **never** the preview; the slice-3 case performs the save while the frame — not merely the publication — is Serving, and the frame keeps the same `Preview` revision and bound storage generation afterwards |
| **identified as preview** | `test_the_session_owns_the_preview_source_and_its_revision_is_the_lane_currency`; `test_3_15d_a_preview_window_is_never_committed_currency` | `identity.origin == Preview`, `servedPlaybackWindowIsPreviewForSlot`, and `origin` is part of identity equality so a preview never equals the committed window it stands in front of; a preview names no published snapshot root |
| **invalidated by its own lifecycle** | the mutation, re-focus, slot-switch and exit fixtures above; `test_3_15d_slice2_a_superseded_revision_never_serves`; `test_3_15d_slice3_a_refused_preview_demand_is_released_at_the_session_edge` | every cancellation is **named** (`SourceMutated` / `SelectedTrackChanged` / `SessionExited`) and only ever cancels the lane's own work; a superseded revision never serves; a *refused* demand no longer outlives the session that owned it |
| **no mixed or stale content** | `test_3_15d_the_interval_preview_never_mixes_revisions`; the mutation fixture; `a_superseded_revision_never_serves` | the served window is the **newest** revision's composition (2304/2500 edit plus the unchanged 3072/3120 committed content), with no row from the superseded revision |
| **never committed currency** | `test_3_15d_a_preview_window_is_never_committed_currency`; `test_3_7a_i3_the_exit_owes_the_handoff_and_never_re_requests_a_preview` | `committedPlaybackWindowServesSlot` is false and the committed event count is 0 while the preview serves; the exit owes the handoff instead of promoting the preview |

## 3.13 — contract terms

| Term | Where it is pinned |
|---|---|
| **source freezing** (the preview revision a build works from) | `test_the_session_owns_the_preview_source_and_its_revision_is_the_lane_currency`: the session's own revision is the lane's currency, the served stream is the session's own projection, and nothing serves the slot before the session exists |
| **cancellation — session mutation** | mutation-mid-build fixture (`SourceMutated`), plus the RWO in-flight `BuildCancelled` case |
| **cancellation — track re-focus** | the re-focus fixture and `test_3_7a_i3_a_refocus_yields_the_preview_without_owing_a_handoff` |
| **cancellation — undo** | the undo/redo fixture (the undo advances the revision and the served window reverts) |
| **cancellation — exit between build grains** | RWO `test_cancelling_withheld_preview_unblocks_next_admission` |
| **committed-content handoff at session exit** | `test_the_session_exit_owes_the_committed_handoff_and_the_preview_keeps_serving` (`HandoffOwed` → committed successor prepared while preview serves → adopted → discharged), `test_3_15d_slice2_the_session_exit_yields_the_preview_frame` |
| **verify 2.12 passes** | the table above; both suites green |
| **preview never accepted as committed currency** | `test_3_15d_a_preview_window_is_never_committed_currency`, RWO `test_preview_window_is_a_distinct_source_and_never_committed_currency` |
| **the live mutable fallback's removal is not narrowed on preview's account** | `test_3_15d_slice2_note_edit_serves_the_preview_interval_at_the_clock_edge` (the preview, not the merged reader, is what the turn consumes once the lane is admitted) and `test_3_15d_slice3_short_and_long_note_edit_emit_the_same_stream` (the merged reader serves only where the lane is **refused**). Preview therefore does not require the fallback to stay; the fallback's remaining short-loop dependency is 3.15f's named supported-route failure, not a preview dependency |
| **production callers** (the half 3.13's note left open) | `Track::servicePreviewWindowForNoteEditSession` is called from `src/main.cpp`'s unconditional per-track visit, from the session lifecycle's open/mutation seams and from the display-projection seam; `Track::cancelPreviewWindowForSlot` from the session exit (`SessionExited`) and the depart route (`SelectedTrackChanged`); the edge reaches the preview through `Track::resolveNoteEditPreviewSourceForTurn`. `test_3_7a_i3_the_focus_lane_turn_requests_the_sessions_preview` is the proof the requirement is firmware-owned: it never calls `requestPreviewWindow` itself, so a published preview can only come from the production caller |

## What this closeout supersedes in the 2026-10-05 evidence

- **"interval == whole loop"** was true of the 2026-10-05 shape and is **not** true now: `requestPreviewWindow` composes the
  committed route's own interval (3.15d slice 1), and
  `test_the_session_owns_the_preview_source_and_its_revision_is_the_lane_currency` asserts the published interval equals
  `playbackIntervalAt(track, 0, kContentPhase)` rather than the loop length.
- **"production callers owed"** — landed (3.15d slices 1–2; the table row above names every call site).
- **"the `then save` clause belongs to the storage harness"** — landed as two fixtures, the second of which saves while the
  frame serves (slice 3).

## Limits recorded, not smoothed over

- **Redo is asserted at the preview boundary, not as a byte-exact redo of the fixture's own edit.** The fixture's mutation
  writes its row into the session store, while the session's current-state model (what redo re-projects from) is built from
  the rematerialized loop, so redo restores the loop's own content. A production geometry edit runs through the edit FSM,
  whose five state TUs cannot be linked natively (`-Wc++11-narrowing` on a `uint32_t`→`uint8_t` braced initializer, reproduced
  outside this harness). The contract's own claim — the revision advances and the served window is the session's projection at
  that revision, never the undone one — is asserted.
- **No device run, and none is owed here.** 2.12/3.13 are native-only by text; audible route parity, timing and the
  registered content profile are 3.15f's and 6.5's.
- **The display consumer is not this contract.** The served-window display path is `runtime-working-set-ownership` **S4e**;
  nothing here claims a displayed-provenance term.
- **2.12's fixture is a suite, not one chain.** Each clause has its own case, driven by production entries; no single fixture
  walks all eleven terms in one session.

## Gates

| Check | Result |
|---|---|
| `pio test -e native -f test_windowed_preview_integration` | **PASS** — 29/29 |
| `pio test -e native -f test_runtime_working_set_ownership` | **PASS** — 230/230 |
| `pio test -e native` | **PASS** — `/tmp/native_212_313.log`: 3 370 cases / 7 skipped / 3 363 succeeded / 0 failed |
| `teensy41-capture-serial` | **PASS** — unchanged by construction (no `src/`/`include/` change since `5ca226da`) |
| `teensy41-capture-serial-window-qualification` | **PASS** — unchanged by construction |
| `openspec validate windowed-playback-generation --strict` | **PASS** |
| `git diff --check` | **PASS** |
