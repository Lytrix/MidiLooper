# Device oracle attempt — 2026-10-30

First load-bearing attempt for section 5. **Not accepted.** This note records what the run proved, what it
did not, and why the next attempt uses the acceptance capture env.

## Image and run

- Image: `teensy41-capture-serial`, HEX
  `sha256 6a84c00362a6b01bdae479860ed7fbc3c6822fcead64636af06617c42f858f01`, retained in
  `captures/6a84c003/`. Flash: Homebrew `teensy_loader_cli 2.3`, `Found HalfKay Bootloader` ->
  `Programming` -> `Booting`, exit 0. Boot verified separately: `DIAG,boot_reset,crash=0`,
  `BOOT,load,ok`, `BOOT,ram1,192512`, `BOOT,ready,settled=1,tracks=8`, USB `Teensy MIDI`.
- Run: `host_midi_hitl.py run --preset overdub_occupy_paint --track 8 --loop-slot 4 --midi-channel 1
  --boot-reset` with the TEMP variant `STIM-OCCUPY-PAINT-01-TEMP-RIAO-ORACLE` and legacy overrides
  `--record-bars 32 --overdub-bars 32 --second-overdub-bars 32 --note-gap-ms 80 --gate-ms 20`.
- Capture: `captures/riao-oracle-20261030/session_20261030_085641.log` (1.6 MiB, 33,125 lines). The
  managed run hit its 360 s watchdog during the second overdub; the record and first overdub committed,
  the second stop landed after the watchdog. Recorded length: **32 bars / 24,576 ticks**, `RECS,stop`
  note count **228** (the occupy-paint pattern is sparse; this run is not the 610-note load).

## Results

| Acceptance | Result |
|---|---|
| zero `loop_rem,idle_maint` | **FAIL — 2** |
| zero `win_gap_overrun` | PASS — 0 |
| max `midi_gap` | 378,136 us at 14.4 s; 330,646 us at 24.4 s (record-start host-stream pauses) |
| max `clk` / `extclk_arrival` | 0 / 0 (no external clock in this run) |
| `persist_save` breaches named by step | 13 rows, names and budgets present |

### The two idle-maint rows

```
#CAP,11835465,DIAG,loop_rem,idle_maint,145872,4,1,1,0
#CAP,343610094,DIAG,loop_rem,idle_maint,64478,255,255,255,0
```

1. **145,872 us at 11.8 s, load job active (`track=4 slot=1 phase=1`).** This is the boot restore
   window, not the live overdub. The preceding rows are the focused slot's `VCACHE,slice_clean` with 641
   restored notes and a `disp_turn`; the 146 ms turn is the boot/first-paint lane with a deferred slot
   restore job in flight. It is a real `idle_maint` row and is not excluded by the task's wording, but it
   is not the live-overdub turn this change targets.
2. **64,478 us at 343.6 s, no load job (`255,255,255`).** This is during the second overdub. The turn
   contains 14 `#CAP,...,DIAG,lcr,srcspan,at=view,...` rows — the capture-only source-view dump
   (`SESSION_CAPTURE_LCR`), one line per note plus a count. Those rows are emitted inside the measured
   idle turn and are a capture-build instrumentation cost, not production work. The production view cut
   is not separately timed here.

## What this changes

- The first attempt used `teensy41-capture-serial`, which leaves `SESSION_CAPTURE_LCR=1` and therefore
  the per-span source-view dump. The project's documented acceptance target is
  `teensy41-capture-serial-no-grain` (`SESSION_CAPTURE_GRAIN=0`, `SESSION_CAPTURE_DROID_QUEUE=0`,
  `SESSION_CAPTURE_LCR=0`). Task 7.3 requires the acceptance to be readable **without** subtracting
  capture-only rows; this run shows why.
- The second attempt should use the acceptance env, a **base-preset dense** 32-bar record plus one dense
  32-bar overdub (no second overdub), a boot settle before the record arm so the boot restore is outside
  the measured live window, and the same row oracle. The base preset is authorized for track 8 slots
  4/5 (`STIM-BASE-2-2-01`).
- The boot 146 ms row is a separate load-path finding. It must be either bounded by the load owner or
  explicitly scoped out of the live-overdub acceptance with the owner's agreement; it is not silently
  dropped.

## Acceptance image prepared

`teensy41-capture-serial-no-grain` build SUCCESS, HEX
`sha256 36160d60529c2650f279baaa2ad2264f548ec39be40b7372072de41a32c987e7`, retained in
`captures/36160d60/`; RAM1 `96,192`, ITCM `392,248 + 968`, locals 34,880. Not flashed.
