# Record-return recovery: 300 BPM external-clock HITL recipe

User explicitly authorized flashing the verified default image, managed reset, clear/overwrite only operator track8/slot4, and final Stop/drain in this session. No quarantine, SD rename/delete, or other-slot clear is authorized.

| Field | Recipe |
|---|---|
| Acceptance | Record-stop playback and first overdub keep scheduled MIDI and display updates through the first/second bar and Snapshot handoff, without the previous seconds-long gap/backlog burst. |
| Reused stimulus | `STIM-BASE-2-2-01@v1`, canonical `--preset base`; stimulus catalog listed before run. |
| Variant | No canonical edit/new stimulus; CLI tempo/clock axis reuses the same failing-leg invocation. |
| Gestures | Existing baseline action/flow owners, `scripts/hitl/actions/`, `control_constants.py`; record and overdub stop are button presses; run-end stop uses host master realtime Stop. Pre-arm setup Stop is outside measured phases. |
| Markers | Existing `MO` scheduled tick + device µs, `ST`, `RECS`, `DISP`/`DFRAME`, Snapshot units, `win_readiness`, `win_gap`, `RING,stats`; no new marker or tier registration. |
| Marker check | `check_marker_contract.py --audit`: 90 Tier-A prefixes, limit31 bytes, 0 over. Existing families reused; no new tier test needed. |
| RAM1 | Default capture SUCCESS, variables96192/code392376/padding840 (floor840). New helper logic cold/FLASH; no instrument added or budget rebase. |
| Native RED | Four registered shared-finalization cases plus five actual surgical removal controls in EXECUTION.md. Full native3060 total/3053 succeeded/7 skipped/0 failed. |
| Preflight/smoke | Non-reset ManagedCaptureSession + DeviceSession, two verified idempotent Stop/drain cycles, no content actions. Latch absent and Teensy visible before preflight. After upload attach non-reset and verify fresh boot/crash0 or live display plus verified idle before content. |
| Entry | `.venv/bin/python scripts/host_midi_hitl.py run --preset base --track 8 --loop-slot 4 --midi-channel 8 --boot-reset --capture-boot-wait 30 --clock-source host --tempo-bpm 300 --stop-after-overdub --out-dir captures/record-return-recovery-t8s4-01`; managed timeout360s. Build/upload separately after preflight. |
| Verdict | Read device-timestamp-ordered first return and first/second overdub MIDI; compare expected 16th-grid spacing (48ticks,50ms at300BPM), scheduled ticks and wall-time gaps, first-bar/second-bar output, display progress, publication timing, final drain and exact report residuals. A baseline INCONCLUSIVE/FAIL remains such even if the local gap is repaired. No claim of all-route timing qualification. |
| Evidence | Fresh metadata pins git+dirty context and flashed HEX hash; invocation/report/session log and shutdown result; stream logs bounded, never whole-read; summary in EXECUTION.md. |

Production display continuity must be observed on the modified image; no display firmware was changed by the local preparation/adoption repair. No broad task is checked solely from a successful build/native gate or an improved gap.
