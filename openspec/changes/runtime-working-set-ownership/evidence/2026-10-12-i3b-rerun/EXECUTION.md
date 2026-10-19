# I3b — seeded-edit HITL rerun

## Scope and frozen acceptance

User confirmed I3b (initially called I2b) and authorized the canonical HITL to clear/re-record only operator track 8 / slot 4 using the installed firmware. No upload, firmware/host/stimulus edits, broader SD operation or Git delivery. Preserve existing dirty work on feature/unified-rolling-playback-path @ 26125cebb08980342ba56ed2b77e5397783ac8e5.

Reuse STIM-NE-RESELECT-STACKED-01@v1, the existing two-bar EDIT_RECORD_FIXTURE seed and canonical managed entry point. Host clock / 300 BPM; fresh managed boot, unchanged 360 s run bound and existing gesture/observation deadlines. Require exact 1536-tick qualified committed seed/current persistence/Record drain, exact target PLAYING, coherent current identity/geometry and intended Move → applied empty deselect → both stacked reselects, final canonical Stop/drain twice and no incomplete latch. PASS_CORE alone does not qualify unmeasured optional feedback/retention/timing. Do not check broader entry/continuity parents from this short leg.

## Preconditions

- Authority/status/scope and active task read; device recipe reused, no new marker/test/stimulus.
- Stimulus catalog listed; Teensy serial /dev/cu.usbmodem154944801 and MIDI input/output Teensy MIDI present.
- No competing capture_session.py process or incomplete-shutdown latch before run.
- Offline canonical verify-only replay of run15: base PASS, edit FAIL (selected_note_moved, deselect_empty_step, stacked_reselect_after_deselect, stacked_note_identity_unproven). Original artifacts unchanged; scratch replay is not hardware acceptance.
- Previous managed metadata pins installed title image SHA-256 7678b719f2c26c21942baff617ff7780a1f3f5d71fd75dc3b3ba5d78185e463b; rerun metadata must retain the existing HEX pin, with no rebuild/flash inferred.

## Command

`.venv/bin/python scripts/host_midi_hitl.py run --preset note_edit_reselect_stacked --track 8 --loop-slot 4 --midi-channel 8 --clock-source host --tempo-bpm 300 --boot-reset --capture-boot-wait 30 --out-dir captures/i3b-closeout-rerun-t8s4-01`

## Result

**FAIL / I3b remains open.** One authoritative hardware attempt, exit1; no blind retry or code repair.

- Run: `captures/i3b-closeout-rerun-t8s4-01`, session `session_20261012_141106.log`; metadata pins Git `26125cebb08980342ba56ed2b77e5397783ac8e5` + dirty and default capture HEX `7678b719f2c26c21942baff617ff7780a1f3f5d71fd75dc3b3ba5d78185e463b`. Installed image reused, no build/upload.
- Fresh reset acknowledged, `DIAG,boot_reset,crash=0`, current `boot_commit` at11.538545s. Boot banner rows include duplicate/earlier attach material; do not interpret their multiplicity as multiple new failures.
- Seed baseline `overall_ok=true`, PASS: musical channel8,8On/8Off/0unmatched, committed1536ticks (raw1544), first-note latency0clocks, current persistence resultok, Record drain completed. Fixture ticks `8,200,392,584,872,1064,1264,1352`; irregular fixture grid diagnostics are not a regular-base grid failure. Requested exact committed two-bar geometry is met, not a long-loop qualification.
- Exact live target reply: `#TRACK_STATUS,us=62494562,track=7,slot=3,state=PLAYING,transport=1`. NOTE_EDIT entered on first attempt.
- F1 command ch16 value-3963 reaches source tick392/pitch67. Direct logs at65.021–65.023s: `Select fader: selected note 3 at tick 392`; `NOTE_EDIT focus rebuild: noteId=3`; `Note selection changed: 4 -> 2`. Deferred receipt: `select_apply selected_tick=392 note_idx=3 slot=2 prior_slot=-1 apply=1 reason=note_changed`; paired motor row reports `note_idx=2 bracket_tick=392`. Host expects note_idx2. The existing stable-source waiter times out and downstream F2 is explicitly refused. This is an observed index disagreement, not a proven root cause or absent source landing; do not treat NoteId3 and selection index2 as interchangeable.
- No intended Move/deselect/stacked-reselect completed. Canonical offline verify-only on the new session: base PASS, edit FAIL (`selected_note_moved`, `deselect_empty_step`, `stacked_reselect_after_deselect`, `stacked_note_identity_unproven`), exit2. Scratch replay artifacts retained, original capture unchanged.
- Ring readback at29.498234s: `tierA_evict=0,overflow=0,crit_rej=0`, non-Tier-A `shed=3`; subsequent shedding row observed. No `RING,overflow` rows. This does not certify loss-free delivery of every non-Tier-A witness.
- Final `shutdown-result.json`: statuscomplete,drains2,oktrue; no `captures/.device-shutdown-incomplete.json`. Failed/partially edited test take preserved. Final heap inspection complete for both arenas; no broader allocator/timing claim.
- `pio test -e native`:3072cases/3065succeeded/7skipped/0failed,exit0 (90.698s). No production files changed by this rerun.

## Closeout / next boundary

I3b and **Qualify repaired composed seed/edit leg on device** remain unchecked, as do broader entry/feedback/continuity parents. Next bounded work is to establish the source selection receipt's index/identity semantics and current-source confirmation before rerunning; no larger timeout, stimulus change or firmware fix is authorized/inferred here. Source-selection repair's pre-existing unchecked local task is not checked off from a native sweep or this failure.

Runtime state/current scope, active task progress and closeout-plan handoff updated. Nothing shipped, so no deliverable checkoff. Session closeout: no new architecture/decision/exclusion; no DEC addition or guide behavior change. No upload, other-slot clear, quarantine/delete/rename, commit/push/PR. Unrelated dirty changes preserved.

Final documentary checks: `openspec validate runtime-working-set-ownership --strict` valid; `git diff --check` clean; runtime/task entries reread and device checkbox remains unchecked; final latch absent and working branch unchanged.
