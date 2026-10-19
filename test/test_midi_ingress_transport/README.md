# Native actual-body MIDI ingress transport fixture

`test_main.cpp` enables receipts, includes the real receipt implementation and executes generated brace-inclusive upstream bodies. `platform_fakes.h` supplies only queue storage/declarations, transfer submissions/debug sinks, callback boundaries, IRQ stubs and UART registers. No receiver/dequeue/overflow/backpressure state machine is authored in the suite. `din_parser.inc` uses the actual pinned MIDI library over the actual UART dequeue body.

## Registered entry point

`pio test -e native -f test_midi_ingress_transport`

`scripts/native_test_link.py` invokes `midi_ingress_native_fixture.generate`, then prepends generated/suite/parser include directories. The native platform reads the already installed Teensy framework; `MIDI_INGRESS_FRAMEWORK_ROOT` may select another read-only copy of the pinned package. All receiver hashes/anchors and parser hashes are checked before any fixture file is emitted under BUILD_DIR. Do not separately link `MidiIngressReceipt.cpp` or enable `MIDI_INGRESS_STANDALONE` for PlatformIO/Unity.

## Standalone development

```sh
FRAMEWORK="${MIDI_INGRESS_FRAMEWORK_ROOT:-$HOME/.platformio/packages/framework-arduinoteensy}"
python3 scripts/midi_ingress_native_fixture.py \
  --framework-dir "$FRAMEWORK" --build-dir "$PI_SCRATCH_DIR/ingress-build"
c++ -std=c++17 -Wall -Wextra -DMIDI_INGRESS_STANDALONE \
  -Itest/test_midi_ingress_transport -Iinclude \
  -I"$FRAMEWORK/libraries/MIDI/src" \
  -I"$PI_SCRATCH_DIR/ingress-build/midi_ingress_native_fixture" \
  test/test_midi_ingress_transport/test_main.cpp \
  -o "$PI_SCRATCH_DIR/ingress-build/tests"
"$PI_SCRATCH_DIR/ingress-build/tests"
```

Intermediates stay outside the authored tree. `usb_missing_bodies.inc` contains original uninstrumented USB bodies for the missing-receive-hook control.

## Coverage and limits

14 tests cover USB partial/trailing-byte packets, transfer FIFO wrap/zero length/slot reuse/reset/missing hook; host FIFO/zero suppression/Note-Clock ordering/partialSysEx-realtime/backpressure/resumption/wrap/filter/disconnect; DIN IRQ ring/full drop/peek/clear/directFIFO/late peek; raw wrap/capacity/owner refusal; and the actual DIN parser's partial/running-status/realtime/SysEx completing-byte receipt association. Receive and dequeue observation times are independently controlled.

Reconnect's reset hook is exercised directly. `claim`, USB `configure`, Serial `begin/end/addMemoryForRead` are hash/anchor/build checked but not extracted/executed. Controller enumeration, hardware IRQ concurrency/timing, bus/sender age, platform counter, WCET and physical deadline qualification are not proved. Platform callback/TX branches outside the fixture fail closed; cold prewarm/exhaustion/retained dispatch are in sibling receipt/application suites. Target resource measurements belong to the ingress execution evidence.

Delegate development controls deleting USB publish/host consume/DIN overflow invalidation in scratch produced5/4/1 failures. The parent registered14/14native run includes the later parser cases. See [I5 — ingress evidence](../../openspec/changes/windowed-playback-generation/evidence/2026-10-13-i5-running-capture-service/INGRESS-EXECUTION.md) for final checks and exclusions.
