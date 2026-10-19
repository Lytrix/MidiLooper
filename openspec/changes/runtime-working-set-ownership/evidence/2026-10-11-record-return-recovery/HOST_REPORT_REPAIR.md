# Host report boundary and persistence observation repair

## Execution contract

User-approved follow-up to S7.5b.5 — next Record-return readiness. Working branch `feature/unified-rolling-playback-path`, baseline `abaa30975d2cd2650fbce6638ef9e6d3783ad5d7` plus existing dirty firmware/docs, preserved. Existing firmware image `a9a65d2f…` unchanged. Current run03 proves actual first-OD input16/16/0 versus parser8/7/1 because predecessor Record retirement occurs during overdub; persistence result appears after baseline report but before managed capture closes.

Owners: `serial_timing._pair_overdub_sessions` for phase pairing; `_managed_record_evidence_cleanup` for cleanup while MIDI ports remain open; existing `DeviceSession` for Stop/drain/Stop/drain and incomplete latch. Reuse these, no second parser/wait algorithm. Historical review: DEC-092 — response-driven HITL; architecture `HITL_ARCHITECTURE.md` execution wrappers and single-owner rules. No new architecture decision or exclusion.

Transition oracle:
1. Enter first overdub, receive On/Off around a predecessor's generic `Reentry released`; actual ST/ODUB stop defines the window, generic release cannot end it. Typed evidence wins over ambiguous fallback text. Deferred ST admission remains earlier than its completion; fallback stop handling remains tested.
2. Baseline body returns or raises, while its managed capture and MIDI output remain open. Existing Record-evidence retrieval finishes; canonical DeviceSession persistence drain completes before the final report log snapshot. A delayed `PERS,result` is visible to the same verifier; missing/error result remains failing, incomplete drain propagates as incomplete and latches. Original leg exception must survive cleanup; unmanaged runs send no new messages.
3. Replay run03 without hardware to verify matched OD boundaries and persistence summary. New hardware run must use unchanged canonical base stimulus, host300BPM, managed reset, only authorized8/4 overwrite, no upload needed for this host-only change. Preserve managed360s watchdog and stop/drain latch. Runner exit0 alone is not PASS.

Verification: focused host unit regressions RED before production edits then GREEN, neighboring managed/phase/report suites; one focused lifecycle review for this new host cleanup ordering if needed; full host suite baseline comparison for recoverable pre-existing failures. Native before delivery/PR as applicable, no firmware changes or Git delivery requested. Strict OpenSpec validation and diff check. Hardware after local checks; record exact verdict, image pin, phase balances, persistence result, shutdown and remaining gates. Parent tasks remain unchecked until full acceptance.

Non-goals: firmware fixes, heuristic verdict overrides, weakening unmatched-note/result checks, changing transport gestures inside phases, persistence polling reimplementation, new CLI/owner/buffer, SD destructive operations, automatic commit/PR.

## Evidence

Fixture implementation pending; checks and device result will be appended.

## Implemented and verified

`_pair_overdub_sessions` excludes generic retirement release, chooses typed ST/ODUB admission/completion evidence before explicit overdub-stop text and reentry-window fallback, and bounds matching by the next distinct machine/human start under existing marker slop. Unused unqualified fallback text does not manufacture stale-stop ambiguity. `_managed_record_evidence_cleanup` now invokes the existing canonical `DeviceSession` after Record retrieval, still within the MIDI-port context, before final same-log report reread. Its receipt is `record_evidence_drain.persistence_shutdown`; incomplete cleanup sets complete=false, preserves the original leg exception and latches via the existing owner. No result check or verdict rule was weakened; unmanaged behavior and outer every-exit drain remain unchanged.

New host suite `test_baseline_report_boundaries.py`:13 cases covering predecessor release, reversed row ordering, ST/ODUB priority, fallback support, missing stop, delayed persistence result, failed/missing result, incomplete drain/latch and original exception. Initial pre-fix run12 cases:10failures/1error (missing new receipt) /1success; synthetic dispatch fixture later corrected to the actual PERS-work protocol. The delayed-result RED was missing result, not a new verdict-policy assertion. One focused independent review found only the human-only next-start gap; both human-only and mixed-source variants RED-proved the merged-window/no-ambiguity defect, then GREEN after the local bound correction. Cleanup ordering otherwise sound; no repeat review/blanket approval claim.

Focused100/100 before review; final corrected phase/live-observation/shutdown/managed suites **184/184 pass**,18.722s (`host-report-final-focused.log`). Full host sweep before the final human-start correction **1442 cases/6failures+1error**,202.630s (`host-report-full.log`); read-only pristine two-module overlay reproduced the exact same six failure names and error in the43-case affected-failure sweep (`host-report-original-failures.log`), including subprocess checks. No workspace source swapping. Final local correction retested in184 focused cases; no final-full-host-sweep claim. Full/native/build checks for unchanged firmware remain the earlier recorded checks; no new native run, firmware rebuild/upload or Git delivery in this host-only slice. Strict OpenSpec valid and diff check clean at checkpoint.

Run03 read-only replay now names firstOD29.523982s→31.980808s and secondOD40.620029s→43.028415s, with no ambiguities. Full drained replay persistence first_missing=None/result_ok=true at74.038269s, confirming both original report defects without changing historical reports.

## Authorized canonical hardware repeat — PASS

Non-reset `captures/host-report-repair-preflight-01`:complete/drains2/ok. Reused already-uploaded default HEX SHA-256 `a9a65d2f56e52845dc777a6c3203bb1ee72b363f83ac10a761251b5baf9075f6`; unchanged firmware and stimulus, no flash. Command:

```sh
.venv/bin/python scripts/host_midi_hitl.py run --preset base --track 8 --loop-slot 4 --midi-channel 8 --boot-reset --capture-boot-wait 30 --clock-source host --tempo-bpm 300 --stop-after-overdub --out-dir captures/host-report-repair-t8s4-01
```

Managed reset and overwrite only8/4, as authorized. Git metadata `abaa30975d2cd2650fbce6638ef9e6d3783ad5d7`+dirty, branch unchanged. Log `session_20261011_165648.log`, report `host_midi_automation_baseline_20261011_165815.json`, metadata and invocation in that run directory.

- Report **PASS**, `overall_ok=true`, capture integrity COMPLETE, stimulus/setup confirmed, firmware_assertions_pass=true, contradiction=false, issues empty.
- Record32On/32Off/0open, firstOD16/16/0, secondOD8/8/0; phase ambiguities/diagnostics empty. FirstOD29.072183s→31.492313s; secondOD40.137195s→42.544579s.
- Persistence first_missing=None, `result=ok`, timestamp72.270574s, duration103427us, now visible in the original report snapshot. Inner report-time shutdown complete/drains2/ok; outer managed shutdown complete/drains2/ok. Latch absent; device stopped/drained.
- Record1536/1536 raw1536; stored32-note exact48-tick grid. Streamed device-time-sorted first return still32On/32Off, exact pitches48..79/ticks0..1488; max consecutive musical MO gap57.063ms. Display checks pass. No exact external-wire or painted-display parity claim.
- Phase stops remain buttons; run-end host_realtime_stop. No quarantine/SD rename/delete, other-slot clear, stimulus edits or acceptance-limit rebase.

Both requested host report defects are repaired and verified on this pinned baseline PASS. Broader S7.5b.5 — next Record-return readiness and timing/content/display acceptance remain unchecked; one baseline run does not establish all-route or envelope qualification. Durable guide, architecture execution notes, runtime status and active task evidence updated; no new DEC required for reuse of existing owners/verification contract.
