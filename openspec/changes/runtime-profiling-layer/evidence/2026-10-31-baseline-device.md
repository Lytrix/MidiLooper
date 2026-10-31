# P8 — same-day reference capture (pre-profiler default image)

Task 0.8. Owner approval for the scratch-slot clear (track 8, slot 4 1-based / track 7, slot 3 0-based)
given in session on 2026-10-31. Stimulus: the owner chain rig, unchanged
([HITL_TEST_RECIPE.md](HITL_TEST_RECIPE.md)).

## Image and boot

| Item | Value |
|---|---|
| Env | `teensy41-capture-serial` at `7fa10fc2` (profiler compiled out) |
| HEX SHA-256 | `909c930dea06465ebafa61b9d45aa6ae7ad5be1f24a423c2a90bf28e57e4b975` |
| RAM1 | variables 96,192 / code 391,928 + padding 1,288 |
| Upload | `pio run -e teensy41-capture-serial -t upload` → SUCCESS (twice; the second upload only to force a fresh boot for run 2) |
| Boot proof | run 1 `captures/session_20261031_232715.log`: `#CAP,854635,DIAG,boot_reset,crash=0`, `BOOT,load,ok`; run 2 `captures/session_20261031_233358.log`: `#CAP,1375002,DIAG,boot_reset,crash=0`, `BOOT,load,ok` |

Command (both runs): `.venv/bin/python scripts/hitl_midifile_chain.py --hold-clock-on-settle --stop-bars 8
--record-bars 20 --record-pattern ".../16ths.mid" --overdub-pattern ".../chords.mid" --out-dir
captures/midifile-chain-NN`, with `scripts/capture_session.py` recording.

## Validity (timing handoff § 4)

| Run | Capture | Rig report | overdubs | `order_deny` | `preparation_denied` | `win_gap_overrun` | Valid |
|---|---|---|---|---|---|---|---|
| 1 | `session_20261031_232715.log` | `captures/midifile-chain-60` | 12 | 0 | 0 | 0 | yes |
| 2 | `session_20261031_233358.log` | `captures/midifile-chain-61` | 12 | 0 | 0 | 0 | yes |

## Timing (`scripts/parse_overdub_chain_timing.py`)

| Session | midi_gap med / max | late_on med / max | frames > 50 ms (max) | rebuild full / incr |
|---|---|---|---|---|
| 213255 (DEC-238 off, prior) | 81 / 197 ms | 38 / 96 ms | 29 (90 ms) | 71 / 479 |
| 214619 (DEC-238 off, prior) | 94 / 204 ms | 35 / 157 ms | 23 (97 ms) | 72 / 405 |
| **232715 (run 1)** | **75 / 193 ms** | **74 / 170 ms** | **29 (75 ms)** | 79 / 483 |
| **233358 (run 2)** | **72 / 196 ms** | **44 / 155 ms** | **45 (87 ms)** | 72 / 377 |

Run-to-run spread for the slice-3/4 tolerance: midi_gap median 72–75 ms, max 193–196 ms; late_on max
155–170 ms; frames > 50 ms 29–45. Consistent with the DEC-238 off pair.

## Preparation reference (the slice-3 oracle)

`DIAG,view_prewarm_step` (one row per off-edge unit, `us` = the unit's own bracket):

| Run | Units | Units > 5 ms (DEFERRED ceiling) | Max unit | Sum | Ready at |
|---|---|---|---|---|---|
| 1 | 39 | 30 | **17,662 µs** (unit 1, first capture staging) | 299.1 ms | 34.37 s |
| 2 | 38 | 29 | **16,736 µs** (unit 1) | 264.8 ms | 23.27 s |

Next largest: unit 26 (15.6 / 14.7 ms), units 13–16 (10.6–13.8 ms). Matches DEC-231 follow-up 3
(17,350 µs first staging; 13–17 ms single allocations).

Record edge, `DIAG,capture_prewarm_split` on the scratch slot (`track=7,slot=3`):

| Run | Admission (`restamp=0`) | Restamp (`restamp=1`) |
|---|---|---|
| 1 | `okmask=31, views=0, canonical=15, horizon=1,830, stopjobs=115, companion=73, total=2,134 µs` | `total=161 µs` |
| 2 | `okmask=31, views=0, canonical=15, horizon=2,870, stopjobs=36,008, companion=86, total=39,126 µs` | `total=227 µs` |

`views=0` in both runs: the prepared-view adoption (DEC-231) holds. **New observation:** in run 2 the
edge spent **36,008 µs in `stopjobs`** (`prewarmCaptureStopJobs`), above the 25 ms COLD ceiling, against
115 µs in run 1 with the same stimulus and a fresh boot. The 5 s `midi_gap` window containing the edge
reads 70.9 ms (run 2) vs 55.9 ms (run 1). Not investigated here (non-goal); it is the first thing a
`RecordStart_edge` / `PlaybackPrewarm_slot` breach should show in slice 3, and a candidate child identity
for `prewarmCaptureStopJobs` (RETIREMENT.md R4).

## Aborted attempt (recorded, not evidence)

A second run on the **same boot** as run 1 (`session_20261031_233036.log`, no reset: the device refused
`!REBOOT` and did not answer the stop probe) was refused by the rig itself: "view preparation did not
reach Prepared before the Record edge". The clear's `view_prewarm_request,track=7,slot=3,runtime=1,phase=1`
appeared at 330.30 s and **no `view_prewarm_step` followed** within the rig's 5 s wait, while ~4,200
`snapshot_unit` rows from run 1's overdubs were still being stepped. This reproduces the DEC-231
follow-up 3 symptom (request without steps) and indicates preparation starving behind snapshot
construction after a chain. Not investigated here; recorded for the timing work and as a slice-4 coverage
case (`ViewPrewarm_unit` vs `SnapshotPublish_step` under `IdleOwner_unit`).
