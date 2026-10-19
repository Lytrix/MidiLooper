# I4 — UI/storage qualification

## Authorized scope

User requested continuation after I3b composed core closure. User explicitly authorizes scratch content only operator track8/slots4–5, creating new checkpoint revisions preserving Current workspace, and loading only checkpoints created for this test. No deletion, quarantine, rename, nuke, cleanup or clearing other slots. Owner can watch OLED and will report physical spinner/focus/overlay behavior. Existing installed default HEXd1b93b866e204c08573b28606c0676c67cc6e066133b6c507046cf7353931780 preferred; no upload yet authorized for this packet. Branch feature/unified-rolling-playback-path @26125ceb+preserved dirty work. Scope recorded in CURRENT_WORK before code. No Git delivery.

## Exact acceptance clauses

- save-status-display4.3: record→PLAYING, spinner completes/PERS,resultok before transport Stop; physical panel witness required. SAVE phase transitions do not prove pixels or every rotation.
- slot-selection-focus8.1–8.3: stopped NOTE_EDIT and playing NOTE_EDIT/LOOP_EDIT plus multi-slot focus-only; immediate display/UI focus, active switch at grid and edit commit on departure.8.4 independently restore both indices after deferred save/reboot;8.5 footer-extension revision and missing-Transport default-first-occupied revision cases.
- load-save-overlay-display-regression6.1 native gate/phase checks;6.2 overlay MIDI load+dirtyYes;6.3 dirty revision regression;6.4 overlay opened whilePLAYING→load and physical minimal-screen/nofreeze/crash. Completion needs correct current IDs/request/dispatch/result/LDSVexit, not exit0 or old trace.
- No full I4 closure from one partial run; implementation/deferred parent gates not inferred.

## Repository findings / reusable path

Read-only explorer report in chat (6d8be98e-4d5b-40d8-a495-5da86b4289c3), no device or edits by delegate. Existing legacy overlay/dirty runners exist but are absent from current registry, open their own SerialCaptureCollector and issue !REV_COMMIT. Managed broker permits only read-only commands and follower.write_line refuses; do NOT weaken broker or open second USB owner. Legacy dirty helper does not select an explicit slot, presses global Record twice and uses tempo120; do not execute unchanged. Legacy _find_midi_port string direction is incompatible with current boolean API. Reuse open_midi_session(serial_port=None), ExternalSerialFollowCollector, managed owner/DeviceSession, actions/observation and existing overlay gestures. No canonical stimulus edits or captured command execution.

A bounded registered UI/storage adapter is a concrete prerequisite. Preferred first leg is checkpoint-first MIDI Save row, then exact authorized scratch stimulus and identity-qualified overlay save/load. Root Save row0 uses beginOverlaySaveRowCommit and exits overlay; Set row confirms latest revision for its resolved Set. Before load, prove the Set belongs to the test checkpoint via existing revision-history navigation marker, never guess Set1/catalogindex0. Revisions affect whole workspace; preserve a pre-test checkpoint before scratch mutation. Do not use serial mutation passthrough/nuke/cleanup.

## Evidence limitations to preserve

- Fenced !TRACK_STATUS reports selected track's **active** slot, not UI selected slot. Fresh DISP identifies actual displaySlot; !LIB_OBSERVE does not report independent index arrays.
- Footer application logs stored active/selected and runtime playing. loadTransportSlotIndices normalizes stopped runtime active to selected; stored differing values do not prove differing live indices after stopped restore. Do not check8.4 from that diagnostic alone.
- Default revisionTransport case needs a revision without Transport and a known nonzero first occupied slot; current legacy drivers do not establish this safe test fixture.
- Mid-scenario reboot is not an existing managed API: startup restart_with_boot_reset is one-shot recovery and changes log/follower. Do not bypass ownership or pretend separate logs are one boot/session.
- Multi-slot hold/release lacks a composable helper; long slot press is clear, not a substitute.
- Direct progress/DFRAME/DMA/serial proves software liveness, not physical pixels. Owner observation needed.

## Prerequisite checks performed

- Branch/status verified; relevant authority, CURRENT_WORK/tasks/designs/specs, DEC topic search and context-map sections read. Existing dirty work preserved.
- Catalog listed with host_midi_hitl.py stimulus list; registered scenario list confirms overlay/dirty IDs absent. Teensy /dev/cu.usbmodem154944801 present; no incomplete-shutdown latch. No hardware action/reset/MIDI/save/load yet.
- `pio test -e native -f test_save_status_display -f test_slot_switch_edit_sessions -f test_set_revision_persistence -f test_slot_load_session`:96/96,exit0.
- `PYTHONPATH=scripts .venv/bin/python -m unittest test_revision_load_serial_verify test_revision_load_serial_debug test_revision_commit_save_serial_verify`:4/4,exit0.

## Execution contract before host implementation

Reuse managed capture and MIDI actions; a fresh stopped/current boot handoff and exact authorized targets must precede mutations. Refuse no collector/no managed owner, stale/missing overlay/row/commit evidence, unknown checkpoint identity or wrong load target. Bound all waits within managed360s unless a separately documented legitimately longer packet is required. Persist checkpoint identity before later scratch operations. Use production MIDI Save/Load, no arbitrary serial writes. Separate machine/pass/inconclusive/manual-unobserved per clause. Add simulated-clock/collector/port tests with actual production helper and hard negative controls before behavior edit; focused plus native/full relevant checks, one focused review after nontrivial implementation, canonical run with metadata/hash/current evidence/two shutdowndrains. Recipe/stimulus registration required before new HITL code; marker tier/RAM1 n.a. if host-only. No firmware observation expansion or unrelated bug repair without its own pinned scope.

## Host implementation and checks

Registered new STIM-I4-CHECKPOINT-01@v1 / i4_checkpoint preset, no existing canonical recipe modified. MIDI-only adapter reuses managed owner, ExternalSerialFollowCollector, ActionContext/buttons/await_markers and boot handoff. Fresh fenced TRACK_STATUS timestamp qualifies each gesture; explicit closed-overlay owner observation handles absent LDSV history. Missing/wrong/changed Save row refuses before mutation; commit oracle requires queue/dispatch/complete/exit and coherent uint16 Set/revision identity. No record/slot/load/cleanup or serial-mutation path. open_midi_session now unwinds partial acquisition.

Recipe recorded before code. Initial test run failed because module absent (not a behavioral RED claim). Initial13 tests passed; extended17 tests plus neighbours87 passed. One focused review requested three changes: documented panel flag, five-digit IDs, partial MIDI acquisition. Both code defects reproduced as two assertion REDs; repaired and retested. Final21 checkpoint tests /91 focused host checks pass. Full native3072total /3065passed /7skipped /0fail. Catalog and preset visible; diff check clean. No firmware build/upload or Git delivery; unrelated dirty work preserved. No second review/PASS endorsement claimed.

## Authorized checkpoint hardware attempt — setup blocked

Owner confirmed normal app screen / overlay closed / ready to watch. Canonical registered command in recipe, run `captures/i4-checkpoint-t8s4-01`, exit2 / SETUP_INCOMPLETE. Git26125ceb+dirty; metadata pins installed defaultHEXd1b93b866e204c08573b28606c0676c67cc6e066133b6c507046cf7353931780. Managed auto attach (no forced reset/upload). Fresh TRACK_STATUS track7/active-slot3/stateSTOPPED/transport0 before each gesture.

Device receipts, microseconds:
- 1350113961 stopped pre-entry; 1350735712 LDSV1.
- 1351138435 stopped pre-scroll; 1351682535 OVLYsel0/0.
- 1351948067 stopped pre-Save; 1352643344 OVLYconfirm0/0.
- 1352643376 PERSrev_request queued; 1352643388 LDSV0; 1352643406 rev_overlay_save queued_exit.
- 1352684755 SAVEpending. No rev_dispatch/rev_complete during bounded90s wait; no checkpoint identity proven.
- During shutdown: 1444678808 rev_blocked deferred_save_active; workspace resultsok1444793961 and1447660679, but revision remains unproven. FinalSAVEpending1447709899.

Owner physical observation: **“Opened on Save, closed, normal screen stayed responsive.”** This proves only this overlay interaction, not revision creation/spinner completion/PLAYING load smoke. No scratch record/clear/slot gesture/load/SD-destructive action. Canonical shutdown complete /drains2 /oktrue and latch absent; this reports workspace persistence idle, NOT checkpoint completion or empty revision queue. Failed artifacts preserved; do not blindly issue another Save or reset away the queued request.

## Concrete firmware admission blocker (read-only trace)

VERIFIED code: main `runDeferredSaveState` enters on `hasDeferredSaveWork()` plus queued capture chunks. `hasDeferredSaveWork` delegates to `hasPersistenceWorkPending`, which includes only Current workspace pending, PersistenceWorkQueue depth/writing and active work item — not revision commit/load. Save-row `beginOverlaySaveRowCommit` sets background flag then `requestCommitRevision` sets revisionCommit.pending; once overlay exits it cannot alone admit the existing scheduler. Separate `hasRevisionCommitWork` already exists. Scheduler dispatches revision only after workspace work is absent; the shutdown's workspace requests let it observe `rev_blocked` but do not prove a later revision turn.

Symbols: src/main.cpp `runDeferredSaveState`; include/StorageManagerStatusQueries.inl `hasDeferredSaveWork`/`hasRevisionCommitWork`; include/StorageManagerInternal.h `hasPersistenceWorkPending`; src/StorageManager/Overlay.cpp `beginOverlaySaveRowCommit`; src/StorageManager/DeferredSaveScheduler.cpp `processDeferredSaveState`. No firmware edit, new marker, budget change or serial-mutation workaround. Exact stopped revision-only production-owner regression and existing-owner admission correction require a separately pinned/authorized firmware slice, then default build/RAM1 and authorized upload/device rerun. Do not indiscriminately widen the workspace-only query: its other callers use narrower semantics.

## Closeout / remaining qualification

load-save-overlay-display-regression6.1 — native gate/phase verification is checked from this full-native run. All device load/dirty/PLAYING smoke clauses and save-status-display4.3 — save during playback, slot-selection-focus8.1–8.5 — focus/restore remain open. No whole-I4 PASS. Host prerequisite delivered locally; checkpoint-first hardware is blocked by revision-only persistence admission. Existing managed teardown/read-only broker kept unchanged. Closeout template applied: runtime/tasks/guide/delivery updated, no new architecture decision adopted (existing owner omission only), no decision-log append required; exclusions remain owner-imposed rather than new architecture policy.

Final checks:21checkpointtests pass after docs; `git diff --check` clean; `openspec validate load-save-overlay-display-regression --strict` valid. CLI informational archive blockers remain: revision-load MODIFIED header not present in target, set-browser-overlay target main spec absent. No archive/sync requested or attempted; no archive-readiness claim. Final runtime Now-implementing handoff verified in place; incomplete-shutdown latch absent.

## Owner-approved firmware follow-up (local only)

Owner subsequently authorized the narrow firmware correction. Admission and its concrete immediate header-patch truncation blocker are now repaired locally; real commit→created revision load/streamed-footer/content-preservation/denial fixtures pass, fullnative3070pass7skip/defaultcaptureSUCCESSpadding904>=840/staticreviewAPPROVE. CandidateHEX8ed5a9dc is **NOT uploaded**, installedd1b93b86/pendingdevice request remain untouched. No new device verdict/wholeI4PASS. [I4 — admission repair execution](ADMISSION-FIX.md) and recipe carry exact image/gates; previous failure remains historical evidence, no silent PASS conversion.
