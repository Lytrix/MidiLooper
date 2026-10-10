# S7.5b.8b — bounded entry, selection and focus preparation: readiness trace

## Scope traced

Owner authorized S7.5b.8b implementation on `feature/unified-rolling-playback-path`, including scope update, execution packet, native tests and default capture build; no upload. S7.5b.8c — feedback pipeline and S7.5b.8d — hardware timing remain separate. Baseline HEAD: `067be7f3d7bf0500a49da589c02d78c0904f9cda`, with substantial pre-existing dirty firmware/tooling/authority work preserved.

**Status: BLOCKED before production mutation at G2 — implementation contract.** This is a discovery record, not a completed execution packet or implementation approval. [The fixture packet](EXECUTION.md#unit--continuation--lifetime-contract-s75b8b-must-close-before-coding) explicitly leaves unit quanta and continuation/lifetime closure to this slice. The parent [design](../../design.md#s75b8-note-edit-midi-boundaries) describes requirements and owners but says it does not select the exact lifecycle model. No firmware, test source or bound classification changed here.

## Current execution path (VERIFIED)

1. `MidiButtonActions::handleCycleEditMode` reaches `ControlSurfaceManager::cycleEditSession`, then `EditManager::cycleEditSession` / `sendEditSessionChange` / `reopenNoteEditSession` / `openNoteEditSession`.
2. `openNoteEditSession` calls `Loop::assignMissingNoteIdsInCommittedCapturePasses` **before** the current-root/miss split. That helper visits the active Record and overdub committed chunk lists; moving only rematerialization off the callback leaves this scan on input.
3. Current-root open avoids rematerialization and starts an empty overlay. Miss open calls `rebuildVisualCacheFromPasses`, `rematerializeEditView`, ID assignment, `copyEffectiveCommittedEvents`, `buildFromSessionStore` and baseline sealing synchronously. The visual-cache rebuild exits early in `isPlaybackSessionExitPublicationGap`, but that does not establish a resumable source-ready continuation for the remaining open work.
4. `enterDefaultNoteEditSessionState` calls bracket/nearest selection, then `applySelectNav` and `syncNoteEditSessionStateToUi`. These fetch selectable vectors by value and scan their contents.
5. `ensureNoteEditDisplayProjectionCachesBuilt` can resolve a **full-content** snapshot into a temporary vector, copy it into the committed cache, union Capture previews with nested identity scans, project paint, and filter selection. Cache-hit callers still return/copy selectable vectors by value. It is not a bounded owner step merely because it is cached.
6. `syncNoteEditSessionStateToUi` invokes `EditSelectNoteState::onEnter` when entering Select. The real FSM calls `track.editAwareMidiEvents().size()` and **selects closest again**. The fixture declares these FSM TUs as stubs; therefore its passing baseline does not verify this nested production work.
7. `handleSessionOpenedEvent` sends session MIDI (`sendEditSessionMidi` includes `delay(10)`), prepares the existing fader request and calls `drainFaderOutboundUntilIdle` inside the same callback. Feedback accounting is pinned, but actual send work is a declared fixture boundary and belongs to S7.5b.8c.

Function anchors: `src/EditManager/NoteEditSessionLifecycle.cpp`, `NoteEditSelection.cpp`, `NoteEditDisplayProjection.cpp`, `EditNoteStateCoordinator.cpp`, `src/EditStates/EditSelectNoteState.cpp`, `src/Loop/LoopCapture.cpp`, `src/Loop/LoopVisualCache.cpp`, `src/ControlSurface/EditEventFeedback.cpp`.

## Ownership/lifetime map (VERIFIED)

- `EditManager` owns session, selection/focus, undo and projection caches. `Loop` owns committed source publication. Track owns playback preparation and the ledger. No ownership transfer is proposed.
- Open currently publishes `editSession.active` before selection completes; reopen clears old session containers immediately. There is no separate pending-entry request/worker/complete-publication seam in the traced lifecycle.
- Selected-track change calls `beforeSelectedTrackChange` (depart/commit and preview cancellation), then `onTrackChanged` / `reenterEditSessionForFocusChange`; slot change has analogous before/after hooks. Refocus immediately reopens and emits feedback for an active session.
- Workspace reload calls `rematerializeNoteEditSessionAfterWorkspaceReload` / reopen. Clear calls `revertNoteEditSessionForLoopClear` and returns to Loop edit. Exit commits existing edit state through the existing pass/persistence/undo owners; those semantics must survive a pending entry and cannot be replaced by simply clearing every request.
- Selection identity uses selected track plus the passed Track's selected Loop. The frozen late-reference fixture demonstrates that an obsolete Track reference can produce a mixed identity.

## Deferred-work map (VERIFIED)

`processDeferredNoteEditDisplayRefresh` is an existing settled-preview continuation (80 ms idle), not an entry/content/selection worker. `main.cpp` calls `ControlSurfaceManager::update` in its prefix; the existing outbound coordinator steps there. `LoopContentResolution::resolveDisplayNotes` exposes full-content stateless queries; the inspected API also has range helpers for other work, but no bounded entry/focus completion contract is specified by this slice. Do not assume those helpers establish a nested bound for the existing selection consumers.

## State-transition oracle and existing validation

The [frozen fixture oracle](EXECUTION.md#oracle-frozen-by-the-fixtures) remains unchanged. The pending-entry API, service-grain limit and observation after each actual owner grain are **not yet specified/linked**. Native fixtures currently freeze synchronous baseline behavior and three independent boundary failures; their names containing “between units” do not prove a production continuation exists.

Commands performed in this session:

```
pio test -e native -f test_note_edit_entry_boundary
PLATFORMIO_BUILD_FLAGS='-DS75B8_ENTRY_BOUNDARY_ORACLE' \
  pio test -e native -f test_note_edit_entry_boundary
```

- Unflagged: exit **0**, **12 succeeded**, suite PASSED.
- Flagged: exit **1**, **12 succeeded / 3 failed**, suite **ERRORED**, `Program received signal SIGQUIT (Quit: 3)`. PlatformIO summary reports 16 cases (including the harness error), not a clean 15-case test completion.
- `test_entry_returns_to_midi_input_between_admitted_units`: `Expected 0 Was 1`, nested send-pipeline drain call.
- `test_entry_keeps_the_accepted_gesture_pending_when_the_owner_turn_is_withheld`: withheld owner turn still publishes the session.
- `test_entry_refuses_a_late_completion_for_a_superseded_track_reference`: `Expected 1 Was 0`, mixed track/slot identity.
- Evidence logs: [`s75b8-baseline.log`](s75b8-baseline.log), [`s75b8-boundary.log`](s75b8-boundary.log). No full-native run, capture build, target layout measurement, device interaction, upload or SD operation in this readiness-only continuation.

## Resource/timing consequences

**VERIFIED:** current projection containers and miss rematerialization can scale with full loop content; a private duplicate of those vectors would add another whole-content working representation. The architectural constraints are [role-owned window-sized working sets](../../../../../docs/Authority/ARCHITECTURE_RULES.md#runtime-working-sets-are-role-owned-and-memory-scales-with-active-roles), DEC-055 — overlay delta on opened LoopSnapshot, and DEC-043 — execution bound class. A loop that checks time between calls to these helpers cannot bound those calls.

**UNKNOWN:** exact admitted work quantum, source pin/borrow lifetime across each grain, bounded ID repair, how to preserve selection order/index/tie semantics without copying a whole-loop inventory, atomic handoff to existing display/selection consumers, active-transport nested duration, and linked RAM1/PSRAM consequences. No numerical budget or memory headroom is claimed.

## Historical decision review

Searched `docs/DECISION_LOG.md` for NOTE_EDIT/focus/entry and read DEC-055 — overlay delta on opened LoopSnapshot and DEC-043 — execution bound class. Reuse decision: **YES**, existing EditManager/ControlSurface/Loop/Track owners; no new coordinator. DEC-055 forbids solving edit/undo by adding a second full-loop representation; DEC-043 says local budgets do not bound indivisible work. Active OpenSpec's boundary requirement explicitly forbids polls around scans/projections/sorts/allocations/send bursts. No decision is superseded or new architecture choice ratified here; no DEC append is warranted for this discovery record.

## Hidden dependencies / readiness closure

Before firmware mutation, the execution packet must specify one coherent existing-owner contract for:

1. Bounded content/NoteId inspection and current/stale/rootless source admission; missing source retains the accepted gesture and audible source, not a synchronous legacy flatten.
2. Incremental selection/focus preparation including **nested** projection, copies, UI synchronization and real FSM entry; preserve existing bracket/nearest ordering and selected identity.
3. Pending→complete publication with source/session/track/slot/NoteId qualification, replacement/revalidation across refocus, source mutation/undo, exit, clear and reload; source release and private storage residency must be explicit.
4. Main-loop stepping returning to input between actual units and a controllable real-worker fixture. Separate feedback preparation/send still belongs to S7.5b.8c, but entry completion must not silently invoke its blocking drain.
5. Structural quanta and resource accounting without an invented timing threshold, full-loop private clone or a second projection authority.

**Preferred direction (not an approved implementation contract):** extend EditManager with bounded metadata acceptance and generation-qualified private selection progress over the existing source owner; publish only a complete qualified focus, and use the existing cooperative surface owner for feedback. Confidence high in owner reuse, unresolved in the exact source/selection-unit and lifetime contract. Do not treat “defer `openNoteEditSession`” as this contract.

## Verdict / closeout

**G0 — evidence: traced, with the above VERIFIED/UNKNOWN distinctions. G1/G2 — architecture/implementation contract: not closed; firmware BLOCKED.** No claim that a solution is impossible, no reassessment decision, no new scope exclusion and no weakened oracle. S7.5b.8b stays unchecked; S7.5b.8c/d and the parent timing gates stay open. Owner authorization remains recorded in CURRENT_WORK; it does not itself establish readiness. Resume by closing the source/selection/lifetime unit contract, then implement and execute the task's full native/build gates.

Documentation checks performed: `openspec validate runtime-working-set-ownership --strict` valid; `git diff --check` clean. Runtime scope/status and task/design handoff updated; no deliverable shipped.
