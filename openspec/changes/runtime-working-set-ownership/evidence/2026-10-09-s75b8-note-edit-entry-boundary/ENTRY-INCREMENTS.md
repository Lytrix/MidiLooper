# S7.5b.8b — bounded entry/focus preparation: local boundary increments

## G2 execution packet — scope and dependency closure

Owner authorization: CURRENT_WORK § Now implementing, S7.5b.8b. Existing contract: [boundary design](../../design.md#s75b8-note-edit-midi-boundaries), [fixture oracle](EXECUTION.md#oracle-frozen-by-the-fixtures). Baseline HEAD `067be7f3d7bf0500a49da589c02d78c0904f9cda`, working branch `feature/unified-rolling-playback-path`; substantial unrelated dirty work preserved.

This independently verifiable increment changes only two already-frozen boundary predicates, not the complete resumable entry pipeline:

1. `ControlSurfaceManager::handleSessionOpenedEvent` requests existing outbound work, then returns rather than calling `drainFaderOutboundUntilIdle`. The request quantum is one SessionOpen request with its existing fixed PlanFlags. `ControlSurfaceManager::update` already calls `processFaderOutbound`; no new scheduler, send order or echo/quiet policy. This removes nested polling only: session-MIDI preparation/send and the synchronous content/selection helpers are NOT classified bounded by this increment.
2. `EditManager::reopenNoteEditSession` first compares the supplied Track address with `trackManager.getSelectedTrack()`. A departed reference is rejected before container clearing, cache invalidation, content read or selection mutation. The qualification quantum is one selected-owner address comparison. No pending-entry lifetime, resource lease or representation changes.

Primary firmware files: `src/ControlSurface/EditEventFeedback.cpp`, `src/EditManager/NoteEditSessionLifecycle.cpp`. Tests: existing `test_note_edit_entry_boundary`, linked production action/session/edit owner with documented feedback/FSM boundaries. No new marker, stimulus or instrumentation. These fixes require no absent pipeline primitive.

## Transition oracle and outcomes

- Missing yield: commit/prewarm long loop → real edit button → SessionOpened → one SessionOpen outbound request → callback returns. `nestedDrainCalls == 0`, `requestCalls == 1`, exact normative plan unchanged. The existing outbound owner, not a nested loop, advances sends later. Its physical latency remains S7.5b.8c/d evidence.
- Superseded reference: open Track 0/slot 0 → real selected-track change to Track 1/slot 1 → late `reopenNoteEditSession(departed)` → rejected no-op. Track 1/slot 1 identity, selected NoteId and content remain authoritative; no old-target content or cache invalidation is introduced.
- Withheld grant remains separate: the synchronous open still publishes when no edit-owner grant is issued. That fixture must remain RED, not be gated out or reinterpreted. These two fixes do not claim the pending pipeline exists.
- Preserve all 12 baseline cases: exact accepted MIDI inventory, ledger closures, track/slot identity, selection, feedback accounting, exit/reopen, mutation, clear/load and competing playback.

## Pre-fix evidence / verification recipe

The preceding readiness run reproduced three independent failures: nested drain (`Expected 0 Was 1`), withheld grant, superseded identity (`Expected 1 Was 0`); flagged suite also ERRORED on SIGQUIT. No missing symbol is credited as behavioral RED.

```
pio test -e native -f test_note_edit_entry_boundary
PLATFORMIO_BUILD_FLAGS='-DS75B8_ENTRY_BOUNDARY_ORACLE' pio test -e native -f test_note_edit_entry_boundary
pio test -e native
pio run -e teensy41-capture-serial
git diff --check
openspec validate runtime-working-set-ownership --strict
```

Expected flagged outcome for this increment: missing-yield and superseded-reference cases PASS; withheld-grant case remains the one named failure. Capture-build RAM1 must be recorded, with no budget rebase. No upload or device/SD action authorized. These are local call-site/predicate fixes with no new lifecycle or resource model; focused direct patch inspection and independent mutation controls verify this increment. The future non-trivial resumable lifecycle still requires its independent lifecycle review.

## Readiness work in parallel

Read-only selection investigation establishes exact inventory-order ties, nonzero-origin bracket conversion, split-note/index semantics, nested FSM reselection and navigation-clamp chunk/visual scans. A contract draft is being assessed once against actual primitive bounds and source recovery. It is not implementation authority until corrected; no full-pipeline claim or checkoff follows from these local fixes.

## Completion record

**Local increments verified; parent task incomplete.** Both repaired cases are now unconditional ordinary-native regressions; all 12 original baseline assertions remain unchanged. Late-reference coverage additionally checks unchanged primary NoteId, selected index, preview/playback revisions and exactly two prior feedback requests. Before firmware, historical decision review reused DEC-043 — execution bound class and DEC-055 — overlay delta; no new owner, lifecycle/undo shape, timing policy or persistent representation, so no reassessment/DEC append for these local fixes.

- `pio test -e native -f test_note_edit_entry_boundary`: promoted suite **14/14 PASS**, exit 0 (also present in final full native with stronger no-op assertions).
- `PLATFORMIO_BUILD_FLAGS='-DS75B8_ENTRY_BOUNDARY_ORACLE' pio test -e native -f test_note_edit_entry_boundary`: **14 succeeded / 1 withheld-owner failure**, exit 1, plus **SIGHUP / suite ERRORED**. This is explicitly not a green boundary suite or a completed pending-entry gate.
- Drain mutation: restoring only `drainFaderOutboundUntilIdle` makes only the yield assertion RED (`Expected 0 Was 1`), 13 others pass, exit 1 / SIGHUP harness error. Restored file tag exactly `A5A6`.
- Identity mutation: removing only the selected-Track guard makes only mixed-slot publication RED (`Expected 1 Was 0`), 13 others pass, exit 1 / SIGHUP harness error. Restored file tag exactly `40FC`.
- `pio test -e native`: **2,906 cases / 2,899 succeeded / 7 skipped / 0 failed**, exit 0, 112 PASSED suites. Both repaired regression cases included. No test exception added to make native green.
- `pio run -e teensy41-capture-serial`: **SUCCESS**, RAM1 variables **96,192**, code **390,360**, padding **2,856**, locals **34,880**; same RAM1 line as prior pin, floor 840 unchanged. RAM2 variables **162,912**; EXTRAM static **9,312**. No runtime high-water or physical timing claim.
- No upload, HITL, device reset, SD operation, canonical stimulus change, new marker or instrumentation.

Logs retained beside this packet: `increment-baseline.log`, `increment-boundary.log`, `promoted.log`, `mutation-drain.log`, `mutation-identity.log`, `full-native.log`, `capture-build.log`. Strict spec/diff checks recorded at closeout.

## Full-pipeline draft assessment — REVISE, not implementation authority

One plan-author draft and one independent plan-assessor pass were performed; no agent debate/review loop. The draft's general owner reuse is viable, but five concrete corrections remain before its full-pipeline G2 packet is complete:

1. Budgeted chunk copying does **not** bound its nested `sortSessionEventsByTick` or `applyNoteEditPassSequence` (`LoopVisualCache.cpp` fallback tail / `LoopMaterialization.cpp`). Replace those with bounded owner primitives; do not defer a whole helper or add a full-loop scratch clone.
2. Active pass-list enumeration currently allocates/sorts pointer lists. Pin the exact producer generation and safe immutable reference/list ownership across units, with bounded discard/retry on mutation and a production recovery producer for missing/stale roots.
3. Snapshot output order is bar/pitch/page/reference with `(NoteId,startTick)` dedup; a first-bar shortcut is not yet proven equivalent for all split/wrap content. Specify emitted-index and dedup semantics and safe source-qualified borrow/reacquisition; no dangling page pointer after source replacement.
4. Reopen/cancel retirement must bound shared store chunk release, undo payloads, edit rows/events and maps. Existing `clear()`/`detachChunksTo` is not an admitted fixed unit.
5. Every immediate-use caller (`enterEditMode`, `cycleNoteEditType`, create-note and short-press entry) needs an explicit retained tail with its arguments and cancellation result. Freeze exact pending/completed rootless and current-root playback-event/ledger assertions, not only store or selection parity.

The proposed empty-delta-only shortcut is unsafe on the existing stale-root reader: `ensurePlaybackMergedMidiEventsBuilt` consumes `sessionMidiEvents()` directly on that branch. Do not weaken the original rootless full-store assertions or move reader migration from windowed 3.15d into this task silently. The bounded stale-source producer/selection/cleanup and caller contract remain the next work within S7.5b.8b, whose checkbox stays open. S7.5b.8c — feedback pipeline, S7.5b.8d — hardware timing and parent continuity remain open.

Closeout checks: `git diff --check` clean; `openspec validate runtime-working-set-ownership --strict` valid. Runtime status/scope, task/design handoff, deliverable status and current fader guide updated. No overlap/ledger/playback-order logic changed. No new architectural decision/exclusion or compatibility owner was introduced; existing task scope retained. Session closeout preserves all unchecked full-pipeline and device gates.
