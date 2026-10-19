# I5 — acquisition/Record compatibility HITL handoff

Status: hardware compatibility PASS. User continued the gate and explicitly confirmed track8/slots4–5 only may be overwritten. Exact retained default-OFF image uploaded no-build; registered baseline, fingerprinted offline replay, parallel native suite and final managed postflight pass. No firmware change, SD cleanup/quarantine/delete/rename, other-slot content gesture or default activation. Native concurrency proof remains separate from compatibility; no running740→768 readiness claim.

| Field | Recipe |
|---|---|
| Acceptance | On the new default-OFF image, real host USB-device MIDI Record/Stop/overdub/undo/redo retains exact baseline content and finishes without crash, stuck request, or new critical evidence loss. |
| Reused stimulus | `STIM-BASE-2-2-01@v1`; catalog listed with `.venv/bin/python scripts/host_midi_hitl.py stimulus list`, saved to session scratch `i5-acquisition-stimulus-catalog.log`. |
| New stimulus | None. Canonical recipe unchanged. The baseline is a compatibility gate, not deterministic acquisition-race or running740→768 qualification. |
| Gestures | Existing managed base flow and `scripts/hitl/actions/` Record/Stop/slot/transport helpers only; no hand-rolled MIDI. |
| Expected markers / tiers | Existing base accepted-Capture, Record-start/stop, snapshot/commit, MIDI/ledger and shutdown/ring observations; use existing verifier. No added Tier-A line, no new per-event traffic. |
| Marker tests / contract | No marker changes; existing classification retained. Marker audit before hardware. |
| RAM1 | No test instrumentation added. Successful defaultcapture build41.065s: variables96192/code392056/padding1160≥840 (margin320)/locals34880. Marker audit93Tier-Aprefixes/0over PASS. Exact retained and cached HEXf48aca66…b0068/ELF7cd88398…a3563 verified before hardware. |
| Native discriminator | `test_record_direct_writers_refuse_executing_builder_before_role_mutation`, preparation cancellation tests, manager queued pre-roll retry and real USB Clock retry. Controls/evidence in acquisition packet. |
| Preflight / smoke | Canonical managed read-only no-reset attach, fresh stopped/idle target and storage readiness; two verified drains/postflight/safety-latch check. Retain exact new and installed/default restore HEX+ELF before any no-build upload. Verify hashes before and after upload. |
| Full entry point | After explicit current-session scratch-content permission for track8/slot4 (or another named safe target): `.venv/bin/python scripts/host_midi_hitl.py run --preset base --track 8 --loop-slot 4 --midi-channel 8 --clock-source host --tempo-bpm 300 --no-boot-reset --out-dir captures/i5-acquisition-compat-t8s4-01`. New directory required; managed timeout360s unless recipe explicitly overrides. Do not execute a captured command.json. |
| Verdict | Strict registered base verifier `overall_ok:true`, no issues/new critical loss, accepted/committed/playback content and ledger closure correct; canonical two Stops/two drains/postflight complete/no safety latch. Missing rows are not absence evidence unless capture loss accounting qualifies it. |
| Evidence | Managed metadata, exact image hashes, raw bounded tails plus existing parser/verdict reports, shutdown result, target/content permission and hardware observations. No maximum-as-WCET, calibration, wire-arrival or new-reader readiness inference. |

Hardware content permission applies only to this explicitly approved8/4–5 scratch evaluation, never inferred for cleanup/quarantine/delete/rename/other slots. The new evaluated default-OFF image now remains installed; prior default restore fallback is retained, not re-uploaded.

## Current-session execution authorization

User: “continue”; explicit scratch response: “Yes — track 8, slots 4–5 are safe scratch slots”. Branch feature/unified-rolling-playback-path/HEAD26125ceb+preserved dirty tree. Restore fallback retained at session scratch `i5-target-cost-image/default-off/`; evaluated new default image retained at `i5-acquisition-default-image/`. No-build upload must preserve exact new hashes. Managed preflight/postflight and canonical shutdown are mandatory; do not clear a safety latch to force progress. Hardware/native/verdict evidence will be recorded in the acquisition packet.

## Result

`captures/i5-acquisition-compat-t8s4-01`: PASS/overall_oktrue/issuesempty/captureCOMPLETE/cap_full; Record/two overdubs/undo1/redo1/geometry/grid/span/cleanup/persistence gates pass. Critical-loss0/routine shed3, advisory second-overdub first-note lateness retained; no deadline safety proof. Exact HEXf48aca66…b0068/ELF7cd88398…a3563 verified before/after no-build upload and final cache check. Preflash and final postflight stopped/closedoverlay/guard15/two verified drains/nolatch; early postflash storage-work refusal retained and second bounded probe passed. Parallel fullnative3254passed7skipped121suites. Fingerprinted replay02 PASS/headerpresent/must-prove notskipped; raw-session replay01 is legacy/pre_fingerprint and weaker, not equivalent proof. Actual run config hostUSB300BPM; canonical header's nominal tempo120 descriptor is not timing evidence. Full details and artifact paths: [I5 — hardware execution record](ACQUISITION-CONCURRENCY.md#current-session-hardware-compatibility-execution-2026-10-15).
