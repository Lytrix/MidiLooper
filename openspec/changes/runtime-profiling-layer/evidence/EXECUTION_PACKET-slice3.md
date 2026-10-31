# Execution packet — slice 3 (DEC-231 preparation vertical proof)

From [`docs/Templates/EXECUTION_PACKET.md`](../../../../docs/Templates/EXECUTION_PACKET.md). Tasks 4.1–4.5.

## Identity and scope

- **Sites:** four long scopes, instrumentation only:

  | Identity | Placement | Ceiling |
  |---|---|---|
  | `RecordStart_edge` | whole `Track::startRecording` body | COLD 25 ms |
  | `PlaybackPrewarm_slot` | whole `Track::prewarmPlaybackForSlot` body | COLD |
  | `PlaybackPrewarm_views` | `LoopPlaybackRuntime::prewarmViews`, after its native test counter | COLD |
  | `ViewPrewarm_unit` | block around the `stepViewPrewarm` call in `Track::stepViewPrewarmForSlot`, closed before `viewPrewarmStep` | DEFERRED 5 ms |

  Context: the slot index for all four (track = none: no cheap index exists in `Track`, and a
  non-trivial argument would cost code in the default image).
- **Result:** `ViewPrewarm_unit` passes its `ready` bool.
  - **Deviation (recorded):** `prewarmViews` supplies **no** result. Its body has eight exits, and
    carrying the bool would restructure them, which the slice forbids. The spec makes results optional.
- **Default image:** symbols identical to pristine except `__rtc_localtime` and the compiler's
  `CSWTCH.N` numbering (same addresses and sizes); section headers identical; RAM1 unchanged.
- **Status:** native verified; device verified with two recorded misses (below).

## Native (task 4.3)

New boundary suite `test/test_runtime_profiler_sites`. It compiles the shared capture/storage harness
with `RUNTIME_PROFILE_SCOPES=1` and drives the production owners:

| Case | Assertion |
|---|---|
| slot prewarm | `PlaybackPrewarm_slot` counts each call; `PlaybackPrewarm_views` moves exactly with the production `monolithicViewPrewarmCallsForTest()` counter |
| prepared edge | one `ViewPrewarm_unit` per step with `lastResult=1` (ready); the edge is counted, its own prewarm is counted, `PlaybackPrewarm_views` does **not** move across the edge; edge self < inclusive |
| cold edge | the not-prepared `return false` is a timed exit; preparation progress unchanged |

Mutation checks:
- removing the `prewarmViews` scope fails (`Expected 1 Was 0`);
- dropping `setResult(ready)` fails (`Expected 1 Was 255`).

Native results:
- focused suites: `test_runtime_profiler` + `test_runtime_profiler_sites` 33/33 (after the boot-row fix);
- full native before the fix: **3,816 cases / 8 skipped / 3,808 succeeded**.

## Device (task 4.4)

- **Image:** `teensy41-capture-serial-profile` `f7689e0c10f59fdcfea124d9372aee470462925b5e4ed24539af76f796f1634f`.
- **Boot:** `captures/session_20261101_000756.log`, `boot_reset,crash=0`, `BOOT,load,ok`.
- **Stimulus:** the owner chain rig (`captures/midifile-chain-62`). Valid: 1 Record + 12 overdubs, 0 `order_deny`, 0 `preparation_denied`, 0 `win_gap_overrun`.
- **Read-back:** `scripts/hitl_profile_dump.py --attach` (no reset) → `captures/session_20261101_001044.log`. Stats: 14 rows, `resume=0`. Ring: 32 records, `range 1..32`, `dropped=0`, `gap=0`.

| Identity | calls | avg µs | max µs | over ceiling | P8 reference |
|---|---|---|---|---|---|
| `RecordStart_edge` | 1 | 3,354 (self 742) | 3,354 | 0 | edge split total 2,134 / 39,126 µs |
| `PlaybackPrewarm_slot` | 18 | 8,744 | **124,920** (boot, t = 3.52 s) | 1 | boot calls 3–4.5 ms each |
| `PlaybackPrewarm_views` | **0** | — | — | 0 | `views=0` at the edge |
| `ViewPrewarm_unit` | 40 | 7,883 | 19,507 | 31 | 38–39 units, 29–30 > 5 ms, max 16.7–17.7 ms |

Same run's edge row: `capture_prewarm_split … okmask=31, views=0, horizon=2,054, stopjobs=109, total=2,375 µs`.
The 36 ms `stopjobs` edge from P8 run 2 did not recur.

### Oracle lines

| Line | Result |
|---|---|
| `ViewPrewarm_unit` ring records for units > 5 ms | **PASS** — 31 records, one per `view_prewarm_step` row over 5 ms (31) |
| profiler ≤ row | **PASS** — all 31 |
| profiler within 10 µs of the row | **MISS** — the row exceeds the profiler by 32–42 µs, a constant offset with 10 µs spread. The row's bracket ends with `micros()` evaluated among `viewPrewarmStep`'s arguments, after `resolveTrackIndexForPersistence(*this)` and `viewPrewarmProgress()`; the scope ends before them. The pre-declared 10 µs band was too tight for that bracket, not a placement error. Proposed band for later comparisons: row − profiler ∈ [0, 50] µs. |
| max unit ≈ P8 largest | PASS — 19.5 ms vs 16.7–17.7 ms (one run; same unit 1 = first capture staging) |
| `parentId` none (no `IdleOwner_unit` yet) | PASS — all `parent=none` |
| prepared edge under COLD, no ring record | PASS — 3,354 µs, `over=0` |
| `PlaybackPrewarm_views` does not move across a prepared edge | PASS — 0 calls in the whole run |
| `capture_prewarm_split` still `views=0` | PASS |
| `midi_gap` / `late_on` within the P8 spread; no new overrun | **MISS (inconclusive)** — `midi_gap` med/max 86 / 204 ms vs P8 72–75 / 193–196; `late_on` max 138 ms vs 155–170 (better); `win_gap_overrun` 0. It is inside the four "off" runs of 2026-10-31 (72–94 / 193–204 ms). Four scopes at ≈ 1 µs per call cannot account for an 11 ms median shift; this is one run against a two-run spread. The paired A/B in slice 4 (task 5.5) is the overhead verdict. |

### Findings from the profile

1. **The 125 ms boot `PlaybackPrewarm_slot` breach is the legacy row's USB transmit timeout.**
   - This is red-team R5 seen on device. The Teensy core's `usb_serial` write blocks up to
     `TX_TIMEOUT_MSEC 120` once when no host drains the port (`cores/teensy4/usb_serial.c`).
     `capture_prewarm_split` is a direct `Serial.printf` inside the measured body, and the capture
     attached after the post-upload boot. 124.9 ms ≈ 120 ms + ~5 ms of real work.
   - Plausible, not proven: the boot-time split row itself was not captured, because the host was not
     reading yet.
   - It implies that any direct boot-time row can stall the boot once by 120 ms when no host is
     attached. That's for the timing work, and a further reason to retire timing rows from measured
     bodies (RETIREMENT.md).
2. **The boot row was lost to a late capture.** It was written directly at boot with no host
   reading, so `session_20261101_000756.log` had no `profile_boot` row. **Fixed in this slice:**
   - `Platform::emitRetainedRow` enqueues the boot row on the Tier-A capture ring;
   - the native case asserts the retained path;
   - device re-check on `97b987da99359fa4ef4e993ff93aa5689fec6b87232af1457fc9f888f44ea030`: a capture
     attached about 10 s after boot received `#CAP,3082199,DIAG,profile_boot,source=1,cpu_hz=600000000,…`
     (`captures/session_20261101_001403.log`).

## Retirement evidence (task 4.5, RETIREMENT.md stage 0)

- `view_prewarm_step us=` vs `ViewPrewarm_unit`: same 31 breaches, row − profiler 32–42 µs. **Equivalent
  within [0, 50] µs**, so the `us=` field is redundant once the profiler is in the image that is read.
  The `ready=1` event stays (the HITL rigs wait on it).
- `capture_prewarm_split`: `PlaybackPrewarm_slot` reproduces the edge total (edge 3,354 µs inclusive vs
  split 2,375 µs plus the edge's own 742 µs self ≈ 3,117 µs, plus the row's own emission). It does **not**
  reproduce the per-reserve split (stopjobs / canonical / horizon / companion / order), which has no
  identity yet. **Not retirable yet** (R4); child identities would be needed first, the stop-job reserve
  being the first candidate.
- **No row is removed in this change.**

## Completion record

- Eligible for checkoff: 4.1–4.5, with two recorded misses (10 µs band; single-run timing spread).
  Neither blocks the slice's purpose: the attribution matches the reference and no new overrun appeared.
- Board left on the profile image `97b987da…`.
