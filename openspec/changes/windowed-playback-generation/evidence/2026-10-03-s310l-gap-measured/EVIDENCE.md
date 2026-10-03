# Slice 3.10 (increment 12) — the service-gap unit, first measured on the device (and the limit of this first instrument)

**Status:** 2026-10-03, device run **PASS** with the current firmware. The unit is now measured, and the measurement has a
**known limit of my own making** which is recorded here rather than averaged away.

## The run, and its identity

`host_midi_hitl.py run --preset base --track 8 --loop-slot 4 --midi-channel 1 --build-upload` — the canonical baseline
stimulus with **its own declared targets** (`track 8`, slots 4–5; the earlier failed leg used track 5/slot 1, which is why
its content never armed). **`Result: PASS`** ✓, and `metadata.json` pins it to
**`git_commit a545def42d26…` / `firmware_hex_sha256 531259700436…`** — i.e. the firmware that carries the sampler.

**34 `DIAG,win_gap` rows over 528 lane opportunities** (`#CAP,<micros>,DIAG,win_gap,<maxDeltaTicks>,<opportunities>,<phase>`).

## What the series says

| Quantity | Measured |
|---|---|
| **phase advance per lane opportunity** — the *unit* the admission is declared in | median **3.25 ticks**, p90 **3.5** |
| wall clock per lane opportunity | median **8 458 µs** (p90 9 153 µs) |
| the sampler's reported **max** per row | min 0, mean **279.5**, **max 9 367** |
| the declaration this measures | `maximumPhaseAdvancePerTurn = 768` ticks (one bar) |

**Two readings, and they must not be conflated:**

1. **Under continuous transport the unit is ~3.25 ticks per lane opportunity** — i.e. the lane is served about every
   8.5 ms, and at this configuration the playhead moves ~3 ticks between services. That is **two orders of magnitude
   below** the declared one-bar-per-turn figure (768), which is why the declared envelope refuses work it could easily
   service. This is the figure the derived admission wants for the steady state.
2. **The large outliers (up to 9 367 ticks, over a 1.54 s gap) are almost certainly transport-state transitions or loop
   wraps, not service gaps** — the capture spans record/overdub/stop legs, and while the transport is stopped the tick
   does not advance at all (a *long wall-clock gap with no phase advance*), while a start/stop or a wrap can move the
   phase discontinuously.

## The limit of this instrument (mine, and it blocks the *bound*)

**`DIAG,win_gap` carries no transport state**, so the series cannot separate "the lane was not served for a long time"
from "the transport was stopped", nor a tick-wrap jump from a real gap. The **typical** figure above is therefore sound
(the steady-state opportunities dominate the median), but the **worst-case** figure — the one an admission bound needs —
**cannot be read off this series**. Fixing it is small and specific: either (a) carry the transport/clock state in the row
(a bool and the clock-source state), or (b) **sample only while the transport is running**, so every row is a true service
gap. Either makes the max meaningful; the row is unchanged otherwise.

## What is therefore claimed, and what is not

- **Claimed:** the unit exists, is measurable, and under continuous transport is **~3.25 ticks per lane opportunity**
  (median, p90 3.5) on this firmware, this stimulus and this tempo — versus a declaration of 768.
- **Not claimed:** any *bound*. The worst case needs the state discrimination above, and only then can the supported
  `H × K` contention envelope be declared and the derived admission re-based and enabled.
- **Also not claimed:** that 3.25 generalises across tempos, loop lengths, track counts or clock sources — one run, one
  configuration, 528 opportunities.

## Next

1. Extend the row (or gate it on the transport running) so the max is a *service* gap; re-run the same leg.
2. With a real worst case, declare the supported `H × K` envelope (the review's step 2) and re-derive the admission.
3. Only then enable the derived admission (currently implemented and disabled) and re-base the fixtures it refuses.
