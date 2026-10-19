# I4 — optimized upload/checkpoint timing

Owner explicitly approved flashing the verified optimized image and rerunning checkpoint-only timing. No record/clear/slot selection/load/SD deletion/quarantine/rename or Git delivery. Retain Current, prior test-created checkpoint S0001_v0001 and unrelated dirty work. Reuse STIM-I4-CHECKPOINT-01@v1 / HITL_TEST_RECIPE.md, no stimulus/firmware/budget change in this hardware slice.

Pinned candidate default teensy41-capture-serial HEXSHA256cdfe4b517563df6c5fa077f63aa59f7e3459a7feade8846c66f3ff5461ab489d; Git26125cebb08980342ba56ed2b77e5397783ac8e5+dirty. Local native3074pass7skip/focused148pass/host21pass/buildSUCCESS/RAM1padding1160>=840/staticreviewno findings in SERIALIZER-OPTIMIZATION.md. Verify unchanged hash before/after upload.

## Preflight

Managed run_with_managed_capture/DeviceSession, boot_reset=never, read-only status/no-op body: captures/i4-optimized-preflash-01. Fresh us56477943 track7/activeSlot3 STOPPED/transport0; shutdowncomplete/drains2/oktrue/no latch. Board still prior image until approved upload. Cached candidate hash in preflight metadata is not device-image readback. No repeated Save or scratch operation.

## Frozen hardware oracle

Successful pinned upload → responsive stopped/app readiness and owner normal-screen/closed-overlay confirmation → SAMEregistered managed checkpoint leg/newdir/noextrareset → fresh current-action Save enter/row0/confirm/request/exit/dispatch/complete/exactnonzero identity → live/drainedreplay proof/two canonical drains/OLEDvisiblecompletion. Missing remains SETUP_INCOMPLETE. Do not execute captured command.json or open second serial owner.

Timing comparison: device Save-confirm→commit-complete against44.656534s on checkpoint02/8ed5a9dc. Qualify comparable observed tails/bars/notes/events/tailpayload inventory, not merely same track CLI. Earlier workload41LSV1tails/152bars/256129tailpayloadbytes/20566servicecalls; numericwork fields are not durations. New calls can differ (intended); contents/shape must match. No exact seconds deadline or broader I4/playback/focus/restore/content-parity acceptance inferred from one stopped checkpoint leg.

## Results

### Approved upload and readiness

Default `pio run -e teensy41-capture-serial -t upload` SUCCESS13.419s; approved cdfe4b51 hash unchanged before/after, RAM1padding1160>=840/no rebase; serial re-enumerated. This image is installed. No further firmware/source/stimulus/budget change, second upload or reset after upload.

Postflash no-reset managed attach `captures/i4-optimized-postflash-01`: statusus63794488 track7/activeSlot3 STOPPED/transport0,29appframes while observing; canonicalshutdowncomplete/drains2/oktrue. Currentcrash0banner3005004, boot_commit14644574 and DFRAME16187395 onward present, but human `DisplayManager: Boot setup complete` receipt absent. Probe exited2/handofftimeout; **no fresh-boot qualification**. No Save was sent by probe, no gate weakened/extra reboot. Subsequent ordinary already-booted attach uses registered recipe as designed, with fresh status and owneractualnormalOLED/closedoverlay witness.

### Canonical checkpoint-only leg

Owner confirmed normal screen/overlayclosed/ready. `host_midi_hitl.py run --preset i4_checkpoint --track 8 --loop-slot 4 --midi-channel 8 --panel-overlay-closed --no-boot-reset --capture-boot-wait 30 --out-dir captures/i4-checkpoint-t8s4-03` exits0 / CHECKPOINT_CREATED, exact **Set1/revision2/S0001_v0002**. Metadata pins installedcdfe4b51/defaultenv/Git26125ceb+dirty; originalmachine report retained.

Current-leg device receipts: Saveconfirm260770002us, revisiondispatch260775834us, rev_complete275632220us. Requiredenter/Save-row/request/exit/queuedexit/dispatch/exactidentity receipts satisfyliveoracleoktrue/issuesempty and replayfromfulldrainedlog passes same floors/identity. Finalshutdowncomplete/drains2/oktrue/no latch. Bounded fault scan finds no matchingcrash1/RINGoverflow/critical/tier-a-evicted rows; this is not a universal no-loss claim. No record/clear/slotchange/load/cleanup/SDdestructive/Git action; both testcreatedS0001_v0001 andS0001_v0002 retained.

### Comparable observed workload and measured improvement

| Metric | Reference checkpoint02 /8ed5a9dc | Optimized checkpoint03 /cdfe4b51 |
|---|---:|---:|
| Save→complete seconds |44.656534|14.862218|
| Save→dispatch milliseconds |2.235|5.832|
| LSV1 tail count |41|41|
| Tail bars |152|152|
| Tail payload bytes (not whole revision) |256129|256129|
| Tail service calls |20566|4329|
| Inside paired tail intervals seconds |40.289918|12.987241|

All41orderedtail tuples (bars/payloadbytes/notecount/eventcount) match exactly, not just aggregates. Timing uses each image's own device Save/complete stamps. **3.0047× faster /66.7188% elapsed reduction** on this stopped workload. Observable shape matching is not full SD byte hash or MIDI content parity; native byte-equivalence/readback fixtures remain separate correctness evidence. Save is no longer delayed by admission; remaining14.86s includes actual serialization/SD/service work, no exact per-function CPU/SD attribution.

Owner report preserved separately in captures/i4-checkpoint-t8s4-03/oled-observation.json: **“Completed normally; responsive; wait is acceptable.”** Accepted for this workload only; no general seconds deadline, maximum-content envelope or PLAYING timing guarantee adopted. captures/i4-checkpoint-t8s4-03/timing-comparison.json carries image/identity/measurements/comparison limits; full ordered parser output also retained in session scratch i4-optimized-comparison.json. No machine-report rewriting or stale-run PASS conversion.

### Qualified boundary / remaining gates

set-revision-persistence3.1b — optimized stopped checkpoint timing/receipt/cohort task qualifies; owner-visible completion and acceptable wait recorded. Full hardware SD/MIDI content parity, record→PLAYING spinner-beforeStop, slot-focus/restore/dirtyYes/load/PLAYING minimal smoke and broaderI4remainopen. No cleanup or further mutation; device left stopped/drained. Preserve same protected targets and prove exact testcheckpoint identity before any later load.

Final closeout:21hostreplayregressions pass; comparisonJSON/identity/drains/OLEDacceptance arithmetic validated; strictset-revisionchange/diffhygiene clean; no latch. Prior native3074pass7skip remains local verification (no new native run claimed for device-only slice). Runtime/task/guide/decisionverification/delivery/recipe updated; no newtimingpolicy/architecturedecision or Git delivery. Before/afterstatus unchanged outside already-dirty documentedpaths; source/test/host/stimulus/budget untouched. Device stopped/drained, Current/testv1+v2 retained.
