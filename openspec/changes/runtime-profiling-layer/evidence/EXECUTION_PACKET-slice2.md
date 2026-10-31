# Execution packet — slice 2 (bounded commands, Tier-A, host decoder)

From [`docs/Templates/EXECUTION_PACKET.md`](../../../../docs/Templates/EXECUTION_PACKET.md). Tasks 3.1–3.7.

## Identity and scope

- **Contract:** spec § profile commands, § Tier-A, § host decoder; design D8, D9; DEC-239 point 3.
- **Status:** native verified; build/RAM1 verified; device verified (task 3.7) except the live-scope
  refusal, which is structurally unreachable on device in this slice (see Completion record).
- **Owner / entry:**
  - `ProfileDiagRows::handleCommand` / `dumpStatsBatch` / `dumpRingBatch` (`src/Utils/ProfileDiagRows.cpp`, carved);
  - cursor state `RuntimeProfiler::DumpCursors` in the `DMAMEM` state;
  - dispatch: one `#if RUNTIME_PROFILE_SCOPES` branch in `StorageManager::processHitlSerialCommands`, with no literal in that TU;
  - classifier: `kProfileTierPrefix` (`PROGMEM`, gated) in `isTierATextLine`;
  - host side: `scripts/profile_report.py`, the capture allowlist in `scripts/capture_session.py`, and the device-leg tool `scripts/hitl_profile_dump.py`.
- **Contract refinement, recorded:** a ring dump **drains**. `DumpCursors::ringDrained` makes a later
  session start after the last emitted (or gap-skipped) seq, so a record is never emitted twice across
  sessions. Reset clears it; `seq` stays monotonic. This is how D8's "never duplicated" holds across
  sessions, not only inside one.
- **Non-goals:** any production scope (slice 3), runtime arming (follow-up).

## Oracle (native, `test_runtime_profiler` slice-2 cases)

| Case | Discriminating assertion |
|---|---|
| stats batches | 14 dumpable ids drain in ⌈14/4⌉ = 4 batches of `dumpStatsBatch(4)` with `resume=1,multi_batch=1` until the last; the command emits one batch of 14 with `resume=0,multi_batch=0`; `Calibration` never dumped |
| stats averages | two calls of 100 / 300 µs → `avg_us=200,avg_self_us=200,max_us=300,last_us=300,last_result=255` |
| ring batches | 70 breaches → 3 requests, 32 / 32 / 6 rows, seq 1..70 in order, range 1..70; a 4th request is empty (`range_start=71,range_end=70`) |
| pinned horizon | 40 pinned, 5 arrive mid-dump → second batch emits 33..40 only; next session is exactly 41..45 |
| overwrite gap | pinned 5..260, 100 more breaches → `gap=68` reported, 105..260 emitted (156 rows) |
| depth-zero gate | each of the three commands under a live scope → one `profile_cmd,refused,depth=1` row and the whole RAM2 state byte-identical (`memcmp`) |
| reset | clears table, ring and both cursors mid-dump; `profile_reset,15`; seq continues at 41 |
| non-profile lines | not consumed, no row |
| row shape | every row < 224 B, `DIAG,profile_` within the 47-byte head read at a 10-digit timestamp |

**Mutation checks (each restored):**

| Mutation | Result |
|---|---|
| no depth gate | refused case fails (`Expected 1 Was 15`) |
| no drain memory | two cases fail (`Expected 1 Was 33`, `Expected 41 Was 1`) |
| horizon re-read every batch | two cases fail |
| gap skipped silently | overwrite case fails |
| batch bound ignored | three cases fail |

## Tier-A and markers

- `test_capture_ring`: new `test_runtime_profiler_rows_are_tier_a_in_the_source_list_only`. All six row
  shapes are Tier-A by `isTierATextLine` and **not** by `CaptureLineTier::isTierALine`. `profilex` and
  `profil_stat` are rejected.
- `test_capture_line_tier`: header list unchanged (`profile_stat` not Tier-A there).
- `check_marker_contract.py`: `OK` for `DIAG,profile_boot,`, `profile_stat,`, `profile_ring,`,
  `profile_reset,` and `profile_cmd,` (prefix `DIAG,profile_`, 13/31 bytes). `--audit` exits 0.
- `kProfileTierPrefix` is in `.text.progmem` (`0x600dbbd0`); the default env has no such symbol.

## Completion record

- **Native focused:** `test_runtime_profiler` 30/30. `test_capture_ring` + `test_capture_line_tier`: 63/63.
- **Host:** `scripts/test_profile_report.py` 8/8, covering complete, multi-batch/cut-off, Tier-A
  eviction, overwrite gap, no profile rows, no `RING,stats`, poisoned/refused, and duplicate rows.
  `scripts/test_hitl_command_forwarding.py` 26/26 (profile commands allowlisted exactly).
  `profile_report.py` on the P8 reference log (no profiler) prints "not armed / unavailable", not zeros.
- **Native full:** `pio test -e native` → **3,813 cases: 8 skipped, 3,805 succeeded**, exit 0.
- **Build / RAM1:**
  - all three envs at variables 96,192, code 391,928 + padding 1,288;
  - profile HEX `8c6108996dae1b1e847c652f980f10292e6d9a9aa8143d6ff9bc108ca03c92b1`;
  - profiler `CSWTCH` tables in FLASH;
  - default ELF symbols identical to pristine except `__rtc_localtime`, and its section headers identical.
- **Device (task 3.7), `captures/session_20261031_235007.log`:**
  - Uploaded `8c610899…`; the capture forced a fresh boot: `#CAP,2163002,DIAG,boot_reset,crash=0`, `BOOT,load,ok`.
  - **Exactly one** boot row: `profile_boot,source=1,cpu_hz=600000000,trust_us=6158278,samples=256,short_min_cycles=286,short_avg_cycles=361,long_min_cycles=388,long_avg_cycles=414`.
  - Host clock running (`hitl_profile_dump.py`, `MidiClockMaster` at 120 BPM). The device's Play/Stop LED (note 40) was lit from 6.53 s to 11.25 s, which spans every dump.
  - Dumps:
    - `!PROFILE_STATS` → 14 rows + `count=14,resume=0` in one batch;
    - `!PROFILE_RING` → `count=0,resume=0,range_start=1,range_end=0`;
    - `!PROFILE_RESET` → `profile_reset,15`;
    - stats again → 14 rows.
  - `profile_report.py` decodes the log: `capture ring: no loss`, aggregate complete.
- **Not run on device:** the live-scope refusal (`profile_cmd,refused`). With no production scope, no
  scope can be live when `processHitlSerialCommands()` runs, and once sites land it still runs at depth 0
  from `loop()` (site review). Covered natively, byte-identical state. **NOT APPLICABLE** on device.
- **Measured scope cost vs the design estimate:**
  - Design D2 estimated 20–35 cycles (short) and 60–90 (long). The device measured min 286 / 388 cycles
    (0.48 / 0.65 µs), because the constructor, destructor and platform reads are out-of-line `FLASHMEM`
    functions (the ITCM-headroom choice).
  - That cost is about 0.3 % of a 250 µs HOT ceiling and about 1.3 % of a 50 µs CLOCK ceiling. No site in
    this change uses CLOCK.
  - Recorded for the slice-4 overhead A/B. No change made.
- **Eligible for checkoff:** 3.1–3.7.
