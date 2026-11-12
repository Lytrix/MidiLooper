# src — folder notes

Read before editing `main.cpp`. Rules: [CODE_HYGIENE.md § 4–5](../docs/Authority/CODE_HYGIENE.md). History behind
these lines: DEC-298 (comment history, moved) § Teensy and main in the decision log.

## Owns
- `main.cpp`: how the firmware starts (`setup`) and runs (`loop`, nine named stages), the memory regions placed by
  attribute, the module instances, the interrupts and the core's startup hooks. Nothing else.
- `Firmware.h`: the shared parts (`Firmware`: engine, clock, card, screen, surface, the last tick) and `Diagnostics`.
- Modules, each owning its state and taking only what it uses: `MidiInput`, `MidiPorts` (output and DROID feedback),
  `GpioEncoder`, `DeviceSettings` (EEPROM, wall clock), `SavedSelection`, `ScreenView`, `BootHealth` (breadcrumbs,
  watchdog, USB health). Coordinators that drive several parts: `LibraryOperations`, `SerialCommands` (the console).
- All live in `namespace firmware` (which imports `ContentEngine` for itself only).

## Invariants
- One main loop, no interrupts of its own. A tick waits at most one grain plus the non-engine stretch (B_max).
- The USB host port (the DROID) gets the surface's feedback only, never playback, and every host send is admitted
  first (`UsbHostTransmitAdmission.h`).
- Boot proof is serial: `#CAP,…,DIAG,boot_reset,crash=0` and `#CAP,…,BOOT,load,ok,…`; an uploader's `SUCCESS` is
  not proof.
- Nothing is written to the card before `!ENGINE approve-saving`, typed by the owner.
- The RTC holds local time as the Teensy loader sets it; the shown offset defaults to 0.
- Diagnostics stay off the tick path and behind a command or the session-capture gate.

## Traps
- Off-hot-path member functions are `FIRMWARE_COLD` (flash, `.flashmem.firmware`, `noclone`), never `FLASHMEM`: an
  inline member cannot share `FLASHMEM`'s section with the core's non-inline functions (a section type conflict).
- No `DMAMEM` / `EXTMEM` static inside a member function: place the buffer in `main.cpp` and pass it.
- `Firmware.h` includes `EngineRoot.h` before `ControlSurface.h`: the surface's headers rely on its includes.
- `!ENGINE end` ends an overdub at once and a Record take at the bar; a bar-quantised overdub end makes the next
  overdub `Busy`. DEC-297 (deep-overdub timing).
- `!ENGINE undo` takes a slot, not a track.
- A reset request takes effect some time after the write: nothing may run on after `quietReboot`.
- A wedged USB device keeps the loop running while the host's serial open blocks: the USB health check resets the
  board (stopped only); a PROGRAM press recovers it otherwise.

## Decisions
- DEC-254 (run-once store) · DEC-266 (watchdog and USB health) · DEC-296 (source layout) · DEC-297
