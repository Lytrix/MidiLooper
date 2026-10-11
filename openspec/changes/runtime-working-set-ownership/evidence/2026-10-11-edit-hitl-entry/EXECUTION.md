# Host NOTE_EDIT entry repair

## Scope and oracle fixed before edits
User requested a direct edit-HITL entry repair on `feature/unified-rolling-playback-path` (HEAD `abaa3097`, pre-existing dirty tree preserved). Existing owner: `edit_minimal._ensure_note_edit_entered`, reused by stacked reselect. No firmware, stimulus, recording, clear, note mutation, upload or budget change.

Oracle: newest mode acknowledgement wins, including an exit after NOTE_EDIT and LOOP_EDIT after a legacy entry row. Already NOTE_EDIT sends nothing. Otherwise send one EDIT short press using the existing MIDI helper; confirm only the newest mode in the post-action suffix. Missing evidence fails closed, never retries a toggle. A fresh explicit LOOP_EDIT response permits the existing second attempt. Entry progress is flushed before the gesture. Tests inject serial rows and time, not a copied entry implementation.

Checks: `PYTHONPATH=scripts .venv/bin/python -m unittest scripts.test_note_edit_entry_ack scripts.test_note_edit_reselect_guard scripts.test_hitl_live_observation`; diff hygiene. C++ build/native not applicable to this host-only slice; no push/merge/PR.

## HITL recipe
- Reuse STIM-NE-RESELECT-STACKED-01 — stacked reselect, listed via stimulus catalog; no canonical edits.
- Diagnostic entry-only restriction: canonical managed runner with saved seed layout, stop immediately after production entry helper, no subsequent fader/note actions. Scratch wrapper is not a new acceptance stimulus.
- Existing note 38 short-press helper/control constants; no new marker/tier registration or RAM1 impact.
- Device oracle: exact target PLAYING+transport=1, fresh NOTE_EDIT acknowledgement after EDIT, final two verified Stop/drain cycles.
- Previous five-minute stall is not reproduced or attributed by this repair. Full composed selection qualification remains open.

## Baseline evidence
Saved aborted capture replay: collector startup/snapshot and entry fake MIDI send return promptly. Original capture has exact PLAYING reply at device 65.775 s but no EDIT.
Bounded unchanged-code entry-only probe `captures/edit-entry-probe-t8s4-02`: actual existing target 8/4 enters NOTE_EDIT on first press, no note actions afterward; canonical shutdown record to be checked at closeout. No reset/upload/record/clear. Probe01 refused before entry because standalone report lacks top-level serial-log path; completed two canonical drains.

## Completion
Completed host and bounded entry-only checks; full composed seed/content/selection qualification remains open.

## Additional reproduced pre-entry blocker
Probe03 has a fully fenced exact-target PLAYING response, but the guard refused before entry. The managed response wait checked its deadline before its final log read, dropping a complete reply arriving during the last poll sleep. Add a deterministic file/time fixture with completion exactly at deadline; fix the existing managed serial owner to read once before declaring timeout and cap sleep to remaining time. Deadline, nonce qualification and cleanup ownership unchanged; no new firmware or verdict policy. Shared entry regressions already reproduced four distinct failures before repair (7 tests, 4 failures), then focused29 passed. Broad edit tests:109 cases, one recursion error in unchanged legacy session-anchor verifier; investigate only for baseline attribution, not a new scope.

Probe04 also refused an initial status query. Preserve the existing `max(2s, phase_wait*4)` setup budget and use it for the initial read-only status observation; after the one start press, a missing individual 0.5s reply continues read-only polling inside that same existing post-start budget rather than ending early. Wrong/missing target never authorizes a toggle. Two new production guard fixtures pin delayed initial success and missing-post-press reply recovery with exactly one start press. This is a host observation budget repair, not a new musical timing limit.

## Final verification and closeout
- Entry regressions:7 tests/4 failures before shared-helper edit. Deadline fixture:1 failure before managed-owner edit. Guard fixtures:2 failures before budget edit. All10 new cases now pass.
- `PYTHONPATH=scripts .venv/bin/python -m unittest scripts.test_note_edit_entry_ack scripts.test_note_edit_reselect_guard scripts.test_hitl_live_observation scripts.test_hitl_command_forwarding scripts.test_managed_record_drain scripts.test_hitl_boot_settle scripts.test_hitl_record_row_contract`:102 tests, OK (4.266s). Includes the pre-existing uncommitted record-row repair unchanged.
- Broader `test_*edit*.py`:109 tests/1 error at `test_enter_fails_when_cycle_before_select`; unchanged `_find_edit_enter_index` / `_find_session_enter_anchor` mutual recursion is present in HEAD source and isolated rerun. Not caused by the edited helper; left outside this bounded repair.
- Device probe05: canonical runner `--scenarios note_edit_reselect_stacked --track-number 8 --loop-slot 4 --midi-channel 8 --no-boot-reset`, entry-only scratch wrapper, 90s managed bound. Saved layout reused only as probe input; no new seed/clear/record or fader/note mutation. Existing EDIT long-exit helper first establishes LOOP_EDIT, then production short-entry sends one press. Fresh fenced `#TRACK_STATUS … track=7,slot=3,state=PLAYING,transport=1` at device1343.951915s; long exit1344.722s; entry short press1345.228729s; NOTE_EDIT acknowledgement1345.686s. `shutdown-result.json`:complete/drains2/ok true; no latch. This is entry evidence, NOT full scenario PASS (wrapper intentionally interrupts before mutation/verification).
- Identity:git `abaa30975d2cd2650fbce6638ef9e6d3783ad5d7` plus dirty host repair; reused firmware SHA256 `6f176b90352435e54510606f2bc55496d3aed7569684712009f04af2d6650465`. No rebuild/upload/reset or SD-destructive action. Device left transport stopped/drained, NOTE_EDIT session active.
- `git diff --check` clean before final evidence edit; repeat at closeout. C++ native/build NOT RUN (host-only; no push/merge/PR). Direct local diff review; no independent-review claim, no owner/architecture/resource change or new decision entry. Runtime, scenario guide, deliverable and active task evidence updated; full composed checkbox deliberately remains unchecked.
- Original five-minute stall did not recur in entry-only probes and is not fully causally attributed. The demonstrated late-reply guard refusal and stale-ack/blind-toggle hazards are repaired. No wider timing, content, selection/reselect or long-loop acceptance inferred.
