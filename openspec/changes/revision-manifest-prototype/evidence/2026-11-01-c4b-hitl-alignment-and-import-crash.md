# C4b — the midifile chain recorded off the grid; an importer crash; why hides do not appear

Date: 2026-11-01. Owner report: the HITL "runs the same midi file twice, but all notes are offset, which causes no
hide to be made ever, while they should overlap exactly".

- Images:
  - probe/shadow `99a962ee…`: the per-run phase diagnostic; this image crashed;
  - `ed82d0b6e0bf1cba…`: the importer fix.
- RAM1 is unchanged: probe 96,192 / 392,072 / 1,144; default 96,192 / 391,944 / 1,272.
- Raw rows:
  - [before the alignment fix](2026-11-01-c4b-phase-status-raw.txt);
  - aligned: [status](2026-11-01-c4b-aligned-status-raw.txt), [windows](2026-11-01-c4b-aligned-windows-raw.txt).

## 1. The offset (host tooling bug, confirmed on device)

`!REVSHADOW_STATUS` now prints `run_phase` for each imported pass: the first NoteOn and a histogram of NoteOn ticks
modulo a 16th (48 ticks).

| Run | Record take start | Record NoteOns off the 16th grid | Overdub NoteOns off the grid |
|---|---|---|---|
| `session_20261101_204351` (before) | tick 120 | **316 / 316 at +24** | 0 (all 12 passes on the grid) |
| `session_20261101_204929` (after) | tick 120 | **0 / 314** | 0 |

Cause:
- `hitl_midifile_chain.py` sent the Record press 85 ms before `Start` and assumed the take started at device tick 0.
- The device acts on a record press only when its short-press window expires. With the transport already running,
  the take starts then, at tick 120, 128 or 160 (15–20 clocks late).
- The take is anchored there: `TrackCaptureInput.cpp` sets `loop.startLoopTick = currentTick` and stores
  `tick - startLoopTick`.
- The Record pattern therefore sat `startLoopTick mod 48` off the grid that the overdubs used after the restart.

Fix (host tooling only):
- The chain reads `Recording started @ tick N` from the capture log.
- It streams the Record pattern with position 0 on pulse `origin + N / TICKS_PER_CLOCK`, joining at the next unsent
  pulse; the first 1–2 sixteenths of the take are not sent.
- `stream_pattern` gained `phase_origin_pulse`. Its default is unchanged.
- Reported as `record_take_start_tick` and `record_phase_origin_pulse`.
- Tests:
  - `scripts/test_hitl_midi_pattern.py` gains 2 cases and is OK;
  - the clear, clock, host-grid-record and first-note suites are OK.

Other scenarios that stream a pattern from the grid origin after a press (for example
`note_edit_overdub_compose`) may share the assumption; they were not changed or checked here.

## 2. Importer crash (fixed)

The `99a962ee` run hard-faulted during overdub 10:
- `crash=1`;
- `CrashReport`: data access violation at address `0x10`;
- PC in `ImportJob::stepCaptures`.

The job kept raw pointers into production's `overdubPasses` and `editPasses` vectors across steps. A rebuild of those
vectors with the same content leaves the frozen source key unchanged while the storage moves.

The job now keeps pass and edit **ids**, resolved against the live `Loop` on every use (`SourceMoved` when one is
gone). New test `test_import_survives_pass_storage_rebuilt_with_the_same_content`:
- it swaps in a copy of the overdub vector mid-import and poisons the old storage;
- RED on the old code (84 vs 80 rows), GREEN now.

The image with the fix (`ed82d0b6`) ran both later chains with 0 crashes.

## 3. Why there are still no hides (a production finding, not changed)

With the take aligned, every same-pitch overdub note lands exactly on a Record note.
- Production's occupy resolve sees it: `lcr,part,why=on,from=ledger,n=1,a=1`, 73 to 80 times, for example
  `as=912,ae=960,hs=912`. A starts at B's hold, so the documented rule is Hide.
- But the overdub stop dropped the companions:
  `Overdub stop: companion path skipped (canonical derivation unavailable); raw pass only`
  (`TrackOverdubLifecycle.cpp`, `retireWithoutCompanions`).
- No Delete edit pass is sealed (`alloc_census … edit=0`), and both copies stay in the passes view.

| Session | Overdub stops | Companion path skipped |
|---|---|---|
| `200525` (the run with 33 hides) | 12 | 9 |
| `202857` | 12 | 10 |
| `203140` | 12 | 10 |
| `204351` | 12 | 10 |
| `204929` (aligned) | 12 | **12** |

So whether a run carries hides depends on how many stops keep their companion path, not on the stimulus. The
`200525` hides came from the 3 stops that kept it. This is production's degradation path (raw pass authoritative).
It belongs to the overdub-audibility investigation, not to Stage C.

## C4 on the aligned run

| Reading | Value |
|---|---|
| Gates: record / overdubs / cap / cleared | 1 / 12 / 13 / yes |
| Whole loop, shadow vs passes | **1,202 / 1,202**, `c4_clean=1` |
| Windows | 10 / 10 |
| Import worst / phases (begin, captures, pair walk) | 193 µs / 99, 193, 182 µs; `slow_seals=0` |
| Edit runs / hides | 0 / 0 (production sealed no companions) |
