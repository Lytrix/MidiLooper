# Timing — folder notes

Read before editing a file in this folder. Rules: [CODE_HYGIENE.md § 4–5](../../../docs/Authority/CODE_HYGIENE.md).
History behind these lines: DEC-298 (comment history, moved) § Timing in the decision log.

## Owns
- `EngineClock`: which tick is due (internal tempo or external MIDI clock). `WallClock`: RTC dates for saves and
  milestones.

## Invariants
- Tick numbers only rise and a running clock never skips one; a late clock catches up `kCatchUp` ticks a pass.
  Tracks, jobs and queued gestures key on tick numbers.
- The clock never runs ahead of an external source (at most 8k + 7 before pulse k + 1 arrives).
- A start goes to the next bar line, never back to 0.
- Tempo range 1–300 BPM; an estimate outside it is ignored (a pulse flood must not set a tempo).
- A `WallClock` time outside [`kFirstValid`, `kLastValid`] is shown as `TIME NOT SET`, never replaced.

## Traps
- Don't stop or freeze the clock when external pulses pause: a running clock continues at the last external tempo
  and follows the pulses again when they return. A half-second source gap once froze a take.
- Don't derive tempo from single pulse intervals: USB delivers pulses bunched (intervals of 0); the tempo is the span
  of the last 24 intervals.
- Don't substitute the build time for an unset RTC: it makes dates look real.

## Decisions
- DEC-108 (tempo fader range) · DEC-276 (external-clock local stop / start) · DEC-298 (comment history, moved)
