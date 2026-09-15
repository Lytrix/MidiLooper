# RAM1 recovery evidence

Date: 2026-09-13. Environment: `teensy41-capture-serial`.

The capture-admission diagnostics crossed a DTCM alignment boundary. The
linker now places string-literal sections from `TrackCaptureInput.cpp.o` and
`TrackOverdubLifecycle.cpp.o` in memory-mapped flash, following the existing
`WorkspaceSave` placement rule. Mutable state and non-string constant data keep
their existing placement. This change does not move executable code.

One incremental build, `pio run -e teensy41-capture-serial -j 8`, relinked the
firmware successfully. RAM1 variables fell from 95,648 to 91,552 bytes and free
space for local variables rose from 2,656 to **6,752 bytes**, clearing the
4,000-byte floor by 2,752 bytes. RAM1 code remained 424,824 bytes with 1,160
bytes of bank padding; RAM2 variables remained 50,080 bytes.

The linked ELF confirms the alignment recovery:

- `_VectorsRam`: `0x20012000` → `0x20011800`.
- USB `periodictable`: `0x20013000` → `0x20012000`.
- `_ebss`: `0x200175a0` → `0x200165a0`.
- `_estack`: unchanged at `0x20018000`.

The 4,096-byte recovery includes avoided alignment padding; it is not the
literal byte count of the relocated strings. These are linker measurements,
not measured worst-case stack usage or proof of runtime timing. No upload or
device capture was performed for this change. The fixed-memory capacity,
grain-cost, coverage, stop-latency, and MIDI-parity gates remain open.

## Latest capture-serial build

`pio run -e teensy41-capture-serial` completed on 2026-09-13 after moving
the stop-only capture closure helpers and the out-of-line phase helper to
flash. This avoided crossing the 32 KiB ITCM bank boundary introduced by the
capture-admission path.

- RAM1: code **425,864**, variables **91,584**, padding **120**, free for
  local variables **6,720** bytes (gate: at least 4,000).
- RAM2: variables **50,080**, free for `malloc`/`new` **474,208** bytes.
- Flash: code **880,256**, data **82,948**, headers **8,568**, free for files
  **7,154,692** bytes.

This is a link-time capacity result only. It does not establish the remaining
grain-cost, stop-latency, coverage, or MIDI-trace evidence.

## Refreshed build after source-cache correction

`pio run -e teensy41-capture-serial` completed on 2026-09-13 after the
terminal NoteOff source-cache correction. The linked ELF reports RAM1 code
**425,072** bytes and RAM1 variables **91,584** bytes. ITCM allocation rounds
the code to 425,984 bytes, leaving **6,720** bytes of RAM1 local-variable
headroom against the 4,000-byte gate. RAM2 variables remain **50,080** bytes;
external RAM use is **704** bytes. No upload or device capture was performed.

## Refreshed build after cold prewarm placement

`pio run -e teensy41-capture-serial` completed on 2026-09-13 after placing
only the fixed-view allocation, release, and prewarm paths in flash. The MIDI
admission, staging mutation, and playback-time operations remain in RAM1.

- RAM1: code **425,496**, variables **91,584**, padding **488**, free for
  local variables **6,720** bytes (gate: at least 4,000).
- RAM2: variables **50,080**, free for `malloc`/`new` **474,208** bytes.
- External RAM: variables **704** bytes.
- Flash: code **882,584**, data **82,948**, headers **8,288**, free for files
  **7,152,644** bytes.

The prior build used 427,464 bytes of RAM1 code and crossed into a fourteenth
32 KiB ITCM bank, leaving only two DTCM banks and failing the local-variable
budget by 26,048 bytes. The cold placement keeps code within thirteen ITCM
banks. This is link-time capacity evidence only; it does not establish the
remaining grain-cost, stop-latency, coverage, or MIDI-parity evidence.

## Capture-overflow timestamp build

`pio run -e teensy41-capture-serial` completed on 2026-09-13 after adding the
first-eviction timestamp to the existing `RING,overflow` capture notice. The
diagnostic format string uses `PSTR` through `SC_FMT`, so it remains in flash
rather than consuming RAM1 string storage.

- RAM1: code **425,560**, variables **91,584**, padding **424**, free for
  local variables **6,720** bytes (gate: at least 4,000).
- RAM2: variables **50,080**, free for `malloc`/`new` **474,208** bytes.
- Flash: code **882,648**, data **82,980**, headers **8,192**, free for files
  **7,152,644** bytes.

This build preserves the RAM1 headroom. It has not been uploaded because the
deferred `OVERDUB_STOPPING` runtime continuation is still incomplete; a device
capture before that work would not close the feature's timing gate.

## STOPPING protection build

`pio run -e teensy41-capture-serial` completed on 2026-09-13 after extending
active-transport, persistence, and slot-selection protection to
`OVERDUB_STOPPING`. It also guards direct overdub-start entry before it can
reset frozen capture staging.

- RAM1: code **425,624**, variables **91,584**, padding **360**, free for
  local variables **6,720** bytes (gate: at least 4,000).
- RAM2: variables **50,080**, free for `malloc`/`new` **474,208** bytes.
- External RAM: variables **704** bytes.
- Flash: code **882,760**, data **83,052**, headers **9,032**, free for files
  **7,151,620** bytes.

This establishes that the protection predicates do not consume the recovered
RAM1 margin. It does not establish deferred-stop latency or canonical/audible
commit semantics.
