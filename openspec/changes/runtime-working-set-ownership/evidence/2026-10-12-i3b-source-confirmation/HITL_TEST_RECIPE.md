# I3b — current focus geometry observation

Owner explicitly authorized capture-only marker extension, default-env build/upload and registered rerun on8/4 after run01 proved current IDs but lacked current-window DNTE geometry. No change to existing stimulus, controls, selection, bounds, runtime state or geometry behavior.

| Field | Recipe |
|---|---|
| Acceptance | Current F1 selection binds exact focused NoteId plus visible current pitch/start/end; qualify Move→empty→both stack notes without stale geometry or index-ID assumptions. |
| Reused stimulus | STIM-NE-RESELECT-STACKED-01@v1 + STIM-BASE-2-FIXTURE-01@v1, catalog listed this turn; canonical preset unchanged. |
| New stimulus | None. |
| Gestures | Existing note_edit_reselect_stacked/actions helpers unchanged; only track8/slot4 target. |
| Marker | Existing direct `NOTE_EDIT focus rebuild: noteId=… presence=… rowProjectsToStore=…` from EditManager::rebuildNoteEditFocusForDisplayNote, append current-state pitch/start/end. SESSION_CAPTURE-only, same call frequency. Direct Logger::log row has logger timestamp, not a #CAP ring family/Tier-A marker. Missing row refuses proof. |
| Tier contract | No #CAP/Tier-A addition; prefix lists/tests unchanged. Existing direct row deliberately not ring-only. Marker-contract audit required; no new tag falsely claimed Tier-A. |
| RAM1 | Prior padding968/floor840, only128B margin; use flash-placed format and gated scalar reads; build guard must pass without rebase. No new buffer, owner, allocation or PSRAM/LoopPool state. |
| Regression | Host verifier RED on valid current geometry marker before parser change; wrong pitch/start/end/presence/projection/current-ID/conflicting/stale marker controls refuse. Native suites retain actual focus/current-state/selection behavior; native Logger::log silences payload, so direct marker delivery requires target build/HITL, not inferred native text proof. |
| Entry point | host_midi_hitl.py run --preset note_edit_reselect_stacked --track8 --loop-slot4 --midi-channel8 --clock-source host --tempo-bpm300 --build-env teensy41-capture-serial --build-upload --boot-reset --capture-boot-wait30 --out-dir captures/i3b-focus-geometry-t8s4-01 (actual CLI uses separated option/value). Managed360s bound unchanged. |
| Verdict | Exact1536tick qualified8pair seed/current save/drain, target7/3PLAYINGtransport1, intended Move392→200 with sourceNoteId/pitch67, appliedempty48, both distinct current IDs at expected pitches/starts, complete/drains2/ok/no latch. PASS_CORE closes composed core only; optional UNMEASURED never closes timing/retention/parents. |
| Evidence | New metadata hash/Git+dirty, actual marker trace, report/snapshot, native/build/RAM1/strict/marker checks, canonical shutdown receipt. Failed run01 preserved. |
