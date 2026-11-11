# Engine timing: budgets, measurements and how to read them

One place for the fixed-memory engine's timing: the clock it runs on, the budget each kind of work has, what was
measured on the device, and the serial lines that show it. Authority stays where each figure was decided — the
OpenSpec design ([`fixed-memory-engine/design.md`](../../openspec/changes/fixed-memory-engine/design.md), § E5, E5.2,
E5.8) and the `DEC-###` entries in [`DECISION_LOG.md`](../DECISION_LOG.md); this guide collects them and does not
override them. When a figure here and its source disagree, the source wins; update this page.

**Not the engine:** the S7.5a continuity envelope (rows L1.1–L1.11: `DIAG,midi_gap`, `envelope_instr`, `late_route`,
`env_dist`, `win_gap`; [`DECLARED-LIMITS.md`](../../openspec/changes/runtime-working-set-ownership/evidence/2026-10-11-s75a-continuity-envelope/DECLARED-LIMITS.md))
belongs to the legacy runtime. The engine firmware prints none of those rows; its instruments are below.

## The clock

| Figure | Value | Source |
|---|---|---|
| Resolution | 192 ticks a quarter note; a MIDI clock pulse is 8 ticks | `EngineClock.h` |
| A tick | 2,604 µs at 120 BPM; 1,041.67 µs at 300 BPM (the fastest case every budget is checked at) | — |
| A window | 1,536 ticks (2 bars of 4/4): the unit a track's playback is composed in, ahead of time | `EngineRoot.h` `kWindowTicks` |
| Checkpoint spacing | 4 windows (8 bars) | `kCheckpointSpacing` |
| External clock | a missing pulse is a timeout after 500 ms (`#ENGINE,clock,timeout_age_us`); a gap over 200 ms between pulses is recorded (`#ENGINE,gap`) | `EngineMain.cpp` `realtime` |
| Watchdog | 4 s without a main-loop pass resets the board; the next boot reports where it hung | DEC-266 — hardware watchdog, `!ENGINE boot` |

## The main loop and its budget

Each main-loop pass reads input, advances ticks, sends due MIDI, runs **one worker grain**, then the surface, LEDs and
screen. Playback is never composed inside a tick: it is composed a window ahead by worker jobs, so the tick path only
plays what is already composed.

| Budget | Value | Meaning | Source |
|---|---|---|---|
| **g — one worker grain** | **810 µs** | every background step (compose, sweep, cold start, save, load, fetch) is cut so one grain stays under it | E5.8, `kGrainUs` |
| Scheduler | hard jobs earliest-deadline-first; a soft grain only when every hard job keeps its slack; display ≥ 1 grain in 4 and persistence ≥ 1 in 16 while pending | E5.2, `WorkerScheduler.h` |
| Feasibility | at 50 % worker share, eight tracks of the adversarial clustered case (100 grains a window) miss no deadline; spread case C1 = 8 × 47 × 0.81 ms = 305 ms per 1.6 s | E5.8 |
| Record / launch lead | a window's composition under load: ≤ 100 grains of 810 µs at ≥ 1 grain in 2 ≈ 160 ms (≈ 160 ticks at 300 BPM), doubled | design § E9.1b — handoff without a gap |
| Card work while playing | no worse than the worst main-loop pass, about **0.9 ms** — the precision budget, not a hard deadline | DEC-254 |

The scheduler decision itself costs 4.7 µs worst with 32 jobs.

## Measured grains (device)

Compose and playback (E5.8, bench image `59c8a6f8…`, production memory placement):

| Grain | Worst | Average |
|---|---|---|
| compose, 128 row units, spread | 719 µs | 581 µs |
| compose, 128 row units, clustered | **785–800 µs** (sets g) | 562 µs |
| sweep step | 684 µs | 583 µs |
| cold-start step | 664 µs | 574 µs |
| compose seek / `begin` | 681 / 420 µs | 451 / 410 µs |
| checkpoint resume (bind / seek) | 408 / 697 µs | 402 / 686 µs |
| checkpoint capture | 68 µs | 67 µs |
| edit index build, 256 units | 398 µs | 191 µs |

Card and undo history (DEC-254; production firmware, while playing):

| Work | Worst measured | Bound by |
|---|---|---|
| autosave grain (one sector; run records and state records) | 94 µs (E4.6b), 83 µs with a demotion (E4.6c) | one sector a grain, started only when the card is ready |
| fetch of a cold undo step | 337 µs (E4.6d) | at most 4 sectors a grain |
| pressure demotion | — (no I/O) | at most one each 50 ms |
| milestone save while playing | 239 µs | DEC-258 |
| card read figures | stream 23 µs a sector; 0.24–0.75 ms to start a stream; 8 sectors ≤ 0.18 ms; a scattered sector ~0.35 ms (worst 0.9 ms) | `!ENGINE readbench` (DEC-254 stage 1) |

Allowed only with the transport stopped (boot, file creation): the boot load reads 32 sectors a grain (up to 31 ms); the
boot's one-time store rename took 43.6 ms; preallocating the library is file-system work of seconds. File-system calls
take 4–8 ms each (E9.8g), which is why nothing on the playing path uses them.

## Output

| Figure | Value | Source |
|---|---|---|
| DIN throughput | one 3-byte message per 960 µs (31,250 baud) | CE-C1 |
| Sounding notes | 16 per track per port; NoteOffs owed by the sounding table, sent first once due | CE-C1, `EngineConfig.h` |
| Lateness | NoteOn and NoteOff lateness measured separately (`maxNoteOnLateMicros` / `maxNoteOffLateMicros`) in the native suites; no device lateness row yet | design § regression metrics |

## Reading it on the device

`!ENGINE take` prints the worst values since the last `take`, then resets them:

| Line | Fields |
|---|---|
| `#ENGINE,stages` | worst µs per main-loop stage: `input`, `serial`, `ticks`, `jobs`, `grains`, `send`, `surface`, `leds`, `screen` |
| `#ENGINE,take,pass_max_us=…` | the worst whole main-loop pass; `pulses`, `timeouts`, `us_per_tick` |
| `#ENGINE,grain_worst` | the worst grain per job class: `hard`, `transition`, `display`, `persistence`, `background` |
| `#ENGINE,gap` | each clock gap over 200 ms: when, how long, passes and USB traffic in it |
| `#ENGINE,card` / `card_write` | worst card operation times; streamed vs. jumped writes, waits avoided |
| `#ENGINE,cold` / `fetch` / `runstore` | the undo history on the card (DEC-254) |

Idle baseline (`bb0d3b0a…`, transport stopped, 2026-11-11): pass 221 µs; stages serial 169, screen 151, jobs 18,
surface 15 µs; every grain class 0. A measurement while playing is the gate for any change to the playing path: compare
`pass_max_us` and `grain_worst` with and without the change on the same set.

## Measured at 300 BPM (2026-11-11, `bb0d3b0a…`)

The owner's set playing (27 slots), internal clock at 300 BPM (1,041 µs a tick), the DROID connected; per phase the
worst values of one `!ENGINE take` window. What bounds a tick's lateness is not `pass_max_us`: a pass runs up to 8
grains (`kGrainsPerPass`) and plays a tick that comes due between them, so a long `grains` stage is by design. A tick
waits at most one grain plus the longest non-engine stretch (input, serial, send, surface, LEDs, screen); the scheduler
assumes that stretch is at most **B_max = 2,000 µs** (`kBlockUs`).

| Phase | Worst grain (class) | `leds` stage | `screen` | Other non-engine | Within budget |
|---|---|---|---|---|---|
| playback | 194 µs (hard) | 12 µs | 180 µs | ≤ 160 µs | yes |
| a take (record) | 365 µs (display) | **3,643 µs** | 806 µs | ≤ 280 µs | **no** — over B_max (fixed below) |
| two overdubs | 425 µs (background) | **4,124–4,419 µs** | 803–815 µs | ≤ 290 µs | **no** — over B_max (fixed below) |
| the 6 s after each take / overdub (their saves) | 223 µs | 1,695–1,801 µs | 772–814 µs | ≤ 160 µs | grains yes; non-engine about 2.6 ms, over B_max |
| undo into a cold step (fetch from the card) | 373 µs (persistence 335 µs) | 1,831 µs | 787 µs | ≤ 180 µs | grains yes; non-engine over B_max |
| redo + saves, playback after, stop | ≤ 369 µs | ≤ 18 µs | ≤ 177 µs | ≤ 160 µs | yes |

**Findings (first run).** Every grain class stays under g = 810 µs, and the card work (saves under 100 µs, the fetch
335 µs) under its 0.9 ms budget; no clock gap. The budget that did not hold was B_max: the `leds` stage blocked for up
to 4.4 ms while a take or overdub ran and about 1.8 ms after it.

**Cause and fix (same day).** Timing the stage's parts (`!ENGINE take` with temporary timers) ruled out the DIN, USB
device and USB host sends (≤ 5 µs each, the DIN buffer never full). The time was in `Surface::leds` scanning the shown
slot's rows: the step LEDs rescanned every row of the state each time the playhead entered a bar (about 1.5 ms per
bar on a large slot), and a new state (each take, overdub wrap or commit) was scanned in one pass (about 3 ms). Now
(`Surface::beyondScan`):

- the scan of a state also builds the 16ths of each of its first 64 bars, so entering a bar reads a table;
- the LEDs and the screen advance a new state's scan by at most 512 rows a pass (`kScanRows`) and show the last state
  scanned until it is done (a few passes); gestures still finish it at once, so their values are exact.

Re-measured (`c23ede89…`, same set, 300 BPM):

| Phase | `leds` stage before → after | `screen` | Worst pass |
|---|---|---|---|
| playback | 1,500 µs → **10 µs** | 812–835 µs | 881 µs (under one tick) |
| overdub | 3,650–4,100 µs → **652–688 µs** | 872–910 µs | 1,910–2,625 µs (up to 8 grains a pass, ticks played between them) |
| after an overdub (its save) | 1,540–1,630 µs → **10 µs** | 816–836 µs | 874–886 µs |

The non-engine stretch is now at most about 1.65 ms (LEDs 688 + screen 910 + the rest ≤ 60 µs), within B_max. The
screen stage (about 0.8–0.9 ms while playing) is now the largest non-engine part. Tests:
`test_step_leds_follow_the_bars_without_a_row_scan`, `test_a_large_state_is_scanned_in_steps` (each caught by a
mutation). A step LED of a bar past the 64th still scans that state's rows (loops longer than 64 bars).

## When a figure changes

A new worst grain above g, a new kind of work on the playing path, or a card operation that can wait: re-measure
on the device, update the source (design or `DEC-###`) and then this page.
