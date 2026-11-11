# Playback — folder notes

Read before editing a file in this folder. Rules: [CODE_HYGIENE.md § 4–5](../../../docs/Authority/CODE_HYGIENE.md).
History behind these lines: DEC-298 (comment history, moved) § Playback in the decision log.

## Owns
- `TrackControl`: gestures and live input per track (queues, Record / Stop timing, pre-roll, launches, set switches).
- `SlotTransport`: the gestures that write — Record, overdub, Stop, undo / redo, Clear, copy, get a loop, loop length
  and start, groove publish, audition. `PlaybackTrack`: one track's windows and source transitions.
  `PlaybackJobs`: compose work as scheduler jobs. `TerminalScheduler`: NoteOffs and sounding limits per track.
  `ClockOutput`, `MidiOutputHub`: the Clock path's output to the ports.

## Invariants
- A gesture at tick `clock` takes effect before that tick is played; input at `clock` arrives after it. Queued
  gestures run in `beforeTick`, never on the tick they were queued.
- One writer: a second Record or overdub while one is writing is refused (`Busy`) or stays queued.
- An overdub publishes at every non-empty wrap; a run still closing is completed in place by a new manifest with the
  same runs, so a manifest's content never changes under its identity (checkpoints and tiles rely on it).
- Handoff without a gap: the next cycle's window 0 is composed before the boundary; a provisional source is never a
  history state, checkpointed or tiled.
- A Record take's rows are immutable: a trimmed Stop keeps the rows past the loop end as its wrap stream; pickups are
  a second run written at Stop.
- One sounding slot per track. Clear is an undoable empty state. Thru is never gated by mute.
- A started instance always gets its NoteOff; nothing is sent into the past; at most T = 16 sound per track.
- The loop wrap (`nextCycle`) is a grain of its own (DEC-297).

- A gesture that writes a state goes through `slotBusy`, `fittedState` (content runs + a fit run → a new state) and
  `commitState` (adopt, then the slot's playing tracks follow): a new gesture reuses them rather than a copy.

## Traps
- `!ENGINE end` must end an overdub at once, as REC does (`stopNow`); ending it at the bar leaves the writer busy and
  the next overdub is refused (`Busy`). A Record take still ends at the bar. DEC-297 (deep-overdub timing).
- Don't publish a state while its run is still closing as a new step: completions replace states in place.

## Decisions
- DEC-033 (overlap ignores channel) · DEC-038 (overdub wrap commit) · DEC-244 (equal-tick order) ·
  DEC-254 (per-slot undo) · DEC-257 (loop length is a play window) · DEC-260 (live load) ·
  DEC-261 (gapless live switch) · DEC-262 (late-Stop hide) · DEC-270 / 271 / 272 (get a loop, audition, preset) ·
  DEC-274 (launch quantisation) · DEC-275 (step restart) · DEC-276 (external-clock local stop / start) ·
  DEC-287 / 288 (groove pass, background build) · DEC-297 (deep-overdub timing)
