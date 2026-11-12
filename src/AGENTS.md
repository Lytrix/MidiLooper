# src — folder notes

Read before editing `main.cpp`. Rules: [CODE_HYGIENE.md § 4–5](../docs/Authority/CODE_HYGIENE.md). History behind
these lines: DEC-298 (comment history, moved) § Teensy and main in the decision log.

## Owns
- `main.cpp`: the engine firmware's one implementation file — the main loop (input, due ticks, jobs and grains, port
  output, the control surface, the screen), the serial commands (`!ENGINE …`, `!STOP_STATUS`, `!REBOOT`), boot,
  breadcrumbs, the watchdog and the USB health check, wall-clock time. Two sections live in parts included in place:
  `LibraryOperations.h` (milestones, loads, get a loop, the picker, the working set) and `SerialCommands.h`.

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
- `!ENGINE end` ends an overdub at once and a Record take at the bar; a bar-quantised overdub end makes the next
  overdub `Busy`. DEC-297 (deep-overdub timing).
- `!ENGINE undo` takes a slot, not a track.
- A reset request takes effect some time after the write: nothing may run on after `quietReboot`.
- A wedged USB device keeps the loop running while the host's serial open blocks: the USB health check resets the
  board (stopped only); a PROGRAM press recovers it otherwise.

## Decisions
- DEC-254 (run-once store) · DEC-266 (watchdog and USB health) · DEC-296 (source layout) · DEC-297
