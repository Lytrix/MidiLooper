# Slice 3.10 (increment 10) — the service-gap unit is NOT measurable from the existing instrumentation (device run, negative result)

**Status:** device work attempted 2026-10-03 with the board attached; **the measurement the review asked for cannot be taken
yet**, and the reason is specific. Nothing is claimed as measured. No code changed.

## What was run, and its identity

`.venv/bin/python scripts/host_midi_hitl.py run --preset base --track 5 --loop-slot 1 --midi-channel 1 --build-upload`
(the **canonical baseline preset** — the authoritative entry point per `.agents/skills/hitl-content-verification`), managed
capture, bound `HITL_MANAGED_RUN_TIMEOUT_S=420`.

- **Run identity (from `metadata.json`):** `git_commit = 24134bd88a433047a055cf404ff94fe65492f640`,
  `firmware_hex_sha256 = 5e1dfa21e1d6baad28f0cbe476ec70aa57f9356e103b40d3fbb6310414c7af81`, `preset = base`, `loop_slot = 1`.
- **Outcome: `INCONCLUSIVE_CAPTURE (SERIAL_INCOMPLETE)`**, abort reason *"recording not confirmed before note stream"*;
  `RECA,entered=0` in the capture shows the **record gesture never armed**. The baseline stimulus declares
  `targets=track 8 slots 4,5`, so the likely cause is **my target choice** (`--track 5 --loop-slot 1`), exactly the
  arm/targeting failure the skill's §2 warns about. **This run is therefore not a pass and is not presented as one.**

## The measurement attempt, and why it fails (the useful result)

The unit the review asked to define is *the phase the playhead advances between two `PlaybackViewMaintenance`
opportunities*. The rows that exist carry **durations**, not opportunity gaps:

| Row | What it actually is | Observed in this capture |
|---|---|---|
| `DIAG,service_period,<maxUs>,<overCount>` | the **maximum duration** of one idle service in the window (5 s cadence) | 0, 0, **2604**, 0 — i.e. up to 2.6 ms of work, not a period |
| `DIAG,idle_maint,<maxUs>,<overCount>` | the same shape for idle maintenance | 84 (µs) |
| `DIAG,loop_rem,idle_maint,52556,…` | an attributed span | 52.6 ms |
| `#CAP,<micros>,BAR,<tick>,…` | a bar **boundary** row | **one** row in 2 559 (tick 0) |
| `DIAG,clk,0` | clock span | 0 — carries no tick |

So there is **no row pairing the playhead phase with the number of idle/lane opportunities**: durations cannot give a gap,
and `BAR` rows are far too sparse to pair ticks with time. **The service-gap unit needs its own gated instrumentation
before any device data can be collected** (AGENTS.md: performance instrumentation stays behind session capture or an
explicit debug gate).

## The corrected next step (what the review's order now requires)

1. **Add the row** (firmware, gated): one row per `PlaybackViewMaintenance` opportunity carrying the current phase (and so
   the delta from the previous opportunity), cadence-bounded through `Utils/TelemetryCadence.h`, placed `DMAMEM`/`FLASHMEM`
   per DEC-108 §13b, with the RAM1 guard checked (the approved line has 48 B of headroom above the floor).
2. **Then** one HITL run with the **stimulus's own targets** (`track 8`, slots 4–5) so the content leg can pass, collecting
   the phase-per-opportunity series under a running transport.
3. **Then** the unit is measured, the supported `H × K` contention envelope declared, the derived admission re-based and
   enabled — in that order, as recorded in increment 9.

## Not claimed

- **No unit value, no envelope, no bar figure.**
- **The run above is not evidence of content correctness** (`INCONCLUSIVE_CAPTURE`, `RECA entered=0`); it pins the firmware
  and shows the flash/run path works and the row semantics are as tabulated.
- The device was left in the harness's post-run state (capture stopped, transport stopped by the harness).
