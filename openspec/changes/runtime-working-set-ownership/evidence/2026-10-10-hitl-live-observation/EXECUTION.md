# HITL live observation and lifecycle — execution packet

## Scope and evidence

Owner requested root-cause discovery and repair of pasted run17 failure. Working branch `feature/unified-rolling-playback-path`; substantial unrelated dirty work preserved. Reuse active runtime-working-set-ownership, existing managed capture/DeviceSession, transport_clock, serial_timing/protocol and capture-only serial observation owners. No new runtime owner/state/queue or content schema; protected no-bulk-flush-during-playback policy unchanged.

VERIFIED: run17 (`29b62f4d…` image) starts transport40.411s then receives realtime Stop42.464715s; original capture releases queued40.391–42.543s playback/stop rows after Stop, ending43.387s. Later probe has35 DFRAME rows45.320–97.675s, after the original reader closed. The report's attach-only firmware stall is not established. Two record takes exist: empty setup768 ticks, actual1536 ticks/14 events. `extract_phase_sessions` selects first because control-channel note messages count as streamed notes and ranking reduces counts3/7 to booleans. Two musical On rows also precede actual recording. `ensure_transport_clock` consults generic historical proxy before caller-scoped acknowledgement. Existing capture context does send a final Stop and query heap; what it lacked was canonical persistence drain verification/latching. Edit guard waits for retained ST/DISP rows after playback starts.

## Contract frozen before edits

- Fresh acknowledgement: caller-start confirmation first uses only post-request transport edge; historical rows cannot authorize it and no second press occurs. Normal non-confirmation behavior retained.
- Take attribution: configured channel filters MI NoteOns (velocity>0), controls excluded; greatest musical count then existing armed/order tie-break. No successful-content claim or early-note tolerance is added.
- Live setup: new argument-free allowlisted `!TRACK_STATUS` snapshots selected track + sole active slot + enum state + global transport under saved/restored target interrupt mask. Its surrounding existing `!TRACE_FENCE` replies are also available live, validated32 lowercase-hex tokens, <=96-byte room guard per reply. Production dispatch shared with the native command-chain fixture; no ring enqueue/flush, content, allocation or worker. Protocol parser validates exact scalar shape/target; missing response refuses before pressing, one press maximum. Scenario parses slot/selects track first and removes a subsequent toggle-if-no-clock helper. Legacy no-target unit callers retain their row-based path; configured production guard requires managed fresh observation.
- Teardown: DeviceSession inside capture lifetime covers all managed exits, fresh idempotent Stop per cycle, independent of boot-only one-shot Stop state. Record shutdown-result.json; incomplete result latches and returns non-success; exceptions propagate after shutdown. No serial release before cleanup.
- RAM: no new resident firmware state; cold observation function/format in flash; unchanged RAM1 budget enforced. This diagnostic is not a timing guarantee for the full serial command owner.

## Historical decision review / preflight

Decision-log search: capture/flush/timing-critical serial, with current ExecutionBounds/HITL architecture and native capture-ring fixtures. No logged decision is overturned: no live bulk ring flush or new owner. Reuse existing read-only command extension and DeviceSession canonical lifecycle. No architecture reassessment/new DEC required. Existing migration/deferred tasks stay open. Scalar observation has constant work; USB backpressure refuses without enqueue or retry on firmware.

## Frozen regressions / verification

- Deterministic old-start + DFRAME fixture fails fresh-ack requirement; delayed new start control succeeds without a press.
- Two armed takes: first control notes, second7 music notes; intended later boundaries required. More musical notes wins even when both windows contain music; explicit channel16 music remains supported when configured.
- Managed fake capture closes only after two actual Stop/drain calls on success/failure/exception; incomplete drain records failure/latch, never success. Boot-dedup must not suppress teardown; failed Stop must not be cached as success.
- Guard with permanently retained ST rows uses fresh status; wrong identity/slot/global stop/malformed/absent query fails without toggle; already playing sends none; stopped sends once then observes. Parser rejects invalid scalar shapes/ranges, broker wire allowlist accepts only exact argument-free query.
- Native real capture emitter: live scalar reply does not consume existing retained ring or advance counters; no-room emits/queues nothing, later explicit retry emits fresh state. New emitter did not exist at baseline: pre-link failure is not a behavioral RED.
- Focused host/new fixtures; current full host A/B baseline `unittest discover -s scripts -p test_*.py`:1315/7F+1E (rotation case may be timing-dependent, report actual sets).
- `pio test -e native`, `pio run -e teensy41-capture-serial`, strict OpenSpec/whitespace, one focused source review. Reuse captured run17 for offline attribution replay. Device tests not authorized to overwrite slots by this request.

## HITL test recipe

| Field | Contract |
|---|---|
| Criterion | Composed seed/edit leg reaches target-qualified live state and exits stopped/drained |
| Reused stimulus | STIM-NE-RESELECT-STACKED-01@v1, authoritative host_midi_hitl.py preset note_edit_reselect_stacked |
| New stimulus | None; catalog listed before changes |
| Gestures | Existing hitl/actions and midi_io helpers; no hand-rolled MIDI bytes |
| Observation | #TRACK_STATUS,us=...,track=...,slot=...,state=...,transport=...; request-only, direct with96B room, no ring retention |
| Tier registration | N/A: direct fenced command response like #STOP_STATUS, not a spontaneous #CAP/Tier-A marker |
| RAM1 | No state addition; flash format/function; build floor840 unchanged |
| Native oracle | test_capture_ring direct status and no-room refusal fixtures |
| Device scope | No reset/upload/content overwrite performed; need explicit scratch-slot permission for rerun |
| Verdict | Fresh exact target PLAYING+transport1; missing/wrong response fails setup; content/selection remains strict |

## Completion

- Baseline: full host1315 tests/7F+1E; new fixed-oracle regressions11 cases yielded9 behavioral failures,1 new API absence error and1 positive control. Review framing regression independently failed before its repair.
- Final focused host:100/100 (19 new cases), including stale-start/DFRAME denial, channel16 explicitly configured music, take ranking/zero velocity, success/failure/exception/watchdog cleanup, failure propagation/cache, exact target guard/status parser/broker sequence.
- Full host A/B: final1334 tests/7F+1E; exact FAIL/ERROR name sets identical to baseline, no new failures. An intermediate1331 run had6F+1E because the pre-existing rotation test passed once; do not credit that flake as a repair. Logs `hitl-repair-host-baseline.log`/`hitl-repair-host-final2.log` in session scratch.
- Native: capture-ring20/20 (4 new emitter/production-dispatch cases), full3041 total/3034 succeeded/7 skipped/0 failed,112 PASSED suites. `pio test -e native` exit0; final log `hitl-repair-native-final.log`.
- Firmware: `pio run -e teensy41-capture-serial` SUCCESS/exit0; unchanged RAM1 variables96,192/code392,360/padding856/floor840/locals34,880; RAM2163,360, EXTRAM9,312, no rebase. Final HEX SHA-256 `c2dee3be8b2f1c54b4855678331b6c65704d3931cc1a55eca7a6b31229562c88`, NOT uploaded. Flash placement: emitTraceFence0x600884f0/92B, emitTrackStatus0x6008854c/64B; both command tokens/formats flash-resident. Compiled command path snapshots under PRIMASK then restores before emitTrackStatus.
- Offline run17 replay now selects17.868776–21.776557s actual take, six channel8 note-ons within that window, setup0; it does not claim eight-note parity and does not hide the two early On rows or remaining unmatched Off.
- One focused review returned REQUEST CHANGES: stopped-only fences made live status unfenced. Repaired by the existing serial-owner live dispatcher and bounded validated direct fence emitter; new broker-wiring RED→GREEN plus production native stopped→playing framing/backpressure/token fixtures pass. No second review or blanket APPROVE claimed.
- Strict active OpenSpec validation and whitespace checks pass. Local two tasks eligible for checkoff; registered device/content/selection gate stays unchecked. No device/serial/MIDI/upload/reset/slot-clear/SD action in this repair session, no stimulus mutation or Git delivery.
- Session closeout: docs/runtime/current work/deliverable/guide/seed journal updated. Restores existing observation/cleanup responsibilities, no new owner or runtime policy decision; no DEC append. Overlap/ledger/playback-order unchanged. Actual fresh-start pacing/full note-edit run remains hardware-unverified until authorized scratch-target rerun.

## Authorized upload/rerun follow-up (2026-10-10)

Owner approved upload and overwrite only track8/slot4. Pinned `c2dee3be…` uploaded unchanged after verified preflight; authoritative registered run `captures/hitl-live-observation-t8s4-01` exited1. Fresh crash0 boot,8 On/8 Off/0 unmatched, valid fenced STOPPED→PLAYING responses on exact7/3 prove command/framing hardware paths, but seed2304 ticks instead of1536/grid6 bad deltas/late17-clock first note, base persistence-result observation gap, and guard SETUP_INCOMPLETE despite later valid PLAYING reply remain. Canonical terminal shutdown complete/2/ok, no latch. Hardware task NOT checked off. No repeated upload/reset/overwrite, other-slot or SD-destructive action or code mutation. [Device result and remaining blockers](DEVICE-RUN.md).
