## Status and boundaries

The reported reboot undo/redo defect is **owner/manual device-accepted** after the replacement build: [session174940 acceptance](evidence/DEVICE-174940.md). Scoped implementation/native/build/review/manual-device tasks are complete. No automated hardware inventory/timing campaign or universal qualification is claimed; no archive/commit requested. Historical failures and repair rationale remain in the linked evidence.

Continuation resolves the fixture identity/readiness errors and implements approved same-inventory source reuse. Full native is green (3636 cases,3629 pass,7 existing skips); after three further test-only display/slot/save cases the final reboot suite is30/30 green. Counts, review scope, clean-build and device limitations are in [continuation evidence](evidence/CONTINUATION.md). No failure was skipped/reclassified or assertion weakened; device gates remain open.

Display repair and loaded-slot redo attribution are separate slices. On continuation, the owner explicitly approved the same-inventory descriptor-reuse packet in design; section4 implements only that contract. DEC-205's admission release, new roles/refcount model and unrelated architecture remain forbidden.

## 1. Production saved-slot fixtures

Owner: native fixtures exercising real StorageLoopIo/Loop adoption, TrackUndo and display/playback owners. Precondition: baseline `7c3f69ff` or explicitly recorded later tree. Postcondition: a literal-content oracle linked through production entry points, including staging destruction. Preserve firmware behavior and user data. Boundary: independently buildable fixtures/support only; no production success stubs.

- [x] 1.1 Implement a host saved-slot fixture using production serialization, deserialization/apply and undo derivation, with real owners linked for all asserted legs. Verify original owners/staging are destroyed and the first playback window emits all literal expected record/overdub events before any undo; cover passes-only and accepted-root bodies.
- [x] 1.2 Add the same-window display regression: load an 8-bar slot, render record+overdub, global undo, observe the ordinary cache refresh, withhold snapshot completion and complete normal display resolve/compose. Verify the baseline fails on the exact surviving DisplayNotes/key rather than on setup; add redo and current-empty viewport cases.
- [x] 1.3 Add loaded-slot repeated undo/redo playback acceptance with two overdubs and eight cycles, advancing clock and real worker/adoption turns. Verify exact event tuples each cycle, original pass payloads/geometry, chunk reference/lifecycle/token transitions and an unrelated sounding slot. Record the first bad boundary or non-reproduction, not an assumed redo cause.
- [x] 1.4 Add fresh/restored Record start/stop and successor-overdub safety controls, preserving explicit replacement Record semantics. Verify no source-maintenance call erases retained content and all expected committed events survive their actual operation lifecycle.
- [x] 1.5 Record commands, fixture linkage, counts, red assertions and bounded-drain diagnostics in `evidence/`. Verify this packet changes no production behavior and states that redo production readiness remains blocked; report any newly exposed contract conflict before generating repair tasks. (Attribution completed; subsequent production repair was separately approved under section4.)

## 2. Qualified display replacement

Owner: DisplayManager draw-source handoff and Loop cache qualification. Preconditions: display RED from section 1; reviewed contract in design/REVIEW. Postcondition: correct next completed frame without snapshot wait when current covered content exists, including empty. Preserve Record/overdub gap behavior, keys, overlays, admission and atomic compose. Non-goals: playback retention, storage format, unrelated display cleanup. Boundary: display regression+minimal repair on working branch.

- [x] 2.1 Expose/reuse Loop's revision-and-window coverage qualification without requiring nonempty notes or adding resident state. Verify current empty, stale empty, partial coverage, changed generation and out-of-window cases independently; query performs no MIDI content gather.
- [x] 2.2 Use the current qualified source at the same-window publication-gap boundary through existing compose/swap/key bookkeeping. Verify the original display RED becomes green, redo restores notes once, qualified empty clears notes and unavailable content retains the coherent fallback. **Reopened/repaired under section5:** the former fixture completed publication before checking redo; the new oracle verifies every painted edge with publication continuously pending. Device retest remains open.
- [x] 2.3 Add deterministic compose allocation-denial/recovery, pending-frame invalidation, 24-bar scrolled viewport, track/slot switch, clear/load replacement, root completion, transport/clock transition and save-in-flight cases. Verify no stale-key adoption, no cross-slot content, no duplicate overlay, no save drain/reset and unchanged Record/successor handoff controls.
- [x] 2.4 Obtain one focused independent implementation review per `docs/Agents/reviewer.md`. Verify concrete findings against fixtures and record retest evidence; do not count the planning self-review as this review.

## 3. Verification and closeout

- [x] 3.1 Run focused display, runtime-working-set, storage-I/O and continuous-capture native suites, then full `pio test -e native`. Verify required cases are present; record counts, skips, failures and whether the still-unrepaired redo acceptance is RED. No all-green claim from excluded/missing acceptance. (Continuation repairs the named redo boundary; original REDs and current green evidence retained separately.)
- [x] 3.2 Run `pio run -e teensy41-capture-serial`; verify successful build, RAM1 variables/ITCM/bank headroom and retained HEX SHA-256 against the recorded baseline. Do not upload automatically.
- [x] 3.3 Device preparation/authorization boundary reconciled: traps read, stimuli listed and recipe filled; **agent upload/test authorization not applicable**, because the owner independently uploaded and performed the manual check. No agent device check, flash, Record/clear or physical SD operation occurred. This completion is not authorization for future agent flashing or a canonical HITL-run claim.
- [x] 3.4 Record boot, displayed-content and MIDI-output evidence from the owner-run capture and confirmation. Session174940 shows repeated post-reboot clean cache/paint changes without transport restart; playback remains functional. Scoped manual device acceptance closes the reported defect. Aggregate MIDI counters are not promoted to per-slot inventory or numeric timing proof; see acceptance limits.
- [x] 3.5 Update PROJECT_STATE, CURRENT_WORK, applicable durable guide and tasks with actual fixture/native/build/device status. Verify no ship/archive/checkoff claim exceeds the evidence; update DELIVERABLE_TRACKING only for actually delivered behavior.

## 4. Same-inventory retained-source reuse

Owner, preconditions, outcomes, invariants, timing scope and commit boundary: [approved packet](design.md#execution-packet--same-inventory-source-descriptor-reuse-owner-approved). This is not permission to reconnect physical retirement to admission.

- [x] 4.1 Add exact refcount/alias/role/index invariants and reader/capture/finalization/inventory mismatch negatives around the corrected loaded-slot fixtures. Verify the original rootless/rooted playback acceptance remains discriminating on the pre-reuse implementation and the Record controls pass with consistent slot identities/readiness.
- [x] 4.2 Share the existing pure retirement predicate and reuse only quiescent older Retiring descriptors with identical ordered pin/chunk inventory, preserving their existing ownership. Verify eight rooted/rootless undo/redo cycles serve exact expected content with two roles, stable refs/alias and unchanged negative protections; no release call or new field.
- [x] 4.3 Obtain one focused independent source-lifecycle review and resolve/retest concrete findings. Verify the approval scope distinguishes this new packet from the prior display review.
- [x] 4.4 Run the packet's focused/full native and firmware build gates; record counts, RAM1/bank/HEX and any remaining device/matrix gaps. Verify no checkoff/flash/archive exceeds evidence and refresh runtime/guide/task closeout.

## 5. Repeated display edges during pending publication

Contract and transition oracle: [device evidence/execution packet](evidence/DEVICE-171433.md). Existing Loop refresh only; no playback or lifetime model change.

- [x] 5.1 Add repeated painted-note acceptance without any snapshot completion, plus ordinary-idle/private-source/refusal controls. Verify the current code fails at the second gesture's redo frame, not setup.
- [x] 5.2 Permit only explicit closed-committed pass-state refresh through one consistent local gap predicate; preserve ordinary/private Capture holds. Verify all gesture/frame expectations and publisher/payload invariants, with no new state or source-retention edit.
- [x] 5.3 Obtain focused independent review, fix/retest concrete findings, and run focused/full native plus clean firmware/RAM1/hash gates. Verify actual case presence/counts and preserve device acceptance as open until retested.
- [x] 5.4 Update runtime/guide/decision/evidence/task records with owner-reported upload/playback success, failed device display acceptance and the exact unflashed replacement image/check results.
