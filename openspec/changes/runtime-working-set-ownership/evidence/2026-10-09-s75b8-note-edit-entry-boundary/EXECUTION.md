# S7.5b.8a — production entry/focus/feedback boundary oracle (G0–G2 packet and fixture record)

Fixture-only slice of [S7.5b.8 — MIDI-separated NOTE_EDIT entry and focus preparation](../tasks.md#s75b8-note-edit-midi-boundaries).
No firmware line is changed by this slice.

## Identity and scope

- **Task:** `runtime-working-set-ownership` **S7.5b.8a — freeze the production entry/focus/feedback boundary
  oracle**; the spec requirement it serves is *NOTE_EDIT entry and focus work preserve MIDI service
  boundaries* (`specs/runtime-working-set-ownership/spec.md`, scenarios *Opening NOTE_EDIT while musical
  work continues*, *Selection or focus preparation needs more than one unit*, *Target changes between
  preparation units*, *Feedback completes without delaying musical deadlines*).
- **Status:** fixtures implemented and native-verified; the documented boundary cases are **RED** behind
  `-DS75B8_ENTRY_BOUNDARY_ORACLE` and belong to S7.5b.8b. No timing claim; S7.5b.8d remains unrun.
- **Production entry → owner path exercised (real code, no copied state machine):**
  `MidiButtonActions::handleCycleEditMode` → `ControlSurfaceManager::cycleEditSession` →
  `EditManager::cycleEditSession` → `sendEditSessionChange(EditSessionType::Note)` →
  `reopenNoteEditSession` → `openNoteEditSession` (`Loop::assignMissingNoteIdsInCommittedCapturePasses`,
  `Loop::rebuildVisualCacheFromPasses` / `Loop::rematerializeEditView`) →
  `enterDefaultNoteEditSessionState` (`selectNoteAtBracket` / `selectClosestNote`,
  `ensureNoteEditDisplayProjectionCachesBuilt`, `applySelectNav`) → `emitSessionOpenedToSurface(true, true)`
  → `ControlSurfaceManager::handleSessionOpenedEvent` → `prepareNoteEditSessionOpen` →
  `sendNoteEditSessionFaderFeedback` → `requestFaderOutbound(Trigger::SessionOpen)` →
  `drainFaderOutboundUntilIdle`; the focus/slot re-entry
  `EditManager::beforeSelectedTrackChange` / `onTrackChanged` / `onSelectedSlotChanged` /
  `reenterEditSessionForFocusChange` (`TrackManager::setSelectedTrack`,
  `TrackManager::setSelectedSlotIndex`); the load entry
  `EditManager::rematerializeNoteEditSessionAfterWorkspaceReload`; the clear entry
  `EditManager::revertNoteEditSessionForLoopClear`; the committed playback edge
  `Track::playMidiEvents` with `MidiHandler` (`MidiSendCapture`) and the active-note ledger.
- **Primary files:** `test/test_note_edit_entry_boundary/test_note_edit_entry_boundary.cpp` (new suite),
  `test/test_note_edit_entry_boundary/off_path_deps.h` (boundary stub surface, adapted from
  `test_windowed_preview_integration/off_path_deps.h`), `scripts/native_test_link.py`
  (`_FIRMWARE_BOUNDARY_SUITES` registration).
- **Harness:** the windowed-preview combination (window lane + real `Track`/`TrackManager`/`Loop` +
  **real** `src/EditManager*.cpp` + real `MidiHandler`) extended with the real
  `src/ControlSurface/EditEventFeedback.cpp`, the real `src/MidiButtonActions.cpp` and the real
  `src/TrackManager/TrackManagerCaptureQueue.cpp` (so a slot move cannot skip the in-flight-capture
  decision). The suite registers `editManager.setEditEventListener(&controlSurfaceManager)` exactly as
  `src/main.cpp` does — without it no session-opened event reaches the feedback seam and the oracle would
  be vacuous. `TrackPlaybackTestAccess` (a `Track` friend in `include/Track.h`) is used only to read the
  slot's playback runtime and its active-note ledger.
- **Declared boundaries (named in `off_path_deps.h`, never silently deciding an asserted outcome):**
  the feedback **send pipeline** (`requestFaderOutbound` records trigger + plan; `processFaderOutbound`,
  `drainFaderOutboundUntilIdle` and `ControlSurfaceManager::update` are stand-ins, so no assertion depends
  on what they do — the RED case asserts the production *call site* of the drain); the five `EditStates`
  FSM TUs (host ill-formed; note that production's `EditSelectNoteState::onEnter` runs a second
  `selectClosestNote`, so the selection asserted here is the one
  `EditManager::enterDefaultNoteEditSessionState` itself builds); storage/persistence requests, display
  projections and the transport start/stop/record-stop routes (observable or fail-closed).
- **Non-goals:** no continuation seam, no admission constant, no new quantum, no serial marker, no
  stimulus, no firmware edit, no SD/device action, no numeric timing limit (S7.5a supplies the envelope),
  and no claim that fixture-only evidence closes the parent S7.5b.8 timing gate.

## G0 — what the entry does today (measured; read from code and from the fixtures)

| # | Finding | Class |
|---|---|---|
| 1 | The whole open is **one synchronous call inside the button callback**: content preparation (`rebuildVisualCacheFromPasses` / `rematerializeEditView`), derived-cache invalidation, the focus/selection build and the feedback request all complete before `handleCycleEditMode()` returns. Nothing distinguishes "interaction accepted" from "session complete". | VERIFIED (RED case 2) |
| 2 | `ControlSurfaceManager::handleSessionOpenedEvent` **polls the send pipeline to idle inside the same callback** (`drainFaderOutboundUntilIdle()`), while production's own continuation steps the same pipeline from `ControlSurfaceManager::update()` once per main-loop turn. This is the poll-around-work shape the requirement names, and the *documented missing-yield case* of this slice. | VERIFIED (RED case 1) |
| 3 | The session-open feedback request is a **single unit** carrying the plan `{fader1, waitFader1Echo, coarse, fine, noteValue}`; the plan and stage order are untouched by the entry. | VERIFIED (baseline) |
| 4 | On a **current-root** source the open selects a committed identity (`selection.{trackId, loopId, primaryNote}`, exactly one primary) and the committed interval keeps sounding **exactly** through `Track::playMidiEvents`: two bars emit the four expected rows (ticks 0/48/768/816, pitch 48, velocity 100/0, On then Off), the Track sink equals `MidiSendCapture`, and the ledger holds the identity `2000` after its On and not after its Off (empty edit delta, DEC-055). | VERIFIED (baseline) |
| 5 | On a **stale/unpublished-root** source (`Loop::isPlaybackSessionExitPublicationGap()` true) `Loop::rebuildVisualCacheFromPasses` returns early (`src/Loop/LoopVisualCache.cpp`), so the loop's visual cache — and the selectable inventory `EditManager::filteredSelectableDisplayNotesForNoteEdit` reads — stays **empty**: the entry projects the committed content exactly, keeps it as the audible source (`Track::hasAudiblePlaybackSource`) and publishes **no selection identity** rather than a partial one. | VERIFIED (baseline) |
| 6 | A **track** change between units (production `TrackManager::setSelectedTrack`) re-enters the session for the new target and raises exactly one further session-open unit; the new track's slot identity and content are published and the departed target's are not. A **slot** change (`TrackManager::setSelectedSlotIndex` on the selected track) behaves the same way. | VERIFIED (baseline) |
| 7 | In-session **mutation** (session store + `Track::invalidateCaches(true)` + `EditManager::flushDeferredNoteEditDisplayRefresh`) advances the preview revision by exactly one and leaves the published identity and the feedback accounting untouched. A **workspace reload** (`rematerializeNoteEditSessionAfterWorkspaceReload`) rebinds the session to the loaded slot with no additional feedback unit. A **clear** (`revertNoteEditSessionForLoopClear`) closes the session identities with no further unit. | VERIFIED (baseline) |
| 8 | Feeding the production **reopen** entry a **superseded track reference** after the selection moved publishes a **mixed** identity (the new track id with the departed slot's loop, `Expected 1 Was 0`) and revives the departed slot's content. Identity rejection for a late completion does not exist today. | VERIFIED, RED control 3 |
| 9 | Native/hardware timing of the entry (individual unit durations, composed MIDI gaps, capture timestamp error, per-route deadlines, feedback completion) is **not** established here. | UNKNOWN → S7.5b.8d |

### Unit / continuation / lifetime contract S7.5b.8b must close before coding

Pinned from the code above; each line names an existing owner and has a fixture in this suite.

- **Admitted units to bound (today all inside one input turn):** (a) content preparation —
  `Loop::rebuildVisualCacheFromPasses` or `Loop::rematerializeEditView`; (b) derived-cache invalidation
  (`EditManager::invalidateNoteEditDerivedCaches`); (c) selection/focus preparation —
  `enterDefaultNoteEditSessionState` → `ensureNoteEditDisplayProjectionCachesBuilt` →
  `selectNoteAtBracket`/`selectClosestNote` → `applySelectNav`; (d) feedback preparation —
  `requestFaderOutbound(Trigger::SessionOpen)`; (e) each send step — `processFaderOutbound()`. Each needs a
  declared quantum and must return to `handleMidiInput()` between units.
- **Publication rule:** only a complete session/slot/`NoteId`/source-qualified result publishes; private
  partial focus must not become ready (the rootless baseline asserts the no-partial-selection answer for an
  unready inventory).
- **Owner:** `EditManager` keeps session/selection/focus authority; `ControlSurfaceManager` keeps feedback
  preparation and outbound continuation; `main.cpp::loop()` integrates the stepping. No second coordinator
  and no poll-around-unbounded-work loop.
- **Cancellation/lifetime:** exit/reopen, track/slot change, mutation, workspace reload, clear/load and
  source supersession must invalidate or revalidate only affected pending work (the baseline fixtures pin
  the observable order today); a continuation must hold no borrowed reference that outlives the session it
  belongs to. `Loop` keeps source publication ownership; `Track` keeps playback-window preparation.
- **Explicitly forbidden by the requirement, present today:** the nested
  `drainFaderOutboundUntilIdle()` inside the session-opened callback (finding 2).

## Oracle frozen by the fixtures

Suite: `test/test_note_edit_entry_boundary` (`pio test -e native -f test_note_edit_entry_boundary`).

**Baseline oracle (unflagged, green — 12/12):**

| Fixture | Shape | Pinned expectations |
|---|---|---|
| `…publishes_an_identity_qualified_session_on_a_current_root_source` | long (64-bar) current root, playing | session type `Note`; `selection.{trackId 0, loopId == loop.loopId, primaryNote}` with exactly one primary; empty edit delta; the four exact committed rows (tick/pitch/velocity/type) and Track sink == `MidiSendCapture`; ledger holds identity `2000` after its On and not after its Off; exactly one session-open unit with the literal normative flags (F1, F1-echo wait, F2, F3, F4) |
| `…on_a_rootless_source_keeps_the_audible_source_and_no_partial_selection` | 2-bar committed take, unpublished-root gap | gap and empty inventory asserted before and after the entry (the rebuild attempt is suppressed); committed content projected exactly (both halves per pair); **no partial selection**; audible source retained; one unit |
| `…on_dense_content_keeps_the_exact_committed_inventory` | 2-bar, 32 note pairs | projection exact (64 rows, halves split exactly); no fabricated selection |
| `…with_an_empty_selection_publishes_no_selection` | controller-only committed take | session opens coherently; no selection identity; one unit |
| `…second_press_returns_to_loop_edit_and_releases_the_session` | 2-bar | second press leaves NOTE_EDIT, clears the selection identity, releases the audition, and invents **no** further unit (`==`, not `>=`) |
| `…session_exit_and_reopen_publishes_exactly_one_fresh_identity` | 2-bar | reopen republishes NOTE_EDIT bound to the current slot; exactly one unit per open; every committed identity still served |
| `…focus_change_between_units_revalidates_the_session_identity` | track change; new target is **track 1 slot 1** (departed: track 0 slot 0) | new track id and **new slot identity** published (`LoopId` is the slot index, so the identity pair discriminates); the projection carries only the new slot's pitches (70..73); exactly one further unit |
| `…slot_change_between_units_revalidates_the_session_identity` | slot change on the selected track (slot 0 → slot 1) | the session follows the newly focused slot's identity and content (pitches 80..83); exactly one further unit |
| `…mutation_between_units_keeps_the_identity_and_requests_no_feedback` | in-session paired, identified edit + production flush | preview revision advances by exactly one; track/slot/`NoteId` identity unchanged; no further unit |
| `…workspace_reload_between_units_rebinds_the_session_without_feedback` | production load re-entry | session rebound to the loaded slot identity; the rebind serves only the loaded pitches (60..63); no further unit |
| `…clear_between_units_invalidates_the_pending_session_identities` | production clear entry | session closed; no selected identity survives; no further unit |
| `…entry_while_a_competing_track_plays_keeps_both_tracks_exact` | two playing tracks with own current roots, different pitches (48 vs 50) | the entry track's entry issues one unit; the competing track still emits its four exact committed rows and only its own pitch |

**Boundary oracle (flagged `-DS75B8_ENTRY_BOUNDARY_ORACLE`; RED today, S7.5b.8b must turn it green):**

| Fixture | RED assertion (verbatim) | Class |
|---|---|---|
| `…entry_returns_to_midi_input_between_admitted_units` | `Expected 0 Was 1. the entry must not poll the send pipeline to idle inside the callback (no polls around work)` | missing yield |
| `…entry_keeps_the_accepted_gesture_pending_when_the_owner_turn_is_withheld` | `acceptance is bounded metadata: a withheld owner turn must not already publish the session` | withheld grant (control) |
| `…entry_refuses_a_late_completion_for_a_superseded_track_reference` | `Expected 1 Was 0. only the current target's slot identity may be published (no mixed track/slot pair)` | identity rejection (control) |

The three failures are independent: (1) is about *where* the work runs (the callback vs the owner turn),
(2) about *when* the session becomes ready with no owner turn granted, (3) about *which identity* a late
completion may publish. A change that fixes one does not silence the others; case 3 additionally fails the
content clause (`the completion must not revive the departed slot's identities`) after the identity clause.

## Controls and verification

- **Reused seams:** the real button action, the real session/feedback seam, the real `EditManager`
  lifecycle, the real slot/focus/load/clear entries, the real `Track` MIDI edge with its active-note ledger
  and `MidiSendCapture`. No new test-only production hook was added and no route substitution sits on an
  asserted path (the declared boundaries are listed above).
- **Controls:** withheld owner turn and superseded-identity completion (flagged); the focus-change,
  slot-change, mutation and clear baselines act as the "guard's job done correctly" counterparts.
- **Coverage note — in-session undo/redo between units:** not duplicated here. The session's undo/redo
  *entries* are production and already pinned by the windowed-preview integration suite
  (`test_windowed_preview_integration`, `EditManager::sessionUndo`/`sessionRedo` through the real owner);
  this suite adds the **mutation** boundary, which is what the entry oracle is about.
- **Untestable-before-link (recorded, not asserted):** the motor send steps (S7.5b.8c), the FSM state
  entries, and a byte-exact *reopen* projection row count (the reopen composes the committed base with the
  pass the exit baked; the fixture asserts every committed identity is present and that the session is
  bound to the current slot).

### Verification recipe and evidence

```
# focused (baseline oracle)
pio test -e native -f test_note_edit_entry_boundary                  # 12 tests / 0 failures
# boundary oracle (documented RED, S7.5b.8b's target)
PLATFORMIO_BUILD_FLAGS='-DS75B8_ENTRY_BOUNDARY_ORACLE' \
  pio test -e native -f test_note_edit_entry_boundary                # 15 tests / 3 failures (above)
# compatibility protection
pio test -e native
git diff --check
openspec validate runtime-working-set-ownership --strict
```

- **Focused:** `12 Tests 0 Failures 0 Ignored` (unflagged); `15 Tests 3 Failures 0 Ignored` (flagged),
  each failure naming its own case above.
- **Broad:** the full-native result line for this slice is recorded in `tasks.md` and
  `PROJECT_STATE.md` (all suites PASSED, 0 failed).
- **Device/build:** no firmware line changed by this slice, so no capture build or upload was performed and
  none is claimed.
- **Review:** one focused independent review of the fixtures and this packet; its concrete findings
  (vacuous silence assertion, stub-owned unit assertions, non-discriminating slot-identity control,
  plan-flag expectation read back from the same helper, `>=` request comparison, and the missing
  slot-change / mutation / load cases) are all repaired in the fixtures and this text.
