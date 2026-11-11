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

## Measured at 300 BPM (2026-11-11)

The owner's set playing (27 slots), internal clock at 300 BPM (1,041 µs a tick), the DROID connected; per phase the
worst values of one `!ENGINE take` window. What bounds a tick's lateness is not `pass_max_us`: a pass runs up to 8
grains (`kGrainsPerPass`) and plays a tick that comes due between them, so a long `grains` stage is by design. A tick
waits at most one grain plus the longest non-engine stretch (input, serial, send, surface, LEDs, screen); the scheduler
assumes that stretch is at most **B_max = 2,000 µs** (`kBlockUs`).

Current firmware (`c23ede89…`, after the LED fix below). *Non-engine* is the sum of the worst input, serial, send,
surface, `leds` and `screen` stages in the window: an upper bound on how long a tick can wait besides one grain.

| Phase | Worst grain (class) | `leds` | `screen` | Serial | Non-engine (≤ 2,000 µs) | Worst pass | Within budget |
|---|---|---|---|---|---|---|---|
| playback | 368 µs (background) | 10 µs | 812–835 µs | 150–161 µs | 1,040–1,050 µs | 881–1,289 µs | yes |
| overdub (2 runs) | 414 µs (transition) | 652–688 µs | 872–910 µs | 145–149 µs | 1,786–1,828 µs | 1,910–2,625 µs | yes |
| the 6 s after an overdub (its save) | 136 µs (hard; persistence ≤ 82 µs during the overdub) | 10 µs | 816–836 µs | 147–149 µs | about 1,040 µs | 874–886 µs | yes |
| playback after | 145 µs (hard) | 10 µs | 896 µs | 137 µs | 1,089 µs | 942 µs | yes |
| a take (record), 4 bars on an empty slot | 343 µs | 20 µs (was 3,643) | 729 µs | 176 µs | 969 µs | 999 µs | yes |
| an undo into a cold step | not re-measured since the fix (the first run: grains ≤ 373 µs, fetch 335 µs) | | | | | | grains yes |

**Stress: overdubs back to back (2026-11-11, `c23ede89…`, 300 BPM, seven tracks playing).** Before the history and
LED work a slot hung after about 9 overdubs. A 4-bar take on an empty slot, then overdubs of 1.2 s each sent one after
the other (about two a loop), sampled every 3 or 6:

| Run | Overdubs sent → committed | Slot depth reached | Worst grain | `leds` | `screen` | Non-engine | Worst pass | Content free | Hang |
|---|---|---|---|---|---|---|---|---|---|
| 12 overdubs | 12 → 9 | 10 | 443 µs | ≤ 110 µs | ≤ 415 µs | ≤ 788 µs | 2,058 µs | 2,805–2,816 pages | no |
| 30 overdubs | 30 → 22 | 23 | 581 µs | ≤ 439 µs | ≤ 549 µs | ≤ 1,264 µs | 3,281 µs | 2,810–2,844 pages | no |
| 30 overdubs, `end` as REC (`f90715c7…`) | **30 → 30** | 31 | 686 µs | ≤ 579 µs | ≤ 790 µs | ≤ 1,664 µs | 4,257 µs | 2,812–2,882 pages | no |

- Every grain stayed under g = 810 µs and the non-engine stretch under B_max; no clock gap; the hot table stayed full
  (98–99) with `evicted_undo=0` (older steps went cold on the card, none forgotten); content memory flat.
- The refused overdubs in the first two runs were the test's: the serial `!ENGINE end` ended every take at the next
  bar, so an overdub sent within that wait found the writer still busy (`Busy`). REC on the DROID ends an overdub at
  once (`Surface::endTake`); `!ENGINE end` now does the same (a Record take still at the bar), and all 30 were taken.
- **Grain cost grows with the slot's layers** (third run, worst per class after N overdubs):

  | Overdubs | transition | background | display | hard |
  |---|---|---|---|---|
  | 6 | 382 µs | 327 µs | 355 µs | 173 µs |
  | 18 | 546 µs | 591 µs | 495 µs | 393 µs |
  | 30 | 686 µs | 673 µs | 632 µs | 542 µs |

  About 12 µs more a layer: a grain's work is counted in rows, but each row costs more as the composer merges more
  runs. Extrapolated, g = 810 µs is reached at about 40 layers on one slot, while a state may hold 63 runs (60
  committed); the non-engine stretch follows the same trend (1,664 µs at 31 steps).
- **Where the growth is** (2026-11-11, temporary cycle-counter probes, the same stress; worst single call):

  | Part | 6 layers | 30 layers |
  |---|---|---|
  | `Composer::bind` (binding a state's runs: a cold start, a tile build, a resume) | 396 µs | **711 µs** |
  | the cold-start step (holds `bind`) | 399 µs | 713 µs |
  | one compose grain (`composeRows`) | 180 µs | 432 µs |
  | a window's tiles (`consume`) / its end (`endLookahead`) | ≤ 209 µs | ≤ 209 µs / 48 µs |

  The worst grain is `bind`: it walks every run in one grain (binary searches into PSRAM per run, about 13 µs a run),
  about 1.2 ms at 63 runs. Composing grows less (a row 1.3 → 2.3 µs on average; a quarter of it the OVL-2/6 look-ahead
  over later passes). Ruled out by measurement: the stream selection (a tournament tree over the runs changed nothing)
  and counting it as work (fewer rows a grain — the E5 feasibility gate then missed deadlines). **Open:** bind a state
  in steps of a few runs a grain, then bound the per-row loops over later passes.
- `leds` (the 512-row scan step) and `screen` grow with the slot too: 20 → 579 µs and 231 → 790 µs from the take to
  the 31st step.
- Afterwards the test steps were undone (the slot empty again, its steps as redo).

A pass longer than a tick (`Worst pass`) is not lateness on its own: it holds up to 8 grains with the due ticks played
between them. The largest non-engine part is now the screen (0.8–0.9 ms while playing); the next one to look at if
B_max must shrink.

**First run (`bb0d3b0a…`, before the fix).** Every grain class stayed under g = 810 µs and the card work (saves under
100 µs, the fetch 335 µs) under its 0.9 ms budget, with no clock gap; B_max did not hold:

| Phase | Worst grain (class) | `leds` | `screen` | Within budget |
|---|---|---|---|---|
| playback | 194 µs (hard) | 12–1,500 µs (1,500 with a large slot shown) | 180–850 µs | not with a large slot shown |
| a take (record) | 365 µs (display) | 3,643 µs | 806 µs | no |
| two overdubs | 425 µs (background) | 4,124–4,419 µs | 803–815 µs | no |
| the 6 s after each take / overdub | 223 µs | 1,695–1,801 µs | 772–814 µs | no |
| undo into a cold step (fetch from the card) | 373 µs (persistence 335 µs) | 1,831 µs | 787 µs | no |

**Cause and fix.** Timing the stage's parts (`!ENGINE take` with temporary timers) ruled out the DIN, USB device and
USB host sends (≤ 5 µs each, the DIN buffer never full). The time was in `Surface::leds` scanning the shown slot's
rows: the step LEDs rescanned every row of the state each time the playhead entered a bar (about 1.5 ms a bar on a
large slot), and a new state (each take, overdub wrap or commit) was scanned in one pass (about 3 ms). Now
(`Surface::beyondScan`, commit `8de09b60`):

- the scan of a state also builds the 16ths of each of its first 64 bars, so entering a bar reads a table;
- the LEDs and the screen advance a new state's scan by at most 512 rows a pass (`kScanRows`) and show the last state
  scanned until it is done (a few passes); gestures still finish it at once, so their values are exact.

Tests: `test_step_leds_follow_the_bars_without_a_row_scan`, `test_a_large_state_is_scanned_in_steps` (each caught by a
mutation). A step LED of a bar past the 64th still scans that state's rows (loops longer than 64 bars).

## When a figure changes

A new worst grain above g, a new kind of work on the playing path, or a card operation that can wait: re-measure
on the device, update the source (design or `DEC-###`) and then this page.
