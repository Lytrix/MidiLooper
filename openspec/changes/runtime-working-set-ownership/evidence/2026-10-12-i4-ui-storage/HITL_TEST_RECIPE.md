# I4 — UI/storage qualification: checkpoint-first recipe

Frozen before host adapter implementation. Companion: EXECUTION.md.

| Field | Contract |
|---|---|
| Acceptance | MIDI overlay Save creates one preserving Current checkpoint with fresh ordered Save/queue/dispatch/complete/exit receipts and exact Set/revision identity, before any scratch mutation. This is setup, not an I4 device-clause PASS. |
| Catalog | Listed `host_midi_hitl.py stimulus list` before addition (scratch i4-stimuli-before.txt). No existing checkpoint-only recipe. |
| New stimulus | STIM-I4-CHECKPOINT-01@v1; new recipe, no canonical recipe edits. |
| Gestures | actions/buttons: double EDIT to enter, global TRACK short to scroll up (clamped), EDIT short to Save. Constants from control_constants. No slot/record/load/clear gesture. |
| Evidence | Existing LDSV, OVLY, PERS rev_request/rev_dispatch/rev_overlay_save/rev_complete only; timestamp-qualified against fresh fenced TRACK_STATUS before each action. No new marker/tier/rate. |
| Marker tier / contract | n.a. host-only, no new serial emission. Missing receipts refuse; do not infer absence of firmware behavior. |
| RAM1 | n.a. firmware unchanged, installed HEX d1b93b866e204c08573b28606c0676c67cc6e066133b6c507046cf7353931780. No build/upload. |
| Regression | test_i4_checkpoint: stale/delayed receipts, wrong row, missing dispatch, mismatched/ambiguous identity, missing owner/collector, unauthorized target, no mutation retry. |
| Boot | Reuse managed attach/readiness and wait_for_boot_handoff; fresh stopped status, refuse running transport. No forced reset. |
| Entry | After explicit owner confirmation that normal app screen is visible and overlay is closed: host_midi_hitl.py run --preset i4_checkpoint --track 8 --loop-slot 4 --midi-channel 8 --panel-overlay-closed --capture-boot-wait 30 --out-dir captures/i4-checkpoint-t8s4-01; default managed timeout360s. Owner observation is recorded, not inferred; without this flag or closed LDSV history the runner refuses before gestures. |
| Bounds | 30s boot; 8s overlay; 8s row; 90s commit. Poll/drain via actions.observation.await_markers. No blind retry or fallback. |
| Safety | Track8/slots4–5 authorization checked before opening MIDI; checkpoint affects whole workspace but preserves it. No serial mutation hooks, second serial owner, load, cleanup, deletion, quarantine or Git delivery. Existing overlay open is refused. |
| Verdict | CHECKPOINT_CREATED only on exact fresh coherent machine receipts; OLED observation UNMEASURED until owner reports. Missing/conflicting evidence SETUP_INCOMPLETE. |
| Artifacts | i4-checkpoint.json (identity/receipts/floors/session/stimulus), metadata.json/hash, managed session log, shutdown-result.json with two canonical drains. |
| Next | Do not mutate scratch until checkpoint identity saved. Load needs separate pre-confirm identity proof; independent-index/default-Transport/manual gates remain open. |

## Corrected firmware candidate (not yet uploaded)

Owner-approved admission/header-patch slice is locally verified in ADMISSION-FIX.md.
Default capture candidate HEX8ed5a9dc97ae56dc0612808d55f353526025c57d54f856ad1067ebea4e518c6b;
RAM1 padding904>=840 (64Bmargin), no new marker/instrumentation. Installed image
remains d1b93b86. No upload/device rerun authorization inferred from implementation
approval. After explicit flash approval, perform safe managed preflight and upload;
use the SAME registered checkpoint preset, a fresh capture directory, current owner
closed-overlay/OLED witness, exact new revision identity and canonical two drains.
Do not treat the freshly built cached HEX as the installed image before upload.
This leg qualifies checkpoint setup only, not physical spinner/PLAYING load/focus restore.

## Authorized execution update

Owner approved upload and checkpoint-only rerun. Pinned8ed5a9dc uploaded/hashunchanged,
safe pre/post/finaltwo-drains complete. No-reset registered leg02 qualifies exact
S0001_v0001 from fresh fenced Save/dispatch/complete receipts and drained replay;
owner confirms responsiveOLED/visible save completion. Save took44.656534s,
considered too slow by owner; no performance, PLAYING spinner or load/focus/restore
qualification inferred. Original machine report retained; owner report is separate.
See [I4 — upload/checkpoint evidence](UPLOAD-CHECKPOINT.md).

## Bounded serializer candidate (not yet uploaded)

Owner-approved optimization is locally verified in SERIALIZER-OPTIMIZATION.md;
DEC-118 — bounded forward save serialization preserves maxGrains1, worker budgets,
main cadence, format/source admission and CRC convention. Candidate defaultcapture
HEXcdfe4b517563df6c5fa077f63aa59f7e3459a7feade8846c66f3ff5461ab489d;
RAM1padding1160>=840/globals/RAM2/EXTRAM unchanged. No new device marker/stimulus.
Installed8ed5a9dc and existingS0001_v0001 remain unchanged. Separate upload approval
is required before safely drained/pinned upload and SAME registered checkpoint
recipe/new capture directory/currentOLEDwitness. Compare sameworkspace Save→complete
against44.656534sreference while requiring exact created identity/live+drained proof,
visible completion and canonicaltwo-drains; sparse199nativecalls is not latencyPASS.
No record/clear/slotchange/load/cleanup or broaderI4 acceptance inferred.

## Optimized hardware execution update

Owner subsequently approved upload. Pinnedcdfe4b51 installed/hashunchanged/default
padding1160>=840; managedpre/post/finaltwo-drainscomplete/no latch. Postfreshboot
probe lackedhumanhandoffreceipt andtimedout (notbootqualification); no extrareset
or weakenedgate. OwneractualnormalOLED/closedoverlay permitsregisteredalreadybooted
no-resetcheckpoint03: exactS0001_v0002/live+drainedoraclePASS. All41orderedtail
shapes/152bars/256129payloadbytes/notes-events match02; Save44.656534→14.862218s,
3.0047×faster. Ownercompletednormally/responsive/waitacceptable forworkload.
No generaldeadline, fullhardwarebyteparity or broaderI4claim; no record/clear/slot/load
or cleanup. [I4 — optimized hardware evidence](OPTIMIZED-HARDWARE.md).
