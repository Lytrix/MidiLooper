# 2026-11-01 — Phase 1 baseline: consumed routes and playback misses

**Image.** Default `teensy41-capture-serial`, `30c6f7693ed2b879839eae9f9a9f438e480d134a11e502167af8e495d52dd667`.
Boot proven (`boot_reset,crash=0`, `BOOT,load,ok`).

**Run.**
- Owner chain (`captures/midifile-chain-79`), capture `captures/session_20261101_031849.log`.
- Valid: 12 overdubs, 0 `order_deny`, 0 `preparation_denied`.
- The capture was held 45 s after the rig, so the host-clock-lost internal-clock interval is included.
- Decoded with `scripts/parse_tick_routes.py`. The clock split is keyed on the firmware's `External MIDI clock detected`
  and `MIDI clock lost, switching to internal` lines.

| Interval | 5 s windows | view | bridge | paged | merged | none | miss_int | miss_ext |
|---|---|---|---|---|---|---|---|---|
| before clock | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| external | 25 | 5,809 | 0 | 0 | **35,448** | 0 | 0 | 0 |
| internal | 10 | 18,386 | 0 | 0 | **111,073** | 356 | **356** | 0 |
| **total** | 37 | 24,195 | 0 | 0 | **146,521** | 356 | 356 | 0 |

**Readings.**
- The merged fallback serves **85.6 %** of 171,072 slot-ticks, on both clocks.
- The windowed lane serves the rest as `view`, so it is selected for some slot or phase but not for most of the long-loop
  ticks. Phase 2 must therefore name the reason per slot and per phase, not only per run.
- Bridge and paged delivery are never consumed.
- **356 internal-clock misses in about 45 s.** These are DEC-241 containments, about 8 per second, and they are the
  phase-4 baseline.
- External misses are 0 by construction: the main-loop tick still builds until phase 5.
- No `RING,stats` row is in this capture, so the decoder reports the totals as possibly incomplete. The Tier-A row
  family was delivered in all 37 windows, which matches the run's length.
- The internal-clock interval has more slot-ticks per window than the external one: the internal tick rate and
  per-slot counting differ. Only the per-route *shares* are compared across clocks.

**Timing, same run (5 s rows, for reference only).**
- External: midi_gap median 26.0 ms, max 140.1 ms; late_on max 101 ms.
- Internal: midi_gap median 9.2 ms, max 12.4 ms; late_on median 74.1 ms, max 75.4 ms.
