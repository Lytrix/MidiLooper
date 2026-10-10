<files>
- `src/EditManager/NoteEditSessionLifecycle.cpp:191–311` — synchronous open/reopen and their internal tails.
- `src/EditManager/EditSessionDepart.cpp:58–151` — session-change and track/slot refocus tails.
- `src/EditManager/EditNoteStateCoordinator.cpp:169–176,277–284` — requested-state and cycle continuations.
- `src/ControlSurface/NoteEditButtonInput.cpp:47–133` — short-press/default-entry/create continuations.
- `src/EditManager/NoteEditSelection.cpp:87–235` — selection publication and synchronous FSM entry.
- `src/EditStates/EditSelectNoteState.cpp:27–49` — real SELECT entry omitted by current fixtures.
- `docs/Authority/IMPLEMENTATION_READINESS.md:34–54,135–155` — G0 evidence and production-path fixture requirements.
- `openspec/changes/runtime-working-set-ownership/design.md:1483–1539` — S7.5b.8 — NOTE_EDIT boundary contract and outstanding integration dependencies.
</files>

<answer>

Branch verified: `feature/unified-rolling-playback-path`. Read-only investigation; no edits, tests, upload or delegation.

## VERIFIED — concrete caller tails

All production occurrences of the three APIs were traced. `enterEditMode` has a declaration but no production caller found beyond its definition; its API continuation nevertheless exists.

| Caller | Immediate synchronous tail / payload dependency | Action intent that cannot disappear while waiting |
|---|---|---|
| `sendEditSessionChange` (`EditSessionDepart.cpp:58–91`) | Writes session type before reopening; remembers prior type, commits departed LOOP_EDIT, leaves loop editing; after reopen emits `SessionClosed`, then `SessionOpened(true,true)` when type changed. Events synchronously invoke the surface. | Requested session type, prior-session transition, MIDI-notification flag and session-open feedback intent/order. |
| `reenterEditSessionForFocusChange` (`EditSessionDepart.cpp:108–119`) | Only reopens NOTE_EDIT when `editSession.active`; immediately emits `SessionOpened(false,true)`. `previousSlot` is ignored. | Refocus/reopen of the actual selected track/slot, feedback without session MIDI. A pending-but-inactive entry would currently bypass this branch. |
| `enterEditMode` (`EditNoteStateCoordinator.cpp:169–176`) | If inactive: open, emit feedback, then **unconditionally** `setState(newState,track,startTick)`. `setState` commits geometry, exits old FSM and invokes new `onEnter` synchronously (`:26–35`). | Explicit requested state pointer and tick—not merely default SELECT. |
| `cycleNoteEditType` (`EditNoteStateCoordinator.cpp:277–284`) | If inactive: open + feedback; then `applyCycleEditKind`, consuming newly initialized kind and `encoderCycleNeedsAnchor_`, synchronizing UI/FSM (`NoteEditSelection.cpp:120–123`). | One cycle action after entry, with its anchor semantics. Returning after accepting open would lose the cycle. |
| `handleCycleNoteEditType` (`NoteEditButtonInput.cpp:47–66`) | Captures **pre-action** `wasInEditOverlay`; may first `sendEditSessionChange(Note)`, then immediately branches on active/current FSM. Short-entry branch can open again, emit feedback, or call default entry and immediately schedule fader sync. Otherwise invokes cycle. | Enter-versus-cycle classification based on original overlay, original interaction tick, and whether feedback is session-open or selection sync. |
| `handleCreateNoteAtBracket` (`NoteEditButtonInput.cpp:83–133`) | Rejects null FSM before opening. After open reads active-loop length/origin and selected bracket, scans cached notes, clears index, begins Add/undo, creates events, resolves overlap, commits Create row, restores bracket, invalidates, selects new note and schedules feedback. | Create action plus qualified bracket/target; opening currently can reset selection before the create tail reads it. Must not silently become “open only.” |
| `openNoteEditSession` (`NoteEditSessionLifecycle.cpp:270–282`) | After default selection: bumps display/playback-preview revisions and immediately calls `track.servicePreviewWindowForNoteEditSession()`. | Completion includes qualified selection/FSM and initial preview-source request, not merely `active=true`. |
| `reopenNoteEditSession` (`NoteEditSessionLifecycle.cpp:305–310`) | Wraps open with `deferSelectionSurfaceEvents_=true/false`, then invalidates track caches. | Selection-event suppression spanning the actual reopen, followed by cache invalidation. Lexical suppression ends too early if open merely starts deferred work. |
| `rematerializeNoteEditSessionAfterWorkspaceReload` (`NoteEditSessionLifecycle.cpp:459–462`) | Reopen then return; outer `applyWorkspaceDisplayRefreshPending` clears pending flag, invalidates display first, then immediately updates LEDs (`DisplayCacheLifecycle.cpp:313–324`). | Rebind to authoritative loaded content; this path does **not** emit session-open feedback. |

Additional outer tails:
- `onTrackChanged` clears mode/index/bracket before refocus (`EditSessionDepart.cpp:135–142`); `TrackManager::setSelectedTrack` then prioritizes restore, invalidates display and requests footer/workspace persistence (`src/TrackManager.cpp:243–258`).
- `setSelectedSlotIndex` commits departure, changes selected slot, optionally changes active slot, then refocuses; afterward invalidates display, refreshes LEDs and persists selection (`src/TrackManager/TrackManagerSlotMatrix.cpp:299–323`). Selected and active slots can deliberately differ.
- `onBootSlotLoadComplete` refocuses then refreshes LEDs (`src/TrackManager.cpp:220–227`).
- MIDI action wrappers add no state-dependent tails: `handleCycleEditMode` logs after surface cycling; cycle/create delegate directly (`src/MidiButtonActions.cpp:933–952`). GPIO short press reaches cycle-type (`src/GpioButtonManager.cpp:238`).

## VERIFIED — real selection/FSM graph and nested constituents

`enterDefaultNoteEditSessionState` (`NoteEditSelection.cpp:189–235`):
- Zero-length path clears selection/index/fader NoteId and returns **without entering an FSM**.
- Otherwise consumes transport tick, selected-loop start tick, loop origin/length and navigation clamp; selects exact bracket or nearest; reads inventory again to obtain NoteId; writes selection track/loop IDs; synchronizes FSM; finally updates reference step.
- `selectNoteAtBracket` → inventory copy/scan → `applySelectNav`, or `selectClosestNote` → clamp/inventory scan → `applySelectNav` (`EditNoteStateCoordinator.cpp:58–111`).
- `applySelectNav` writes kind/selection and undo-warm metadata, synchronizes UI/FSM **before** emitting selection feedback (`NoteEditSelection.cpp:87–117`).
- `syncNoteEditSessionStateToUi` obtains inventory, resolves index, updates fader NoteId/mode, commits departed geometry, exits old state, assigns target state, then invokes `onEnter` (`:126–186`).

Real `EditSelectNoteState::onEnter`:
> `lastMidiEventCount = track.editAwareMidiEvents().size();`  
> `manager.selectClosestNote(track, startTick);`  
(`src/EditStates/EditSelectNoteState.cpp:31–35`)

Thus default entry invokes a **second selection pass through the FSM**, not just a scalar state assignment. Assigning `currentState` before `onEnter` prevents recursive re-entry of that same state.

Other real FSM tails:
- Home repeats closest selection (`EditNoteHomeState.cpp:9–10`).
- Start hashes all edit-aware events, obtains selected inventory row, rebuilds focus and changes selected tick; otherwise closest selection (`EditStartNoteState.cpp:12–32`).
- Length hashes events, calls closest selection, reads live selected note, stores target NoteId and sets end bracket (`EditLengthNoteState.cpp:12–25`).
- Pitch hashes events, reads live selected note, stores target NoteId and sets start bracket (`EditPitchNoteState.cpp:12–23`).
- Closest selection calls `applySelectNav`, which resets kind to Select; consequently those nested transitions are actual semantic mutations, not harmless observation.

Nested content-sized work remains:
- Clamp scans committed pass/chunk extents and visual notes (`src/EditManager.cpp:84–119`; `src/Loop/LoopCapture.cpp:51–77`).
- Inventory getters return copies and can build projection caches: snapshot resolution, committed-base copying, nested Capture dedup, projection and filtering (`NoteEditDisplayProjection.cpp:21–44,84–150`; `NoteEditSelection.cpp:237–261`).
- `track.editAwareMidiEvents()` routes to mutable session events while active, otherwise committed scratch refresh (`TrackMidiEventRouting.cpp:15–32`; `src/EditManager.cpp:201–233`).
- Focus rebuild upserts mover state, mutates focus/baselines, evicts scratch and scans visual baselines even with mover-only scope (`NoteEditFocusRebuild.cpp:127–159`; `EditSessionInteraction.cpp:261–276`; `NoteEditFocusState.cpp:224–243`).
- Empty-selection focus rebuilding can preserve/copy overlap maps and scan pending baseline differences (`NoteEditFocusRebuild.cpp:76–124`).
- Open still performs committed-ID inspection; stale/rootless branch rebuilds visual cache, rematerializes store, flattens events, stamps IDs and builds current state (`NoteEditSessionLifecycle.cpp:195–254`). Capture diagnostics add nested inventory/store comparisons (`:47–149,242–258`).
- Reopen clears store/current-state/undo/edit-row payloads synchronously (`:291–304`). Individual retirement costs are **UNKNOWN**, not proven constant.

Declared bound: composed open/focus helpers are explicitly **Unbounded** (`docs/Authority/Architecture/ExecutionBounds.md:187`).

## VERIFIED — qualification/cancellation terms

Existing selection primitive qualifies selected Track/index/slot, Loop pointer/ID, session active/type, both preview revisions, prior selected NoteId/tick/index; source additionally qualifies root identity/revision, Track generation, playback/visual/Capture generations, visual dirty flag and loop geometry (`NoteEditSelectionPreparation.cpp:14–53`).

It rejects overlays/dirty values/focus/owned rows and stopped dirty visual sources (`:31–40`); waits for current source, discards stale coordinates, reacquires root per step (`:183–214`). Complete query rechecks everything and never advances work (`:336–342`). No production caller begins/steps this primitive yet.

Reopen rejects a departed Track **before mutation**, but open/default entry lack that guard (`NoteEditSessionLifecycle.cpp:191–194,285–289`). Clear resets selection preparation through `resetNoteEditSessionState` (`NoteEditSelection.cpp:41–56`); `revertNoteEditSessionForLoopClear` also clears payload/focus/FSM and returns to LOOP_EDIT (`NoteEditSessionLifecycle.cpp:419–454`). `Track::clearTrack` mutates its active slot before that hook (`TrackLoopJamGeometry.cpp:51–67`).

Surface events carry no target identity: `emitEditEvent` invokes listener synchronously; surface resolves the **currently selected Track** (`src/EditManager.cpp:263–265`; `EditEventFeedback.cpp:104–108`). Session-open flags are mutable owner fields (`EditSessionDepart.cpp:52–55`). Their safe retention across yields is **UNKNOWN**.

## VERIFIED — cooperative opportunities / coverage limits

Existing main opportunities: unconditional MIDI input at `src/main.cpp:832,1011`; conditional drains `:907,922,954`; surface update `:191`; deferred owner maintenance `:913`; load/display `:941`; motor continuation `:957`; persistence `:1001`. Real SELECT overdub update is independently invoked at `:869–871`. None currently services entry/selection preparation.

Session feedback immediately prepares suppression state and requests outbound; no nested outbound drain remains (`EditEventFeedback.cpp:156–165`). Session MIDI still includes `delay(10)` (`:148–151`). Outbound later obtains current selected Track and builds dependent feedback inside its step (`FaderOutbound.cpp:285–315`); this is not evidence of entry-qualified stale-target cancellation.

Harness gaps:
- Entry suite no-ops **all five FSMs** (`test/test_note_edit_entry_boundary/off_path_deps.h:68–78`), explicitly excluding SELECT’s second selection (`:22–25`).
- Outbound request is recorded; processing/update are stubs (`:128–141`).
- Suite links real session/selection/coordinator but not `NoteEditButtonInput.cpp` (`test_note_edit_entry_boundary.cpp:111–159`): short/cycle/create tails lack that production-path coverage.
- Windowed preview harness has the same FSM omission, citing host narrowing errors (`test/test_windowed_preview_integration/off_path_deps.h:97–111`).
- “Returns to MIDI input” case actually asserts zero nested drains and one request, not observed service boundaries (`test_note_edit_entry_boundary.cpp:833–845`).
- Withheld-owner acceptance case is macro-gated (`:848–870,969–970`); ordinary baseline cases still exercise synchronous entry.

## CONFLICT / UNKNOWN / G0 verdict

**CONFLICT:** Treating asynchronous open/default/reopen acceptance as completed readiness leaves the verified callers consuming partial selection/FSM, emitting premature feedback, losing cycle/create/requested-state intent, or ending reopen suppression before completion. This conflicts with S7.5b.8 — NOTE_EDIT boundary readiness/retention requirements (`design.md:1483–1497`), not with a newly proposed policy.

**UNKNOWN:** integrated waiting/replacement/cancellation predicates for these action tails; safe payload retirement; real-FSM publication parity; source-recovery composition; unit/device timing and composed opportunity gaps.

**G0 — evidence verdict:** caller-tail inventory and real FSM dependencies are established. Evidence does **not** establish complete asynchronous entry readiness or a closed production-path transition oracle. Existing primitive/native results cannot cover omitted FSM/button tails. No approval or solution design conferred.
</answer>