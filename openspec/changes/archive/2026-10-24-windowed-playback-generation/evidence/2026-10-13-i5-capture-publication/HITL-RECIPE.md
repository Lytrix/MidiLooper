# I5 — default-image compatibility HITL recipe

| Field | Frozen recipe |
|---|---|
| Acceptance criterion | Default rolling-OFF compatibility Record/overdub/undo/redo completes with exact committed seed geometry, known intended MIDI/ledger behavior and canonical stopped persistence drain; not new-reader device qualification. |
| Reused stimulus | Catalog listed before edits. `STIM-BASE-2-2-01@v1`, authoritative `host_midi_hitl.py run --preset base`; no canonical edit or new variant. |
| Target permission | Owner explicitly approved overwrite only track 8 slots 4–5; this leg uses track8/slot4/channel8 only. No other clear/delete/rename/quarantine. |
| Gestures | Existing registered base flow and `scripts/hitl/actions/` / canonical control constants; scratch preflight calls only existing managed APIs, no hand-rolled MIDI/serial. |
| Expected evidence | Existing fresh `RECA`/`RECS`/`RID`, stored record/overdub summaries, device MIDI output, baseline report, `PERS,result`/`SAVE` terminal idle and shutdown-result. No new serial marker or tier classification. |
| Marker/tier changes | n.a.; no new instrumentation or marker. Existing marker contract only. |
| RAM1 | Default capture build passes unchanged variables96,192/code392,216/padding1,000/locals34,880; floor840, no rebase. New source behavior is rolling-gated and epoch-0-only; default device does not exercise it. |
| Native proof | Cold current publication + real TrackManager edge pass; real two-bar stop exact On2316/Off2544, root-before-edge hold and stop-release cases pass. Removal controls RED/restored. Late input and epoch1 remain expected RED. |
| Preflash/smoke | Scratch adapter reuses HitlConfig/run_with_managed_capture and existing selected-track query; no-reset, no preset/content gesture, managed attach readiness and canonical two drains. Require shutdown complete/drains2/oktrue, no latch, pinned successful build before upload. Preflight metadata candidate hash is not old installed readback. |
| Full entry | `.venv/bin/python scripts/host_midi_hitl.py run --preset base --track 8 --loop-slot 4 --midi-channel 8 --clock-source host --tempo-bpm 300 --no-boot-reset --out-dir captures/i5-cold-capture-compat-t8s4-01`; default managed timeout360s. Reboot/upload only after safe drain. |
| Verdict | Inspect actual baseline report and full-drain evidence, not exit code alone. Setup-incomplete or contradiction is not PASS; hardware default baseline cannot qualify epoch-0 new reader. No timing or OLED pixel claim inferred. |
| Evidence | Fresh invocation/metadata/session log/report/shutdown files in this run's directory. Read tail/parse summaries, never entire capture log. Pin SHA256 before/after upload and Git26125ceb+dirty. |

Status: executed. Pre/postflash managed queries and canonicaltwo-drains PASS; defaultuploadSUCCESS/post-upload HEX3168ccb1 unchanged through run. Registered compatibility baseline `captures/i5-cold-capture-compat-t8s4-01` exit0/overall_oktrue/PASS/issuesempty, critical-loss0 and finaltwo-drainscomplete/nolatch. Advisory second-overdub first-note lateness retained. Pre-upload hash check used obsolete path and failed before shell continued; no pre/post hash-equality claim. No new-reader/device-epoch/content-cycle/timing acceptance. User-approved only8/4 overwritten; no broader SD or Git action. Full record is [I5 — execution evidence](EXECUTION.md).
