# I4 — pinned upload and checkpoint-only rerun

Owner explicitly authorized flashing the verified candidate and rerunning the checkpoint-only leg. No recording/clear/slot selection/load/SD deletion/quarantine/rename or Git delivery. Preserve whole Current in new test checkpoint before later scratch mutation. Same registered STIM-I4-CHECKPOINT-01@v1 and HITL_TEST_RECIPE.md; no code/marker/stimulus/budget changes in this slice.

Pinned candidate: default teensy41-capture-serial HEXSHA2568ed5a9dc97ae56dc0612808d55f353526025c57d54f856ad1067ebea4e518c6b; Git26125cebb08980342ba56ed2b77e5397783ac8e5+preserved dirty work. Prior local native3070pass7skip0fail/134focused/buildSUCCESS/padding904>=840/staticreviewAPPROVE in ADMISSION-FIX.md. Verify unchanged hash before/after upload.

## Preflight

- Branch/hash/device/no latch/no other capture owner verified.
- Initial no-reset boot-census attach `captures/i4-upload-preflight-01`: no post-restore checkpoint in6s, no mutation/reset. It has no canonical shutdown receipt and is NOT flash clearance.
- Follow-up uses existing run_with_managed_capture / DeviceSession with boot_reset=never, fresh read-only TRACK_STATUS and no-op body; no second serial owner. `captures/i4-preflash-managed-01`: fresh us3808168021 track7/activeSlot3 STOPPED/transport0; shutdowncomplete/drains2/oktrue/no latch. Explicit heap observations complete.
- Board still runs prior installedd1b93b86 for these preflight observations. Managed metadata names cached candidate8ed5a9dc, not a device readback; **do not cite that preflash metadata as installed-image evidence**. No revision identity proven or claimed from workspace drain. Authorized upload may discard the old in-RAM pending Save; payload/workspace persists, no new Save before upload.

## Execution oracle

Approved image uploaded successfully with hash unchanged → current boot handoff/app witness/physical normal-screen confirmed → fresh stopped fences → MIDI overlay enter/Save-row/confirm → ordered queue/dispatch/complete/exit with coherent nonzero exact Set/revision identity → canonicaltwo-drainshutdown. Missing evidence remains SETUP_INCOMPLETE, not inferred completion. CaptureOLED actual owner report separately; this qualifies checkpoint setup only, not I4PLAYING spinner/load/focus/restore clauses.

## Approved upload and postflash observation

`pio run -e teensy41-capture-serial -t upload` SUCCESS14.121s; hash8ed5a9dc unchanged before/after; RAM1padding904>=840/no rebase. Serial re-enumerated. This image is now installed. No second upload, code/marker/stimulus/budget edits, reset after upload, content/slot action or Git delivery.

Managed no-reset postflash `captures/i4-postflash-managed-01`: fresh statusus27075531 track7/activeSlot3 STOPPED/transport0; canonicalcomplete/drains2/oktrue. Existing boot_commit14655591 and appDFRAME16212519 onward advance. Fresh boot banner/display setup row not retained in attach, so probe expecting handoffcomplete exited2/not-fresh-boot; **do not label it fresh-banner boot qualification**. Normal installed-image upload/progress/stopped readiness plus owner physical normal-screen/overlayclosed witness permit the registered attach recipe, which explicitly accepts already-booted state. No extra reset to manufacture a marker.

## Checkpoint-only registered hardware result

Owner confirmed current OLEDnormal/overlayclosed/ready. Canonical run:
`host_midi_hitl.py run --preset i4_checkpoint --track 8 --loop-slot 4 --midi-channel 8 --panel-overlay-closed --no-boot-reset --capture-boot-wait 30 --out-dir captures/i4-checkpoint-t8s4-02`.

Exit0 / **CHECKPOINT_CREATED**, exact **Set1 / revision1 / S0001_v0001**. Metadata records Git26125ceb+dirty/defaultenv/installedHEX8ed5a9dc. Fresh per-action fences enter125401642, row126214132, Save127223789us. Current-leg receipts:
- 125857655 LDSV1;126943481 OVLYsel0/0.
- 127960846 OVLYconfirm0/0;127960882 rev_requestqueued.
- 127960896 LDSV0;127960909 rev_overlay_savequeued_exit.
- 127963081 rev_dispatchrun;172617380 rev_complete0/1/1/S0001_v0001 and direct `Revision commit complete S1 v1`.

Liveoracleoktrue/issuesempty; replay from relevant rows of full drained session passes same identity/floors. Shutdowncomplete/drains2/oktrue/no latch. No matching crash1/RINGoverflow/critical/tier-a-evicted rows found by bounded scan; absence is not a universal no-loss claim (no new ring statistics observation). Preserved failed run01, no scratchrecord/clear/slot selection/load/SDdestructive/Git action. Committed checkpoint identity stored in original i4-checkpoint.json before any later scratch operation; no hardware content-parity/readback/load claim.

Owner actual OLED report, preserved in separate oled-observation.json without rewriting machine report: **“save open/closed normally, screen stayed responisve, save indicator visbily completed, but it seems  too tale way too long +1 minute intstead of seconds”**. Overlay and visible save completion qualify only this STOPPED checkpoint leg.

## Slow-save concern explicitly open

Owner considers duration unacceptable; **no performance PASS**. Device Save-confirm→dispatch2.235ms, Save-confirm→commit-complete44.656534s; admission defect is removed, serialization/completion remains slow. Existing interval rows:460PERSwork,18DFRAME,9timingwindows; persist_save span2830063us near start, then58277/79659/84092us near completion. These are recorded evidence, **not complete stage attribution or OLED pixel timing**. No numerical deadline invented, worker budget loosened, new marker or broad speed repair. A bounded follow-up may trace existing revision steps/SD work and pin an acceptable speed oracle before optimizing; current upload-only/checkpoint authorization does not implement that change.

## Remaining I4 qualification

Checkpoint-first setup now qualified on corrected installed image. Save-status-display4.3 — record→PLAYING spinner/result-beforeStop is NOT this stopped revision leg. Slot-selection-focus8.1–8.5 — focus/independent-index/revision/defaultTransport cases and overlay6.2–6.4 — dirtyYes/load/PLAYING minimal smoke remainopen; no wholeI4PASS. Native gate/phase previously checked remains valid. Next hardware load must prove it targets exactly this test-created checkpoint before confirmation; no guessed catalog index. Device left stopped/drained, checkpoint retained, no cleanup.

Final21hostregressions pass; diffhygiene/strictoverlaychangevalidation valid (previous archive/sync informational prerequisites unchanged), no new native run claimed for this device-only slice (prior3070pass7skip remains). Before/after status preserves unrelated work. Runtime/task/guide/delivery/recipe updated; no new architecture decision or performance deadline adopted, no DECappend/Gitdelivery. Device remains stopped/drained; test-created checkpoint retained.
