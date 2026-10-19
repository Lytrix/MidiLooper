# I3b — source-confirmation repair and validation

## Execution contract before edits

User requested investigation/fix to validate the failed test run; scope recorded under CURRENT_WORK. Continue registered STIM-NE-RESELECT-STACKED-01@v1 on authorized track8/slot4 only, installed HEX7678b719/host clock300BPM; managed fresh boot and two canonical drains, unchanged bounds. Preserve unrelated dirty work on feature/unified-rolling-playback-path @26125ceb. No firmware/stimulus/marker/budget/Git or broader SD changes.

Existing owner: legacy_edit_baseline._wait_for_select_stable_at_tick, called by _fader1_select_fixture_note through the existing source/Move scenario. Firmware read-only trace: SelectFaderInput logs target index before macro commit, resolves same NoteId in post-commit notes and applies navigation; NoteEditSelection logs final selected index directly; FaderMotorSync queues selection_motor_sync with final index/bracket. Deferred #DBG is not live confirmation; NoteId and array index are distinct. No ownership/runtime allocation/schema transition.

Fresh failing run: direct pre-index3/tick392, focusNoteId3, direct final index4→2, deferred applyindex3/motorindex2; host expects2 and reads only pre-index, timing out before F2. Baseline focused wait/mapping/Move suites pass despite missing counterexample.

Frozen oracle: fresh direct fader target followed by final selection-index change at expected tick/index qualifies after existing quiet interval. Wrong final index, wrong target tick, final row without a fresh target, pre-baseline rows, empty selection or subsequent different target refuse; delayed pre-commit #DBG twin cannot undo occurrence-order proof. Existing direct-only compatibility stays, strict source/Move/focus and scenario downstream checks remain. No accepting generic motor rows or inferring NoteId from index. Simulated collector/clock exercises production wait; exact capture replay is read-only corroboration, not hardware PASS.

Verification: regression RED before host edit; focused edit_select_wait/source mapping/Move gate plus seed/reselect/boot neighbours; broad host discovery compared with documented existing6F+1E; native compatibility; strict OpenSpec/diff hygiene. One focused independent review after repair. Canonical fresh HITL and inspect exact seed/current identity/geometry/intended Move/applied-empty/two reselects/final teardown; PASS_CORE never closes optional timing/retention parents. No build/RAM1 check needed unless firmware changes (not authorized here).

## Completion

**Completed composed core qualification; optional/parent qualification remains open.** Results below distinguish source-wait repair, observation extension and replay correction.

## Authorized observation slice — execution contract

After source repair, run `captures/i3b-source-confirmation-t8s4-01` passed seed/source/exact Move392→200/empty48 and drove stackIDs2/3, but current selection-window DNTE geometry was absent; later80s dumps cannot qualify74–76s selections. Owner explicitly approved capture-only marker extension and build/upload/rerun. This supersedes the initial installed-image/no-firmware restriction only for the observation slice.

G0 scope traced: existing EditManager::rebuildNoteEditFocusForDisplayNote through fader/coordinator/start-state/add/geometry/encoder helpers. VERIFIED: active/nonzero-loop guards, current-state mover upsert, noteEditFocusApplyDisplayNote/current row before logger, existing SESSION_CAPTURE gate and direct timestamped Logger::log delivery. Alternate callers mean marker is not standalone F1 proof; current direct select and bounded current session/time interval must qualify it. Object lifetime is synchronous EditSession/current-state/focus; no new stored reference/owner/queue/continuation. Persistence/undo/publication/selection transitions remain untouched; consumers remain read-only. ExecutionBounds row26's surrounding composed focus remains unbounded; logging is not a timing guarantee. UNKNOWN until build/HITL: memory placement/actual direct marker delivery and physical timing. No architecture conflict or ownership transition; G0 PASS, ordinary existing-marker extension, not a runtime reassessment.

Historical decision review: topic grep in DECISION_LOG plus current-state spec/Move guide/active tasks; no conflicting observation policy. Current state owns currentSpan, Focus is interaction cache and NoteId differs from UI index. Reuse existing logger prefix/call; no migration/DEC/marker-tier addition. Read HITL authoring traps and filled [recipe](HITL_TEST_RECIPE.md).

Frozen marker oracle: append pitch/start/end from the observed current-state row (missing row retains explicit presence=-1); host requires visible presence0/projectstoStore1, nonzero NoteId, legal pitch and positive span, selection-window timestamp, exact expected geometry and unique current focus identity. Conflicting geometry must refuse even when another DNTE row matches. No copying seed geometry into current proof. Logger payload sink is absent natively, so build/HITL carries emission proof; parser RED controls and real native focus/current-state compatibility carry semantic checks. Format flash-placed to preserve RAM1, no new buffers/PSRAM/LoopPool footprint, baselinepadding968/floor840/no rebase.

Required checks: six new host marker cases RED before parser; focused neighbours/native/defaultcapture/marker audit; dedicated canonical build/upload (explicit owner authorization), pinned HEX/readboot/seed/edit/core/teardown. No second broad review loop; single review already found no defects in source-wait slice; observation slice locally inspected and regression/hardware tested. Optional UNMEASURED/parent gates remain open.

## Verification and hardware results

- Source wait regressions:8newcases,4RED before host change (2valid rejects + wrong final index/deselect falseaccepts), then168focusedpass. Baseline wait/mapping/Move suites were green before adding counterexamples. Wait now correlates fresh direct pre-target tick with final published index, keeps existing4s deadline/quiet budget and one snapshot per poll so newly appended rows are not skipped. No NoteId-index equivalence or weaker source-stage bypass.
- One independent read-only focused review of source waiter/tests: no findings, no repeated review. Marker extension and final parser correction were locally inspected/tested; no independent approval claimed for them.
- First corrected device attempt `captures/i3b-source-confirmation-t8s4-01`, oldHEX7678b719/no upload: exact1536tick8pair qualified seed/current save/drain/targetPLAYING, source confirmed, actual MoveNoteId3/pitch67/392→200, empty48 and both stackedcurrentIDs2/3. Live and full replayFAIL only mandatory current geometry; no DNTE rows inside reselection intervals. Canonical finalcomplete/drains2/oktrue/no latch. Original artifacts preserved, no historical promotion.
- Current focus geometry regression:6newcases,2RED before parser edit; valid marker ignored and wrong direct geometry hidden by correctDNTE. Firmware appended currentSpan fields to existing SESSION_CAPTURE direct focus row, using flash-placed format; missing row retains presence=-1, never qualifies from fallback. Verifier requires visible/projecting current row, legal positive span, exactgeometry and unique focus identity within current direct-selection interval.
- Default capture build `pio run -e teensy41-capture-serial`:SUCCESS. RAM1 byte-identical to pre-marker line: variables96,192/code392,248/padding968/floor840/locals34,880; no rebase. RAM2 163,360; EXTRAM11,680 reported, no new marker resident state/allocation. Marker audit90Tier-Aprefixes/0over; no new Tier-A family, existing direct logger delivery tested on target.
- `pio test -e native` after firmware edit:3072cases/3065passed/7skipped/0failed,exit0 (100.440s). Native Logger::log has no text sink; this is actual native semantic compatibility, not emission proof.
- Broad host discovery before final DNTE-precedence correction:1604tests/6failures+1error, same previously documented cases; intermediate source-only sweep1598/same6F1E. No broad-green claim. Final full sweep recorded separately after closeout checks.

### Qualified run — captures/i3b-focus-geometry-t8s4-01

Authoritative command: `.venv/bin/python scripts/host_midi_hitl.py run --preset note_edit_reselect_stacked --track 8 --loop-slot 4 --midi-channel 8 --clock-source host --tempo-bpm 300 --build-env teensy41-capture-serial --build-upload --boot-reset --capture-boot-wait 30 --out-dir captures/i3b-focus-geometry-t8s4-01`. Explicit owner upload/overwrite authorization; default build/uploadSUCCESS13.001s, managedrunexit0. No captured command executed or canonical stimulus edited.

Metadata Git26125cebb08980342ba56ed2b77e5397783ac8e5+dirty; HEXSHA256 **d1b93b866e204c08573b28606c0676c67cc6e066133b6c507046cf7353931780**. Session `session_20261012_144303.log`, fresh crash0/boot_commit16.860872s. Baseline report `host_midi_automation_baseline_20261012_144405.json` PASS/overall_oktrue: committed1536ticks(raw1544)/8On8Off0unmatched/currentpersistenceok/Recorddrain; target `#TRACK_STATUS,us=70404723,track=7,slot=3,state=PLAYING,transport=1`. First-press NOTE_EDIT/sourcewait confirmed. No full content-pipeline/longloop/timing claim.

Current direct trace:
-73.135s: focusedNoteId3/presence0/projects1/pitch67/start392/end488.
-76.673s: exactMove type3/NoteId3/start200/end296/pitch67.
-82.218s: appliedempty step48; first request used scenario's existing bounded retry (2of3), no deadline/gesture-plan change.
-84.104s: first stacked focusNoteId2/presence0/projects1/pitch64/start200/end296.
-85.777s: second stacked focusNoteId3/presence0/projects1/pitch67/start200/end296.

Machine report `host_midi_hitl_note_edit_reselect_stacked_20261012_144429.json`: **oktrue/issuesempty/PASS_CORE**, destindexes1/2, currentIDs2/3, movedID3, moveTick200. `qualification_complete=false`; playing_motor_value_only/value_only_notegate_held/select_apply_highlight_follows/moved_note_kept_after_deselect allUNMEASURED. Physical checklist not supplied by operator; no panel/pixel proof claimed.

Final `shutdown-result.json`: complete/drains2/oktrue; no incomplete-shutdown latch. Ring readback: shed3/tierA_evict0/overflow0/crit_rej0; non-Tier-A loss not concealed. Internal/external final heap inspection complete. Take preserved; no other-slot clear or quarantine/delete/rename.

### Final full-capture replay correction

LivePASS_CORE initially replayedFAIL because later DNTE inventories reused UIindexes after exit and contaminated exactgeometry sets. One new index-reuse regression RED, then repaired by separating identity-qualified current focus geometry from index-based DNTE compatibility evidence. Current focus wins; wrong or conflicting direct geometry still rejects, old-marker/DNTE-only compatibility remains. No stronger historical proof synthesized. Final focused175/175; canonical offline verify-only on the same full session now basePASS/editPASS (nested corePASS_CORE), exit0, scratch `i3b-focus-geometry-final-replay`. No extra flash/overwrite required for host-only correction; original live report unchanged.

## Closeout disposition

**I3b — composed seeded-edit core:** qualified, exact clause **Qualify repaired composed seed/edit leg on device** eligible for checkoff. **S7.5b.8b — bounded entry/focus preparation**, separate feedback/timing/physical/retention/continuity parents remainunchecked; this short core run is not their acceptance. Existing unrelated source-mapping task not broadly checked off by this narrowly reviewed correction; new source-final-index/capture-observation repair recorded separately. Guide/runtime/task/deliverable/handoff updated with this distinction. No architecture/owner/schema/bound/selection policy change or new DEC, no commit/push/PR; unrelated dirty work preserved.

## Final checks

Final full host discovery:1605tests/6failures+1error,exit1 (204.443s), same failure names as pre-observation sweep: legacy EditSession recursion; machinery closeout; two compatibility consumer inventories; foundation/layered verifier Namespace defects; occupy-paint boundary. No new failure names or broad-green claim. Finalfocused175pass, native3065pass7skip0fail, build/upload and pinned full-capture core replay evidence above remain valid.

`openspec validate runtime-working-set-ownership --strict` valid; `git diff --check` clean. Checked composed core/new repair clauses and unchecked broader entry parent reread. Final latch absent, working branch unchanged, current built HEX still exactlyd1b93b866e204c08573b28606c0676c67cc6e066133b6c507046cf7353931780. Session closeout checklist: runtime/current scope/tasks/guide/deliverable/handoff updated; no new architecture/owner/representation/policy decision, no DEC addition. No commit/push/PR. Remaining blockers only for separate optional/physical/timing/continuity work and known unrelated broad-host failures, not the qualified composed core.
