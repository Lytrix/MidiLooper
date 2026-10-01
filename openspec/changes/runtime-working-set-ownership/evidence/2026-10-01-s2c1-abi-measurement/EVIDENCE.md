# S2.1 — measuring the `P` term's inputs on the target ABI (2026-10-01)

**Why this exists.** `S2C1-PLAYBACK-WINDOW-LEASE.md` derives the playback window lease from the resolved window,
and its inputs are byte sizes. The host compiler is the **wrong ABI** for them: `MidiEvent` carries
`const uint8_t* sysexData` (`include/MidiEvent.h`), so its size differs between the two. Measuring on the host
would have inflated **every** `P` figure by 34 %. These are the commands and the raw output the artifact's tables
come from, so the numbers can be re-derived rather than trusted.

## 1. Host (x86-64) — recorded as the trap, not as the answer

```
g++ -std=c++17 -DPIO_UNIT_TEST_NATIVE -I include -I test "$PI_SCRATCH_DIR/sizeof_probe.cpp" -o probe && ./probe
```
```
MidiEvent=32 PlaybackPassEventOrigin=4 PlaybackBridgeRecord=40
FixedPlaybackEventStorage=56 RuntimePlaybackView=2224 RuntimePlaybackViewSet=38832
kViewCount=3 kEventsPerView=2048 kBridgeCount=2 kRecordsPerBridge=2048 kCaptureStagingEvents=2048
```

## 2. Device (cortex-m7) — the figures the artifact uses

`abi_probe.cpp` instantiates `Reveal<sizeof(T)>` for an undefined template, so the compiler prints the value in
its diagnostic. It needs no runtime, no link and no upload; `ProbeSerial` only satisfies the `Serial.print`
calls in `MidiEvent`'s non-native debug branches, and it must be declared **before** `MidiEvent.h` is included.

```
TC=~/.platformio/packages/toolchain-gccarmnoneeabi-teensy/bin/arm-none-eabi-c++
FW=~/.platformio/packages/framework-arduinoteensy
$TC -std=gnu++17 -mcpu=cortex-m7 -mfloat-abi=hard -mfpu=fpv5-d16 -fsyntax-only \
    -Iinclude -I$FW/libraries/MIDI/src evidence/2026-10-01-s2c1-abi-measurement/abi_probe.cpp
```
```
aggregate 'Reveal<20> sizeof_a' has incomplete type and cannot be defined      # sizeof(MidiEvent)
aggregate 'Reveal<4> sizeof_b' has incomplete type and cannot be defined       # PlaybackPassEventOrigin
aggregate 'Reveal<28> sizeof_c' has incomplete type and cannot be defined      # PlaybackBridgeRecord
aggregate 'Reveal<28> sizeof_d' has incomplete type and cannot be defined      # FixedPlaybackEventStorage
aggregate 'Reveal<2168> sizeof_e' has incomplete type and cannot be defined    # RuntimePlaybackView
aggregate 'Reveal<33024> sizeof_f' has incomplete type and cannot be defined   # RuntimePlaybackViewSet
```

`sizeof(MidiEvent) = 20` is independently corroborated by `docs/Authority/Architecture/ExecutionBounds.md`,
which states it in its own text for a *different* derivation (`canonicalPassInputs` = 147,456 B at C = 2048).

**Scope of the claim.** These are compile-time facts of the same headers, with the m7 ABI flags
(`-mcpu=cortex-m7 -mfloat-abi=hard -mfpu=fpv5-d16`) and the Teensy framework's MIDI library — the one the device
build resolves `<MIDI.h>` to (`framework-arduinoteensy/libraries/MIDI/src`, **not** the mbed-flavoured copy
under `.pio/libdeps/`). The firmware build's full `-D` set is not reproduced, and none of the six measured types
depends on it: they are plain data members (`uint32_t`, `uint16_t`, `uint8_t`, one pointer, two 2-byte structs).

## 3. The arithmetic (re-deriving `fixedExternalBytes()` from those sizes)

```
per-event, view     = 20 + 2 + 4 + 2 + 1 + 4 + 2 = 35 B      (design.md's own §Sizing figure)
per-event, staging  = 20 + 2 + 4 + 2 + 1         = 29 B
views (3)     215,040 B = 210.0 KiB
bases (2)     143,360 B = 140.0 KiB     -> views+bases 358,400 B = 350.0 KiB
bridges (2)   368,640 B = 360.0 KiB
stagings (2)  123,904 B = 121.0 KiB     -> 61,952 B each = 59,392 + 128 x 20
whole-take    850,944 B = 831.0 KiB     windowed-only 338,944 B = 331.0 KiB
```

Cross-checks against statements that already exist in the tree — `ExecutionBounds.md`: views + bases **350 KB**,
bridges **360 KB**, `captureStagings[2]` **59,392 + 128 reservations = 61,952 each**, `canonicalPassInputs`
**147,456 B at C = 2048** (`2048 × (2·20 + 2·8 + 8 + 4 + 2·2)`; note `sizeof(const EditPass*)` is **4** on this
ABI, which is what makes that sum match). `design.md`: **35 B/event**. All exact.

Derived capacities from the window (`design.md` densities: typical ~72 events/bar, representable ceiling
4,096 refs/bar):

```
1/4 bar:   c=18   -> 8,054 B (7.9 KiB)   |  c=1,024 -> 172,032 B (168 KiB)
1 bar:     c=72   -> 16,856 B (16.5 KiB) |  c=4,096 -> 672,768 B (657 KiB)
2 bars:    c=144  -> 28,592 B (27.9 KiB) |  c=8,192 -> 1,340,416 B (1.28 MiB)
today:     c=2048 -> 338,944 B (331.0 KiB)
```
