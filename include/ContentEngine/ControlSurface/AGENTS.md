# ControlSurface — folder notes

Read before editing a file in this folder. Rules: [CODE_HYGIENE.md § 4–5](../../../docs/Authority/CODE_HYGIENE.md).
History behind these lines: DEC-298 (comment history, moved) § ControlSurface in the decision log. The user-facing
behaviour is `docs/Guides/ENGINE_UI_UX_GUIDE.md`; the DROID patch is `droid/midilooper_v1.ini`.

## Owns
- `ControlSurface`: the DROID control plane (MIDI channels 13–16) → gestures, windows (GROOVE, length popup, copy,
  sets browser), LEDs and motor faders; its bodies are split by responsibility into `ControlSurfaceGroove.h`,
  `ControlSurfaceLoopEdit.h`, `ControlSurfaceCopy.h` and `ControlSurfaceLeds.h` (declarations stay in the class).
  `PressDetector`: short / double / triple / long presses. `NoteEditSurface`: NOTE_EDIT's faders, buttons and encoder. `SetBrowser`: the
  sets browser's model. `UsbHostTransmitAdmission`: whether a USB host write returns at once.

## Invariants
- The gesture map is the DROID patch's: a change here is a change to `droid/midilooper_v1.ini` too.
- The control plane is never recorded and never thru.
- A long press always exits a window. Trash is destructive and immediate (a triple TRACK brings it back).
- Step and bar buttons are absolute places in the stored loop (DEC-283), in performance and LOOP_EDIT.
- One undo step per edit visit (LOOP_EDIT, the length popup, a NOTE_EDIT note's changes), not per fader move.
- `leds` sends only changes, at most `max` a call; a new state's rows are scanned `kScanRows` a pass (256 LEDs, 128
  the screen) and the step LEDs read the per-bar table (DEC-297).
- The USB host port is written only when `hasRoom` says the write returns at once; otherwise the message waits or is
  dropped (counted). USBHost_t36's `write_packed` spins forever on full buffers.

## Traps
- Motor faders: a written fader's travel is not a move (`landing`, `kMotorSettleMs`), and neither is where it comes to
  rest — the DROID's motors land about 2.5 % off. The first report as a bank opens is not a move (`faderSeen_`).
- The DROID repeats a fader at rest and jitters by a few values: act only past a deadband, or a select at a 16th the
  moved note has left drops the selection.
- Don't write F3 on every F2 step, nor F2 after an F3 move: a landing read as a move moves the note a 16th on.
- The DROID switches fader banks on a program change: NOTE_EDIT must send its own bank's messages, not the loop-edit
  bank's.
- Channel 15 notes 1 / 2 are both F3 / F4's `clear` and the 16th-step LEDs 2 / 3: a stale stored start value sends
  F4 to an old pitch when those LEDs change, so a moved fader's position is sent back as its stored value.
- A notch number sent as is saturates at CC 127 past notch 0: send `round(notch × 127 / 24)`.
- The DROID sends PLAY/STOP as a short-press trigger only: the GROOVE window opens on note 43 (the patch's long
  press) or PLAY/STOP with NOTELEN held.
- LED levels below 32 do not show on the DROID (8 and 20 are invisible).
- Opening and closing the GROOVE window without a change must not write the columns (it made a stray undo step).

## Decisions
- DEC-257 (play window) · DEC-263 (sets browser on the DROID) · DEC-272 (get-a-loop picker) · DEC-276 (external-clock
  local stop / start) · DEC-279 (copy / paste) · DEC-280 / 281 (LOOP_EDIT ranges) · DEC-283 (absolute buttons) ·
  DEC-284 (HOME and variation) · DEC-289 / 292 / 294 / 295 (the GROOVE window) · DEC-297 (deep-overdub timing)
