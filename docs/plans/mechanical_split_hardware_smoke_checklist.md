# Mechanical split — hardware smoke checklist

**When:** First large behavior-preserving TU split with a **new agent model** or **new domain folder** in a session. Not a substitute for full [HITL edit baseline](.cursor/rules/HITL-Edit-Test-Flow.mdc) when commit/stop paths change.

**Build:** `teensy41-capture-serial` already green per phase before smoke.

## Minimal smoke (~5 min)

1. Boot — device enumerates; `#CAP` or serial heartbeat visible.
2. **Record** — 1–2 bars on one track/slot; stop; loop plays back notes.
3. **Slot** — switch slot or track; no hang.
4. **Domain-specific** (pick one):
   - **Loop / capture:** second overdub optional.
   - **NOTE_EDIT:** enter edit, move one note, exit without crash.
   - **Storage:** slot load after boot (if load path touched).

## Pass

- No silence after stop, no USB/MIDI drop, no watchdog reboot.
- Capture session log saved if `#CAP` enabled.

## Fail

- Bisect with per-phase commits; do not batch further phases until green.
