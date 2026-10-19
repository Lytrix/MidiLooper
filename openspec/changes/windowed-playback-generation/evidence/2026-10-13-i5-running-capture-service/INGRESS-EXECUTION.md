# I5 — receiver ingress receipts execution

Owner approved implementing timestamp metadata at existing USB-device/USB-host/DIN receive queues using minimal project-owned, source-pinned integration; installed dependencies remain read-only. No new payload queue/transport ordering, target continuation/default rolling activation, upload/HITL/SD or Git action.

Criterion: exact generation-qualified payload/receive-observation correspondence through original queue insertion/dequeue, reset/overflow/reconnect and MidiHandler ordinary/retained dispatch. Receive observations are not sender times or physical deadline qualification. [Physical-reference evidence](PHYSICAL-REFERENCE-CONTRACT.md) supplies the completed source audit; independent elapsed source and arrival-delay bounds remain missing.

Owner: existing transport queues own metadata; MidiHandler owns retained copies. New receipt instrumentation is gated defaultOFF. Cold external-first, non-aborting provisioning only; six USB transfer cells,64 Serial8 byte cells,400 host word cells, no receive/dequeue allocation. Unsupported capacity/source/reset/exhaustion/missing metadata refuses provenance but preserves payload. Timestamp kind distinguishes sampled independent64bit time, rawmicros32 diagnostic observation, missing observation and late direct-FIFO observation. No widening micros32 into qualified64bit time. No numeric delay/WCET budget adopted.

Hook ABI uses route0 USB-device/1 DIN/2 host, owner identity and exact original slot. Queue reset/invalidate advances generation; packet begin captures once; publish records receipt/offset/extent before payload index publication; consume copies matching receipt before slot release and never restamps it. Last-receipt clear occurs at read entry. DIN overflow invalidates provenance without changing existing byte behavior; directFIFO and empty peek paths receive explicitly later-observation metadata. Host disconnect retains payload but invalidates old receipt currency. Application callbacks are dequeue, not ingress.

Primary files: project hook/receipt headers and implementation; pinned source transformation/build middleware and host tests; MidiHandler/qualification continuation + isolated native fakes/receive-boundary fixtures. Source transformation verifies exact hashes/unique anchors, preserves licenses, emits generated build copies, and never writes installed dependencies. Default builds compile original sources.

Oracle: controllable receiver-time source independent of logical Clock/input time; actual instrumented receive/dequeue method bodies where buildable with platform-boundary fakes; ordinary/qualified real MidiHandler route/transport grouping/exactonce with receipt copies. Reused slots/same-payload bytes, partialUSB packets, parser partial/running-status/realtime association, reconnect, overflow, capacity mismatch, zero clock, generation/sequence exhaustion, cold provisioning refusal and no hotallocation. Missing/late metadata must never become a physical readiness pass. Mutation controls remove a hook or retained copy and fail the intended association invariant.

Verification: focused native/host development; one focused implementation review; actual receipt/store/retained-state ABI and defaultcapture/profile build/RAM1 with gate/default symbol checks; fullnative and strict/link/diff at closeout. No hardware source/USB-IRQ/sender age or28tick readiness claim. Parent tasks remain open until their own full criteria pass.

## Implemented correspondence prerequisite — 2026-10-13

DEC-125 — pinned receiver ingress receipts selects insertion-only, source-hash-guarded generated copies of Teensyduino1.159.0 USB-device, HardwareSerial and USBHost MIDI sources. `scripts/midi_ingress_build.py` validates every source before generation, preserves original include directories and object targets, and never edits installed dependencies. `teensy41-capture-serial-ingress` opts in; the default compiles originals and has no receipt/continuation/test-observer symbols. The ingress budget entry aliases the existing capture limits; no numerical budget or size pin changed.

The startup-cleared pointer publishes a fully constructed, cold non-aborting external-first store. IRQ-mask restoration preserves the incoming PRIMASK. No hook/read/dispatch allocates metadata. Unsupported owner/capacity, missing hooks, terminal sequence/generation and unknown counter observations decline provenance without suppressing accepted payload. Queue reset, DIN overflow, host disconnect and sampler replacement invalidate currency. Old acquired/retained observations are preserved, not refreshed. Direct-FIFO/empty-peek observations carry LateHardwareRead. DIN parser messages inherit the completing byte's IRQ/FIFO observation, **not** the first byte or whole-frame arrival time. Sampler-era changes remain the future reference owner's responsibility; counter observations are not arrival-delay qualification.

### Production-path fixtures and controls

- Store suite: **9/9**; cold refusal/recovery, USB offsets,800 host turnovers, owner/capacity mismatch, reset/overflow/disconnect, rawmicros wrap, lateFIFO, invalid offsets/slots/clear, terminal identities, sampler replacement and unknown era.
- Actual receiver/parser suite: **14/14** through generated upstream bodies: USB rx_event/read_message, host rx_data/read/disconnect/sysex_byte, DIN IRQHandler/read/peek/clear plus pinned MIDI parser. Covers packet partial reads/trailing-byte truncation, transfer/ring wrap and slot reuse, zero filtering, host backpressure/resumption, partialSysEx/realtime, DIN full drop/clear/directFIFO/peek, partial/runningstatus/realtime/SysEx completing-byte association, wrong owner/capacity and original unhooked USB negative control. Configure/claim/begin/end/addMemoryForRead reset hooks are hash/anchor/build checked; native reconnect reset uses the hook directly, not complete controller enumeration.
- Real MidiHandler suite: gate-on **17/17 ordinary** and **19/19 native retained**. Native-only observer records every payload/receipt pair. USB/DIN dispatch Continue(200),Clock(100),Clock(300); host preserves Clock(100),Continue(200),Clock(300). Poll time99999 cannot refresh observations. Retained invalidation preserves exact accepted copies but removes currency; the original Capture/Clock/reader oracles remain green.
- One focused independent review found no production defect and one ordinary-fixture coverage gap. Added full ordered payload/receipt assertions. First ordinary receipt removal leaves final receipt intact but fails **2 USB/DIN assertions**; restored original Edit anchor. Retained-copy removal fails **3 all-route assertions**, restored original Edit anchor. Delegate scratch-only USBpublish/hostconsume/DINoverflow removal controls reported **5/4/1 failures**, restored; delegate's receiver-body Unity/ASan/UBSan run reported12/12 before the two parser additions. No additional review loop.
- Host checks: **25 transform/extraction/C-C++ preprocessing/compiler tests +5 middleware tests**, all pass. Version/hash/anchor/double-instrumentation drift fails closed, last-source drift produces no copies, originals/licenses/payload operations preserved. Installed receiver and parser hashes independently rechecked unchanged.

### Actual resource evidence

Compiler ABI probes: receipt32B/alignment8, cell40B, State**19,168target /19,192host**, enabled native continuation**5,152B both** versus1056B gate-off. Ordinary batch row40B versus8B:128 rows add4096B stack payload, not a new queue. Target pointer is4B in startup-cleared BSS at20016c18. The aggregate linker report rounds/alignment leave RAM1 variables unchanged; that does not mean the pointer or cold allocation is free. No RAM1/PSRAM readiness allocation inferred.

Enabled GCC `-fstack-usage`: input5352B, dispatch680B, handler56B, packet/direct160B each, observe24B; original instrumented USBreceive32/dequeue24, DINIRQ56/read24/peek24, hostreceive40/read40/disconnect8. Individual compiler frames exclude callee/nesting/exception/sampler costs; no whole-stack or WCET proof. Sampler executes inside the IRQ mask and must be bounded/nonallocating before production installation. Cold State construction clears all470 cells once, never per IRQ or setup reuse.

First enabled link was rejected at padding408<840; input-only dispatch helpers were gate-qualified FLASH-placed, **not** a relaxed floor. Final default capture SUCCESS: RAM1variables96192/code392216/padding1000/locals34880; ingress SUCCESS: same variables/locals, code391912/padding1304. Both keep12ITCMbanks/floor840, RAM2variables163360/EXTRAM11680. No target continuation or rolling activation; target continuation flag1 remains compile-rejected even with receipts on. `main.cpp`/`ClockManager.cpp` hashes match pre-slice values.

### Final commands and evidence

`pio test -e native`: **3161total/3154passed/7skipped/118PASSED suites/0fail**. Both gate-on input profiles rerun after controls/review fix:17/19passed. `scripts/test_midi_ingress_sources.py` and `scripts/test_midi_ingress_build.py`:25/5passed. `pio run -e teensy41-capture-serial` and opt-in ingress build:SUCCESS74.988s/74.657s. Full native289.505s. Logs/ABI probes/target-refusal/stack reports are in session scratch under `i5-ingress-*`; no hardware execution or sender/USBIRQ delay bound measured. Strict OpenSpec/link/diff/progress checks are recorded at session closeout.

### Closeout and remaining criterion

Correspondence integration is implemented and locally verified. [Operational guide](../../../../../docs/Guides/MIDI_INGRESS_RECEIPTS.md) describes the opt-in contract. Prior 'integration policy unselected' notes are historical and superseded by owner approval/DEC-125 — pinned receiver ingress receipts. Session-closeout template applied through decision/runtime/design/task/delivery/handoff updates. No upload/HITL/device/SD/Git/legacy removal; parents3.15a–f/3.10/3.7b remain unchecked and progress29/60. No complete successor at740→768, physical producer/arrival-correlation/delay/IRQ health, enforced owner-to-input cost, live source lifetime/mutation/stop receipt, forward source cursor/request/initial running anchor or all-route reader qualification claim.

### Repeatable profiles / final closeout checks

```sh
pio test -e native -f test_midi_ingress_receipts
pio test -e native -f test_midi_ingress_transport
PLATFORMIO_BUILD_FLAGS='-std=gnu++17 -D PIO_UNIT_TEST_NATIVE=1 -I include -D MIDI_INGRESS_RECEIPTS_ENABLED=1' pio test -e native -f test_midi_input_clock_integration
PLATFORMIO_BUILD_FLAGS='-std=gnu++17 -D PIO_UNIT_TEST_NATIVE=1 -I include -D MIDI_INGRESS_RECEIPTS_ENABLED=1 -D UNIFIED_INPUT_CONTINUATION_ORACLE=1' pio test -e native -f test_midi_input_clock_integration
PYTHONDONTWRITEBYTECODE=1 .venv/bin/python scripts/test_midi_ingress_sources.py
PYTHONDONTWRITEBYTECODE=1 .venv/bin/python scripts/test_midi_ingress_build.py
pio test -e native
pio run -e teensy41-capture-serial
pio run -e teensy41-capture-serial-ingress
openspec validate windowed-playback-generation --strict
git diff --check
```

Final parent scratch standalone build uses `-fsanitize=address,undefined -fno-omit-frame-pointer` and the README generation/include recipe: **14/14PASS, no sanitizer diagnostics**, including both new actual-parser cases (`i5-ingress-sanitizer-final.log`). Strict OpenSpec validation PASS; new guide/fixture/evidence local links resolve, new fixture whitespace and whole tracked diff checks PASS. Two Markdown hard-break trailing spaces were removed from the decision entry before the final diff PASS. Final apply remains ready/progress29of60, unchanged. Session-closeout checklist complete; no archive/commit/push/PR/upload/device/SD operation.
